#include <upcxx/ze.hpp>
#include <upcxx/ze_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/os_env.hpp>

namespace detail = upcxx::detail;
using upcxx::ze_device;
using upcxx::gpu_device;
using upcxx::experimental::os_env;
using id_type =          ze_device::id_type;
using device_handle_t =  ze_device::device_handle_t;
using driver_handle_t =  ze_device::driver_handle_t;
using context_handle_t = ze_device::context_handle_t;

using std::size_t;
using std::uint64_t;

#if UPCXXI_ZE_ENABLED
using upcxx::backend::ze_heap_state;
namespace ze = detail::ze;

namespace {

  ze_result_t ze_init(bool errors_return = false) {
    static ze_result_t res = []() { // first call
      return zeInit(0);
    }();
    if_pf (res != ZE_RESULT_SUCCESS && !errors_return) {
      ze::ze_failed(res, __FILE__, __LINE__, "zeInit(0)", true);
    }
    return res;
  }

  bool ze_all_devices() { // deliberately undocumented experimental feature
    static bool result = os_env<bool>("UPCXX_ZE_ALL_DEVICES", false);
    return result;
  }

// ================================================================
// enumerate_ze_devices(fn)
// Enumerates all the ZE devices in a deterministic order,
// invoking provided callback fn with info for each device:
//   result_type fn(device_ordinal_id, ze_device_handle_t, ze_driver_handle_t)
// If fn returns value not equal to result_type(), the enumeration stops returning that value
// Otherwise, returns result_type() after completing the enumeration

  // Minimal C++11 hack to cleanly allow void result_type
  struct or_void { bool operator==(or_void const &) { return true; } };
  template<typename T>
  T&& operator,( T&& x, or_void ){ return std::forward<T>(x); }

  template<typename Fn>
  auto enumerate_ze_devices(Fn &&fn) -> decltype(fn(0,0,0)) {
    using result_type = decltype(fn(0,0,0));

    ze_init();
    id_type device_id = 0;

    uint32_t driverCount = 0;
    UPCXXI_ZE_CHECK_ALWAYS( zeDriverGet(&driverCount, NULL) );
    std::vector<ze_driver_handle_t> drivers(driverCount);
    UPCXXI_ZE_CHECK_ALWAYS( zeDriverGet(&driverCount, drivers.data()) );
    for (auto driver : drivers) {
      uint32_t deviceCount = 0;
      UPCXXI_ZE_CHECK_ALWAYS( zeDeviceGet(driver, &deviceCount, NULL) );
      std::vector<ze_device_handle_t> devices(deviceCount);
      UPCXXI_ZE_CHECK_ALWAYS( zeDeviceGet(driver, &deviceCount, devices.data()) );
      for (auto device : devices) {
        ze_device_properties_t deviceProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
        UPCXXI_ZE_CHECK_ALWAYS( zeDeviceGetProperties(device, &deviceProperties) );
        if (deviceProperties.type != ZE_DEVICE_TYPE_GPU && !ze_all_devices()) continue;

        auto r = ( fn(device_id, device, driver) , or_void() );
        if (!(r == decltype(r)())) return (result_type)r;

        device_id++;
      }
    }
    return result_type();
  }
// ================================================================

  GASNETT_COLD
  detail::segment_allocator make_segment(int heap_idx, void *base, size_t size) {
    ze_heap_state *st = heap_idx <= 0 ? nullptr : ze_heap_state::get(heap_idx);
    auto dev_alloc = [st](size_t sz) {
      ze_device_mem_alloc_desc_t allocDesc{ ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC };
      allocDesc.ordinal = 0; // ZETODO: should we allow other memory ordinals?
      void *p = 0;
      ze_result_t r = zeMemAllocDevice(st->zeContext, &allocDesc, 
                                       sz, /*alignment=*/0, st->zeDevice, &p);
      switch(r) {
        case ZE_RESULT_SUCCESS: break;
        case ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY: break; // oom returns null
        case ZE_RESULT_ERROR_UNSUPPORTED_SIZE: break; // "too big" also returns null
          // ZETODO: This case has been seen to arise on some systems when
          // ze_device_properties_t.maxMemAllocSize < ze_device_memory_properties_t.totalSize
          // with more effort we could bypass the allocator and manually map the device memory
        default: // other unknown errors are immediately fatal:
          std::string s("Requested ze_device segment allocation failed: size=");
          s += std::to_string(sz);
          ze::ze_failed(r, __FILE__, __LINE__, s.c_str());
      }
      return p;
    };
    auto dev_free = [st](void *p) {
      UPCXXI_ZE_CHECK_ALWAYS(zeMemFree(st->zeContext, p));
    };
    std::string where("device_allocator<ze_device> constructor for ");
    if (st) where += "ZE device " + std::to_string(st->device_id);
    else    where += "inactive ze_device";
    return ze_heap_state::make_gpu_segment(st, heap_idx, base, size, 
                                             where.c_str(), dev_alloc, dev_free);
  } // make_segment

} // anon namespace
#endif // UPCXXI_ZE_ENABLED

GASNETT_COLD
std::string ze_device::uuid(id_type device_id) {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_ZE_ENABLED
  int dev_n = ze_device::device_n(); // handles ze_init
  UPCXX_ASSERT_ALWAYS(device_id >= 0 && device_id < dev_n);
  return enumerate_ze_devices(
    [&](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) -> std::string {
      if (id == device_id) {
        ze_device_properties_t prop{ ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
        if ( zeDeviceGetProperties(zeDevice, &prop) == ZE_RESULT_SUCCESS) {
          std::stringstream ss;
          // Intel GPUs use an 8-4-4-4-12 binary UUID
          // see:  clinfo -a | grep UUID
          int i = 0;
          for (auto v : prop.uuid.id) {
            if (i == 4 || i == 6 || i == 8 || i == 10) ss << "-";
            ss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)v;
            i++;
          }
          return ss.str();
        } 
        else return "Unsupported query";
      } 
      return {};
    });
#else
  return "ZE support is disabled in this UPC++ install.";
#endif
}

GASNETT_COLD
std::string ze_device::kind_info() {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_ZE_ENABLED
  std::stringstream ss;

  int dev_n = 0;
  ze_result_t res = ze_init(true);
  if (res != ZE_RESULT_SUCCESS) {
    ss << "zeInit() failed: err=0x" << std::hex << (int)res << "\n";
  } else {
    dev_n = ze_device::device_n();
  }

  // Latest version the SDK header knows about
  ss << "ZE API VERSION: " << ZE_MAJOR_VERSION(ZE_API_VERSION_CURRENT)
                    << "." << ZE_MINOR_VERSION(ZE_API_VERSION_CURRENT) << '\n';

  ss << "Found " << dev_n << " ZE devices:\n";
  
  if (dev_n > 0) {
    ze_driver_handle_t lastDriver{}; 
    int driver_n = 0;
    enumerate_ze_devices(
    [&](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
       if (zeDriver != lastDriver) {
         lastDriver = zeDriver;
         ss << "ZE Driver " << driver_n++ << ":\n";

         ze_api_version_t version{}; // Runtime version reported by driver
         if ( zeDriverGetApiVersion(zeDriver, &version) == ZE_RESULT_SUCCESS && (int)version > 0) {
           ss << "  API version: " << ZE_MAJOR_VERSION(version)
                           << "." << ZE_MINOR_VERSION(version) << '\n';
         }
         ze_driver_properties_t driver_prop{ ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES };
         if ( zeDriverGetProperties(zeDriver, &driver_prop) == ZE_RESULT_SUCCESS && 
              (int)driver_prop.driverVersion > 0) {
           ss << "  Driver version: 0x" << std::hex << driver_prop.driverVersion << std::dec << '\n';
         }

         uint32_t driverExtCnt = 0;
         if ( zeDriverGetExtensionProperties(zeDriver, &driverExtCnt, nullptr) == ZE_RESULT_SUCCESS && 
              driverExtCnt > 0) {
           std::vector<ze_driver_extension_properties_t> driverExtProp(driverExtCnt);
           if ( zeDriverGetExtensionProperties(zeDriver, &driverExtCnt, driverExtProp.data()) == ZE_RESULT_SUCCESS) {
             ss << "  Driver extensions: ";
             for (auto &prop : driverExtProp) {
               ss << prop.name << ' ';
             }
             ss << '\n';
           }
         }

       }
       ss << "  ZE device " << id << ":\n";
       ze_device_properties_t prop{ ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
       if ( zeDeviceGetProperties(zeDevice, &prop) == ZE_RESULT_SUCCESS) {
          #define SHOW_PROP(fmt, desc) \
            ss << "    " desc ": " << std::dec << fmt << '\n'
          SHOW_PROP(prop.name, "name");
          ss << "    type: ";
          switch(prop.type) {
            case ZE_DEVICE_TYPE_GPU: ss << "GPU"; break;
            case ZE_DEVICE_TYPE_CPU: ss << "CPU"; break;
            case ZE_DEVICE_TYPE_FPGA: ss << "FPGA"; break;
            case ZE_DEVICE_TYPE_MCA: ss << "MCA"; break;
            case ZE_DEVICE_TYPE_VPU: ss << "VPU"; break;
            default: ss << "unknown"; break;
          }
          ss << '\n';
          SHOW_PROP("0x" << std::hex << prop.vendorId, "vendor ID");
          SHOW_PROP("0x" << std::hex << prop.deviceId, "device ID");
          SHOW_PROP(prop.coreClockRate << " MHz", "clock rate");
          SHOW_PROP(prop.maxMemAllocSize/(1024*1024.0) << " MiB", "maxMemAllocSize");
          SHOW_PROP(prop.maxHardwareContexts, "maxHardwareContexts");
          SHOW_PROP(ze_device::uuid(id), "UUID");
          #undef SHOW_PROP
       }
       ze_device_memory_properties_t mprop { ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES };
       uint32_t memcount = 0;
       if ( zeDeviceGetMemoryProperties(zeDevice, &memcount, nullptr) == ZE_RESULT_SUCCESS &&
            memcount > 0 ) {
         std::vector<ze_device_memory_properties_t> mprop(memcount);
         if ( zeDeviceGetMemoryProperties(zeDevice, &memcount, mprop.data()) == ZE_RESULT_SUCCESS ) {
           int ord = 0;
           for (auto &mp : mprop) {
             ss << "    device Memory " << ord++ << ":\n";
             ss << "        name: " << mp.name << '\n';
             ss << "        size: " << mp.totalSize/(1024*1024.0) << " MiB\n";
           }
         }
       }
      uint32_t numCmdQueueGroups = 0;
      if (zeDeviceGetCommandQueueGroupProperties(zeDevice, &numCmdQueueGroups, nullptr) == ZE_RESULT_SUCCESS) {
        std::vector<ze_command_queue_group_properties_t> queueGroupProperties(numCmdQueueGroups);
        if (zeDeviceGetCommandQueueGroupProperties(zeDevice, &numCmdQueueGroups, 
                                                   queueGroupProperties.data()) == ZE_RESULT_SUCCESS) {
          int ord = 0;
          for (auto &groupProp : queueGroupProperties) {
             ss << "    device Command Queue Group " << ord++ << ":\n";
             ss << "      numQueues (physical): " << groupProp.numQueues << '\n';
             std::string flags;
             #define GROUP_FLAG(type) \
               if (groupProp.flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_##type) \
                 flags += std::string(flags.size() ? ", " : "") + #type
             GROUP_FLAG(COMPUTE);
             GROUP_FLAG(COPY);
             GROUP_FLAG(COOPERATIVE_KERNELS);
             GROUP_FLAG(METRICS);
             #undef GROUP_FLAG
             ss << "      flags: " << flags << '\n';
          }
        }
      }
    });
  }

  for (auto s : 
       { "ZE_AFFINITY_MASK", "ZE_ENABLE_PCI_ID_DEVICE_ORDER" }) {
    const char *header = "Environment settings:\n";
    const char *v = std::getenv(s); // deliberately avoid os_env here to get local process env
    if (v) {
      if (header) { ss << header; header = nullptr; }
      ss << "  " << s << "=" << v << '\n';
    }
  }

  return ss.str();
#else
  return "ZE support is disabled in this UPC++ install.";
#endif
}

#if UPCXXI_ZE_ENABLED
GASNETT_COLD
void ze::ze_failed(ze_result_t res, const char *file, int line, const char *expr, bool report_verbose) {
  const char *errname=nullptr, *errstr=nullptr;

  switch (res) {
    #define UPCXXI_ZE_RESULT(tok,desc) \
      case tok: errname=#tok; errstr = desc; break;
    UPCXXI_ZE_RESULT(ZE_RESULT_SUCCESS, "[Core] success")
    UPCXXI_ZE_RESULT(ZE_RESULT_NOT_READY, "[Core] synchronization primitive not signaled")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_DEVICE_LOST, "[Core] device hung, reset, was removed, or driver update occurred")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY, "[Core] insufficient host memory to satisfy call")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY, "[Core] insufficient device memory to satisfy call")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_MODULE_BUILD_FAILURE, "[Core] error occurred when building module, see build log for details")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_MODULE_LINK_FAILURE, "[Core] error occurred when linking modules, see build log for details")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_DEVICE_REQUIRES_RESET, "[Core] device requires a reset")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_DEVICE_IN_LOW_POWER_STATE, "[Core] device currently in low power state")
    UPCXXI_ZE_RESULT(ZE_RESULT_EXP_ERROR_DEVICE_IS_NOT_VERTEX, "[Core, Expoerimental] device is not represented by a fabric vertex")
    UPCXXI_ZE_RESULT(ZE_RESULT_EXP_ERROR_VERTEX_IS_NOT_DEVICE, "[Core, Experimental] fabric vertex does not represent a device")
    UPCXXI_ZE_RESULT(ZE_RESULT_EXP_ERROR_REMOTE_DEVICE, "[Core, Expoerimental] fabric vertex represents a remote device or subdevice")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS, "[Sysman] access denied due to permission level")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_NOT_AVAILABLE, "[Sysman] resource already in use and simultaneous access not allowed or resource was removed")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE, "[Tools] external required dependency is unavailable or missing")
    UPCXXI_ZE_RESULT(ZE_RESULT_WARNING_DROPPED_DATA, "[Tools] data may have been dropped")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNINITIALIZED, "[Validation] driver is not initialized")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_VERSION, "[Validation] generic error code for unsupported versions")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, "[Validation] generic error code for unsupported features")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_ARGUMENT, "[Validation] generic error code for invalid arguments")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_NULL_HANDLE, "[Validation] handle argument is not valid")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE, "[Validation] object pointed to by handle still in-use by device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_NULL_POINTER, "[Validation] pointer argument may not be nullptr")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_SIZE, "[Validation] size argument is invalid (e.g., must not be zero)")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_SIZE, "[Validation] size argument is not supported by the device (e.g., too large)")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_ALIGNMENT, "[Validation] alignment argument is not supported by the device (e.g., too small)")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_SYNCHRONIZATION_OBJECT, "[Validation] synchronization object in invalid state")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_ENUMERATION, "[Validation] enumerator argument is not valid")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_ENUMERATION, "[Validation] enumerator argument is not supported by the device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT, "[Validation] image format is not supported by the device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_NATIVE_BINARY, "[Validation] native binary is not supported by the device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_GLOBAL_NAME, "[Validation] global variable is not found in the module")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_KERNEL_NAME, "[Validation] kernel name is not found in the module")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_FUNCTION_NAME, "[Validation] function name is not found in the module")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION, "[Validation] group size dimension is not valid for the kernel or device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_GLOBAL_WIDTH_DIMENSION, "[Validation] global width dimension is not valid for the kernel or device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_INDEX, "[Validation] kernel argument index is not valid for kernel")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_SIZE, "[Validation] kernel argument size does not match kernel")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_KERNEL_ATTRIBUTE_VALUE, "[Validation] value of kernel attribute is not valid for the kernel or device")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_MODULE_UNLINKED, "[Validation] module with imports needs to be linked before kernels can be created from it.")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_INVALID_COMMAND_LIST_TYPE, "[Validation] command list type does not match command queue type")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_OVERLAPPING_REGIONS, "[Validation] copy operations do not support overlapping regions of memory")
    UPCXXI_ZE_RESULT(ZE_RESULT_WARNING_ACTION_REQUIRED, "[Sysman] an action is required to complete the desired operation")
    UPCXXI_ZE_RESULT(ZE_RESULT_ERROR_UNKNOWN, "[Core] unknown or internal error")
    UPCXXI_ZE_RESULT(ZE_RESULT_FORCE_UINT32, nullptr)
    #undef UPCXXI_ZE_RESULT
  }
  
  std::stringstream ss;
  ss << expr << "\n";
  if (errname)
    ss << "  error=" << errname;
  else
    ss << "  error=UNRECOGNIZED(0x" << std::hex << std::uint64_t(res) << ")";
  if (errstr) ss << ": "<<errstr;

  // TODO: consider also interrogating zeContextGetStatus(hContext)

  if (report_verbose) {
    ss << "\n\nZE info:\n" << ze_device::kind_info();
  }
  
  detail::fatal_error(ss.str(), "ZE call failed", nullptr, file, line);
}

GASNETT_HOT
extern void detail::ze_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s,
                                           std::size_t size, backend::device_cb *cb) {
  UPCXX_ASSERT(buf_d && buf_s && cb);
  const bool host_d = heap_d < 1;
  const bool host_s = heap_s < 1;
  UPCXX_ASSERT(!host_d || !host_s);

  int heap_main = !host_d ? heap_d : heap_s;
  UPCXX_ASSERT(heap_main > 0);
  ze_heap_state *st = ze_heap_state::get(heap_main);

  ze_command_list_handle_t hCommandList;
  ze_fence_handle_t hFence;
  st->lock.lock();
  if (!st->cmdFreeList.empty()) { // reuse when possible to avoid high construction overheads
    std::tie(hCommandList,hFence) = st->cmdFreeList.top();
    st->cmdFreeList.pop();
    st->lock.unlock();
  } else {
    st->lock.unlock();

    // Create a command list
    ze_command_list_desc_t commandListDesc = { ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC };
    commandListDesc.commandQueueGroupOrdinal = st->cmdQueueGroup;
    // ZETODO: should we set either of these "go fast" flags?
    // commandListDesc.flags |= ZE_COMMAND_LIST_FLAG_RELAXED_ORDERING; 
    // commandListDesc.flags |= ZE_COMMAND_LIST_FLAG_MAXIMIZE_THROUGHPUT; 
    UPCXXI_ZE_CHECK( 
      zeCommandListCreate(st->zeContext, st->zeDevice, &commandListDesc, &hCommandList));

    // Create fence
    ze_fence_desc_t fenceDesc = { ZE_STRUCTURE_TYPE_FENCE_DESC };
    UPCXXI_ZE_CHECK(
      zeFenceCreate(st->zeCmdQueue, &fenceDesc, &hFence)); 
  }

  UPCXXI_ZE_CHECK( // Add memory copy to the list
    zeCommandListAppendMemoryCopy(hCommandList, buf_d, buf_s, size, nullptr, 0, nullptr));

  UPCXXI_ZE_CHECK( // finished appending commands
    zeCommandListClose(hCommandList));

  UPCXXI_ZE_CHECK( // Submit the command list to the device command queue
    zeCommandQueueExecuteCommandLists(st->zeCmdQueue, 1, &hCommandList, hFence));

  static_assert(sizeof(ze_command_list_handle_t) == sizeof(void*),"oops");
  static_assert(sizeof(ze_fence_handle_t) == sizeof(void*),"oops");
  cb->event = (void*)(hFence);
  cb->extra = (void*)(hCommandList);
  cb->hs = st;

  persona *per = detail::the_persona_tls.get_top_persona();
  per->UPCXXI_INTERNAL_ONLY(device_state_).ze.cbs.enqueue(cb);
}
#endif

int ze_device::device_n() {
  UPCXXI_ASSERT_INIT();
  #if UPCXXI_ZE_ENABLED
    static int dev_n = [](){ // first call
      int result = 0;
      enumerate_ze_devices(
        [&](id_type id, ...) {
          UPCXX_ASSERT(result == id);
          result++;
        });
      return result;
    }();
    return dev_n;
  #else
    return 0;
  #endif
}

device_handle_t ze_device::device_id_to_device_handle(id_type device_id) {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT(device_id >= 0 && device_id < ze_device::device_n());
  #if UPCXXI_ZE_ENABLED
     return enumerate_ze_devices(
        [=](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
          if (id == device_id) return zeDevice;
          else return ze_device_handle_t{};
        });
  #else
    return nullptr; // unreachable
  #endif
}

driver_handle_t ze_device::device_id_to_driver_handle(id_type device_id) {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT(device_id >= 0 && device_id < ze_device::device_n());
  #if UPCXXI_ZE_ENABLED
     return enumerate_ze_devices(
        [=](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
          if (id == device_id) return zeDriver;
          else return ze_driver_handle_t{};
        });
  #else
    return nullptr; // unreachable
  #endif
}

id_type ze_device::device_handle_to_device_id(device_handle_t device_handle) {
  UPCXXI_ASSERT_INIT();
  #if UPCXXI_ZE_ENABLED
    id_type result = invalid_device_id;
    enumerate_ze_devices(
        [&](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
          if (device_handle == zeDevice) result = id;
        });    
    return result;
  #else
    return invalid_device_id;
  #endif
}

namespace {
  detail::par_mutex driver_to_context_lock;
  std::unordered_map<driver_handle_t, context_handle_t> driver_to_context_map;
}

GASNETT_COLD
void ze_device::set_driver_context(context_handle_t context_handle, driver_handle_t driver_handle) {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT_ALWAYS(context_handle);
  UPCXX_ASSERT_ALWAYS(driver_handle);
  #if UPCXXI_ZE_ENABLED
    std::lock_guard<detail::par_mutex> g(driver_to_context_lock);
    auto it = driver_to_context_map.find(driver_handle);
    if (it == driver_to_context_map.end()) { // initial insertion
      driver_to_context_map[driver_handle] = context_handle;
    } else if (it->second != context_handle) {
      UPCXXI_FATAL_ERROR(
        "ze_device::set_driver_context() attempted to set a new ZE context for a ZE driver whose context was already established");
    }
  #endif
}

GASNETT_COLD
void ze_device::set_driver_context(context_handle_t context_handle, device_handle_t device_handle) {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT_ALWAYS(context_handle);
  #if UPCXXI_ZE_ENABLED
    if (!device_handle) device_handle = device_id_to_device_handle(0);
    driver_handle_t driver_handle = enumerate_ze_devices(
        [=](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
          if (zeDevice == device_handle) return zeDriver;
          else return ze_driver_handle_t{};
        });
    UPCXX_ASSERT_ALWAYS(driver_handle, "device handle not found");
    return ze_device::set_driver_context(context_handle, driver_handle);
  #endif
}

GASNETT_COLD
context_handle_t ze_device::get_driver_context(driver_handle_t driver_handle) {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT_ALWAYS(driver_handle);
  #if UPCXXI_ZE_ENABLED
    auto it = driver_to_context_map.find(driver_handle);
    if (it != driver_to_context_map.end()) { // return existing context
      return it->second;
    } else { // first time we've seen this driver, need to create context now
      ze_context_handle_t zeContext;
      ze_context_desc_t cDesc = { ZE_STRUCTURE_TYPE_CONTEXT_DESC };
      UPCXXI_ZE_CHECK_ALWAYS_VERBOSE( 
        zeContextCreate(driver_handle, &cDesc, &zeContext) );

      driver_to_context_map[driver_handle] = zeContext; // insert
      return zeContext;
    }
  #else
    return nullptr;
  #endif
}

GASNETT_COLD
context_handle_t ze_device::get_driver_context(device_handle_t device_handle) {
  UPCXXI_ASSERT_INIT();
  #if UPCXXI_ZE_ENABLED
    if (!device_handle) device_handle = device_id_to_device_handle(0);
    driver_handle_t driver_handle = enumerate_ze_devices(
        [=](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) {
          if (zeDevice == device_handle) return zeDriver;
          else return ze_driver_handle_t{};
        });
    UPCXX_ASSERT_ALWAYS(driver_handle, "device handle not found");
    return ze_device::get_driver_context(driver_handle);
  #else
    return nullptr;
  #endif
}

GASNETT_COLD
ze_device::ze_device(id_type device_id):
  gpu_device(detail::internal_only(), device_id, memory_kind::ze_device) {

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);

  #if UPCXXI_ZE_ENABLED
    if (device_id != invalid_device_id) {
      heap_idx_ = backend::heap_state::alloc_index(use_gex_mk(detail::internal_only()));

      auto st = enumerate_ze_devices(
        [device_id,this](id_type id, ze_device_handle_t zeDevice, ze_driver_handle_t zeDriver) -> ze_heap_state* {
          if (id == device_id) {
            auto st = new ze_heap_state{};
            st->device_base = this;
            st->zeDriver   = zeDriver;
            st->zeDevice   = zeDevice;
            st->device_id = device_id;
            return st;
          } else return nullptr;
        });

      if (!st) {
        UPCXXI_FATAL_ERROR("Invalid ZE device ID: " << device_id <<
                           "\n\nZE info:\n" << ze_device::kind_info());
      } 
      st->zeContext = get_driver_context(st->zeDriver);

      uint32_t numCmdQueueGroups = 0;
      UPCXXI_ZE_CHECK_ALWAYS_VERBOSE(
        zeDeviceGetCommandQueueGroupProperties(st->zeDevice, &numCmdQueueGroups, nullptr));
      UPCXX_ASSERT_ALWAYS(numCmdQueueGroups > 0);
      std::vector<ze_command_queue_group_properties_t> queueGroupProperties(numCmdQueueGroups);
      UPCXXI_ZE_CHECK_ALWAYS_VERBOSE(
        zeDeviceGetCommandQueueGroupProperties(st->zeDevice, &numCmdQueueGroups, queueGroupProperties.data()));

      // Choose the best command queue group
      // Currently favor the highest-numbered queue group with COPY capability and no COMPUTE,
      // to help ensure our copy operations proceed asynchronously wrt compute kernels whenever possible.
      // ZETODO: Do we need a smarter algorithm or more configurability here?
      { int copy_only_ordinal = -1;
        int copy_compute_ordinal = -1;
        for (size_t ord = 0; ord < numCmdQueueGroups; ord++) {
          ze_command_queue_flags_t flags = queueGroupProperties[ord].flags;
          if (flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY) {
            if (flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) {
              copy_compute_ordinal = ord;
            } else {
              copy_only_ordinal = ord;
            }
          }
        }
        if      (copy_only_ordinal >= 0)    st->cmdQueueGroup = copy_only_ordinal;
        else if (copy_compute_ordinal >= 0) st->cmdQueueGroup = copy_compute_ordinal;
        else UPCXXI_FATAL_ERROR("ZE device ID: " << device_id << " lacks any CmdQueue Groups with COPY capability");
      }
      ze_command_queue_desc_t cmdQueueDesc = {ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
      cmdQueueDesc.ordinal = st->cmdQueueGroup;
      cmdQueueDesc.index = 0;
      cmdQueueDesc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;

      // Create command queue for this ze_device
      UPCXXI_ZE_CHECK_ALWAYS_VERBOSE(
        zeCommandQueueCreate(st->zeContext, st->zeDevice, &cmdQueueDesc, &st->zeCmdQueue));

      #if UPCXXI_GEX_MK_ZE
      { // construct GASNet-level memory kind and endpoint
        std::string where = std::string("ZE device ") + std::to_string(device_id);
        gex_MK_Create_args_t args;
        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_ZE;
        args.gex_args.gex_class_ze.gex_zeContext = st->zeContext;
        args.gex_args.gex_class_ze.gex_zeDevice = st->zeDevice;
        args.gex_args.gex_class_ze.gex_zeMemoryOrdinal = 0;
        st->create_endpoint(args, heap_idx_, where);
      }
      #endif
      
      backend::heap_state::get(heap_idx_,true) = st;
    }
  #else
    UPCXX_ASSERT_ALWAYS(device_id == invalid_device_id);
  #endif
}

GASNETT_COLD
void ze_device::destroy(upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);

  backend::quiesce(upcxx::world(), eb);

  if (!is_active()) return;

  #if UPCXXI_ZE_ENABLED
    ze_heap_state *st = ze_heap_state::get(heap_idx_);
    UPCXX_ASSERT(st->device_base == this);
    UPCXX_ASSERT(st->device_id == device_id_);

    // specification preconditions imply this *should* be unnecessary 
    // for a properly synchronized program, but it doesn't hurt to over-synchronize
    // before we start tearing down
    UPCXXI_ZE_CHECK_ALWAYS(
      zeCommandQueueSynchronize(st->zeCmdQueue,std::numeric_limits<uint64_t>::max()));

    #if UPCXXI_GEX_MK_ZE
      st->destroy_endpoint("ze_device");
    #endif
    
    if (st->alloc_base) {
      auto alloc = static_cast<detail::device_allocator_core<ze_device>*>(st->alloc_base);
      alloc->release();
      UPCXX_ASSERT(!st->alloc_base);
    }

    { std::lock_guard<detail::par_mutex> g(st->lock);
      while (!st->cmdFreeList.empty()) { // drain the free list
        ze_command_list_handle_t hCommandList;
        ze_fence_handle_t hFence;
        std::tie(hCommandList,hFence) = st->cmdFreeList.top();
        st->cmdFreeList.pop();

        UPCXXI_ZE_CHECK_ALWAYS(
          zeCommandListDestroy(hCommandList));

        UPCXXI_ZE_CHECK_ALWAYS(
          zeFenceDestroy(hFence));
      }
    }

    UPCXXI_ZE_CHECK_ALWAYS(
      zeCommandQueueDestroy(st->zeCmdQueue));

    backend::heap_state::get(heap_idx_) = nullptr;
    backend::heap_state::free_index(heap_idx_);
    delete st;
  #endif
  
  device_id_ = invalid_device_id; // deactivate
  heap_idx_ = -1;
}

// collective constructor with a (possibly inactive) device
GASNETT_COLD
detail::device_allocator_core<ze_device>::device_allocator_core(
    ze_device &dev, void *base, size_t size)
#if UPCXXI_ZE_ENABLED
    :detail::device_allocator_base(dev.heap_idx_,
                                   make_segment(dev.heap_idx_, base, size)) { }
#else 
    { UPCXX_ASSERT(!dev.is_active()); }
#endif

GASNETT_COLD
void detail::device_allocator_core<ze_device>::release() {
  if (!is_active()) return;

  #if UPCXXI_ZE_ENABLED  
      ze_heap_state *st = ze_heap_state::get(heap_idx_);
     
      if(st->segment_to_free) {
        UPCXXI_ZE_CHECK_ALWAYS(zeMemFree(st->zeContext, st->segment_to_free));
        st->segment_to_free = nullptr;
      }
      
      st->alloc_base = nullptr; // deregister
  #endif

  heap_idx_ = -1; // deactivate
}

template
ze_device::id_type detail::device::heap_idx_to_device_id<ze_device>(int);

