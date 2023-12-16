#include <upcxx/cuda.hpp>
#include <upcxx/cuda_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace detail = upcxx::detail;
using upcxx::cuda_device;
using upcxx::gpu_device;

using std::size_t;
using std::uint64_t;

#if UPCXXI_CUDA_ENABLED
using upcxx::backend::cuda_heap_state;
namespace cuda = detail::cuda;

namespace {
  CUresult cu_init(bool errors_return = false) {
    static CUresult res = []() { // first call
      int dev_n = -1;
      CUresult res = cuDeviceGetCount(&dev_n);
      if (res == CUDA_ERROR_NOT_INITIALIZED) {
        return cuInit(0);
      } else return res;
    }();
    if_pf (res != CUDA_SUCCESS && !errors_return) {
      cuda::cu_failed(res, __FILE__, __LINE__, "cuInit(0)", true);
    }
    return res;
  }

  GASNETT_COLD
  detail::segment_allocator make_segment(int heap_idx, void *base, size_t size) {
    cuda_heap_state *st = heap_idx <= 0 ? nullptr : cuda_heap_state::get(heap_idx);
    auto dev_alloc = [st](size_t sz) {
      auto with = cuda::context<1>(st->context);
      CUdeviceptr p = 0;
      CUresult r = cuMemAlloc(&p, sz);
      switch(r) {
        case CUDA_SUCCESS: break;
        case CUDA_ERROR_OUT_OF_MEMORY: break; // oom returns null
        default: // other unknown errors are immediately fatal:
          std::string s("Requested cuda allocation failed: size=");
          s += std::to_string(sz);
          cuda::cu_failed(r, __FILE__, __LINE__, s.c_str());
      }
      return reinterpret_cast<void*>(p);
    };
    auto dev_free = [st](void *p) {
      auto with = cuda::context<1>(st->context);
      UPCXXI_CU_CHECK_ALWAYS(cuMemFree(reinterpret_cast<CUdeviceptr>(p)));
    };
    std::string where("device_allocator<cuda_device> constructor for ");
    if (st) where += "CUDA device " + std::to_string(st->device_id);
    else    where += "inactive cuda_device";
    return cuda_heap_state::make_gpu_segment(st, heap_idx, base, size, 
                                             where.c_str(), dev_alloc, dev_free);
  } // make_segment

} // anon namespace
#endif

GASNETT_COLD
std::string cuda_device::uuid(id_type device_id) {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_CUDA_ENABLED
  int dev_n = cuda_device::device_n(); // handles cu_init
  UPCXX_ASSERT_ALWAYS(device_id >= 0 && device_id < dev_n);
  #if CUDA_VERSION >= 9020
    union {
      CUuuid uuid;
      std::uint8_t bytes[16];
    } u;
    #if CUDA_VERSION >= 11040
      if (cuDeviceGetUuid_v2(&u.uuid, device_id) == CUDA_SUCCESS)
    #else
      if (cuDeviceGetUuid(&u.uuid, device_id) == CUDA_SUCCESS) 
    #endif
      {
        // NVIDIA GPUs use an 8-4-4-4-12 binary UUID
        // e.g. see: nvidia-smi -L
        std::stringstream ss;
        ss << "GPU-";
        int i = 0;
        for (auto v : u.bytes) {
          if (i == 4 || i == 6 || i == 8 || i == 10) ss << "-";
          ss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)v;
          i++;
        }
        return ss.str();
      }
  #endif
  return "Unsupported query";
#else
  return "CUDA support is disabled in this UPC++ install.";
#endif
}

GASNETT_COLD
std::string cuda_device::kind_info() {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_CUDA_ENABLED
  std::stringstream ss;

  CUresult res = cu_init(true);
  if (res != CUDA_SUCCESS) {
    ss << "cuInit() failed";
    const char *errname = nullptr;
    cuGetErrorName(res, &errname);
    if (errname) ss << ": " << errname;
    ss << "\n";
  }

  int version = -1;
  if ( cuDriverGetVersion(&version) == CUDA_SUCCESS && version >= 0) {
    ss << "CUDA Driver version: " << version/1000 << "." << (version%1000)/10 << '\n';
  }

  int dev_n = -1;
  if (res == CUDA_ERROR_NO_DEVICE || 
      cuDeviceGetCount(&dev_n) == CUDA_ERROR_NO_DEVICE) dev_n = 0;
  if (dev_n >= 0) {
    ss << "Found " << dev_n << " CUDA devices:\n";
    for (int d = 0; d < dev_n; d++) {
      char name[255];
      size_t mem = 0;
      ss << "  " << d << ": ";
      if (cuDeviceGetName(name, sizeof(name)-1, d) == CUDA_SUCCESS && *name) {
        name[sizeof(name)-1] = '\0';
        ss << name;
      }
      if (cuDeviceTotalMem(&mem, d) == CUDA_SUCCESS && mem > 0) {
        ss << "\n    Total memory: " << mem/(1024*1024.0) << " MiB";
      }
      ss << "\n    UUID: " << cuda_device::uuid(d);
      ss << '\n';
    }
  }
  for (auto s : 
       { "CUDA_VISIBLE_DEVICES", "CUDA_DEVICE_ORDER", "NVIDIA_VISIBLE_DEVICES" }) {
    const char *header = "Environment settings:\n";
    const char *v = std::getenv(s); // deliberately avoid os_env here to get local process env
    if (v) {
      if (header) { ss << header; header = nullptr; }
      ss << "  " << s << "=" << v << '\n';
    }
  }

  return ss.str();
#else
  return "CUDA support is disabled in this UPC++ install.";
#endif
}

#if UPCXXI_CUDA_ENABLED
GASNETT_COLD
void cuda::cu_failed(CUresult res, const char *file, int line, const char *expr, bool report_verbose) {
  const char *errname="", *errstr="";
  cuGetErrorName(res, &errname);
  cuGetErrorString(res, &errstr);
  
  std::stringstream ss;
  ss << expr <<"\n  error="<<errname<<": "<<errstr;

  if (report_verbose) {
    ss << "\n\nCUDA info:\n" << cuda_device::kind_info();
  }
  
  detail::fatal_error(ss.str(), "CUDA call failed", nullptr, file, line);
}

GASNETT_HOT
extern void detail::cuda_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s,
                                           std::size_t size, backend::device_cb *cb) {
  UPCXX_ASSERT(buf_d && buf_s && cb);
  const bool host_d = heap_d < 1;
  const bool host_s = heap_s < 1;
  UPCXX_ASSERT(!host_d || !host_s);

  int heap_main = !host_d ? heap_d : heap_s;
  UPCXX_ASSERT(heap_main > 0);
  cuda_heap_state *st = cuda_heap_state::get(heap_main);

  auto with = cuda::context<0>(st->context);

  if(!host_d && !host_s) {
    cuda_heap_state *st_d = cuda_heap_state::get(heap_d);
    cuda_heap_state *st_s = cuda_heap_state::get(heap_s);

    // device to device
    UPCXXI_CU_CHECK(cuMemcpyPeerAsync(
      reinterpret_cast<CUdeviceptr>(buf_d), st_d->context,
      reinterpret_cast<CUdeviceptr>(buf_s), st_s->context,
      size, st->stream
    ));
  }
  else if(!host_d) {
    // host to device
    UPCXXI_CU_CHECK(cuMemcpyHtoDAsync(reinterpret_cast<CUdeviceptr>(buf_d), buf_s, size, st->stream));
  }
  else {
    UPCXX_ASSERT(!host_s);
    // device to host
    UPCXXI_CU_CHECK(cuMemcpyDtoHAsync(buf_d, reinterpret_cast<CUdeviceptr>(buf_s), size, st->stream));
  }

  CUevent event;
  st->lock.lock();
  if (!st->eventFreeList.empty()) { // reuse when possible to avoid high construction overheads
    event = st->eventFreeList.top();
    st->eventFreeList.pop();
    st->lock.unlock();
  } else {
    st->lock.unlock();

    // Create an event object
    UPCXXI_CU_CHECK(cuEventCreate(&event, CU_EVENT_DISABLE_TIMING));
  }
  UPCXXI_CU_CHECK(cuEventRecord(event, st->stream));
  cb->event = (void*)event;
  cb->hs = st;

  persona *per = detail::the_persona_tls.get_top_persona();
  per->UPCXXI_INTERNAL_ONLY(device_state_).cuda.cbs.enqueue(cb);
}
#endif

int cuda_device::device_n() {
  UPCXXI_ASSERT_INIT();
  #if UPCXXI_CUDA_ENABLED
    int dev_n = -1;
    if (cu_init(true) == CUDA_ERROR_NO_DEVICE) {
      return 0; // cuInit can give this error when no devices are visible
    }
    UPCXXI_CU_CHECK_ALWAYS_VERBOSE(cuDeviceGetCount(&dev_n));
    return dev_n;
  #else
    return 0;
  #endif
}

GASNETT_COLD
cuda_device::cuda_device(id_type device_id):
  gpu_device(detail::internal_only(), device_id, memory_kind::cuda_device) {

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);

  #if UPCXXI_CUDA_ENABLED
    if (device_id != invalid_device_id) {
      heap_idx_ = backend::heap_state::alloc_index(use_gex_mk(detail::internal_only()));

      cu_init();
      CUcontext ctx;
      CUresult res = cuDevicePrimaryCtxRetain(&ctx, device_id);
      if (res != CUDA_SUCCESS) {
        std::string callstr("cuDevicePrimaryCtxRetain() failed for device ");
        callstr += std::to_string(device_id);
        cuda::cu_failed(res, __FILE__, __LINE__, callstr.c_str(), true);
      }
      auto with = cuda::context<2>(ctx);

      cuda_heap_state *st = new cuda_heap_state{};
      st->device_base = this;
      st->context = ctx;
      st->device_id = device_id;

      #if UPCXXI_GEX_MK_CUDA
      { // construct GASNet-level memory kind and endpoint
        std::string where = std::string("CUDA device ") + std::to_string(device_id);
        gex_MK_Create_args_t args;
        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_CUDA_UVA;
        args.gex_args.gex_class_cuda_uva.gex_CUdevice = device_id;
        st->create_endpoint(args, heap_idx_, where);
      }
      #endif
      
      UPCXXI_CU_CHECK_ALWAYS_VERBOSE(cuStreamCreate(&st->stream, CU_STREAM_NON_BLOCKING));
      backend::heap_state::get(heap_idx_,true) = st;
    }
  #else
    UPCXX_ASSERT_ALWAYS(device_id == invalid_device_id);
  #endif
}

GASNETT_COLD
void cuda_device::destroy(upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);

  backend::quiesce(upcxx::world(), eb);

  if (!is_active()) return;

  #if UPCXXI_CUDA_ENABLED
    cuda_heap_state *st = cuda_heap_state::get(heap_idx_);
    UPCXX_ASSERT(st->device_base == this);
    UPCXX_ASSERT(st->device_id == device_id_);

    #if UPCXXI_GEX_MK_CUDA
      st->destroy_endpoint("cuda_device");
    #endif
    
    if (st->alloc_base) {
      auto alloc = static_cast<detail::device_allocator_core<cuda_device>*>(st->alloc_base);
      alloc->release();
      UPCXX_ASSERT(!st->alloc_base);
    }

    { std::lock_guard<detail::par_mutex> g(st->lock);
      while (!st->eventFreeList.empty()) { // drain the free list
        CUevent hEvent = st->eventFreeList.top();
        st->eventFreeList.pop();

        UPCXXI_CU_CHECK(cuEventDestroy(hEvent));
      }
    }

    UPCXXI_CU_CHECK_ALWAYS(cuStreamDestroy(st->stream));
    UPCXXI_CU_CHECK_ALWAYS(cuDevicePrimaryCtxRelease(st->device_id));
    
    backend::heap_state::get(heap_idx_) = nullptr;
    backend::heap_state::free_index(heap_idx_);
    delete st;
  #endif
  
  device_id_ = invalid_device_id; // deactivate
  heap_idx_ = -1;
}

// collective constructor with a (possibly inactive) device
GASNETT_COLD
detail::device_allocator_core<cuda_device>::device_allocator_core(
    cuda_device &dev, void *base, size_t size)
#if UPCXXI_CUDA_ENABLED
    :detail::device_allocator_base(dev.heap_idx_,
                                   make_segment(dev.heap_idx_, base, size)) { }
#else 
    { UPCXX_ASSERT(!dev.is_active()); }
#endif

GASNETT_COLD
void detail::device_allocator_core<cuda_device>::release() {
  if (!is_active()) return;

  #if UPCXXI_CUDA_ENABLED  
      cuda_heap_state *st = cuda_heap_state::get(heap_idx_);
     
      if(st->segment_to_free) {
        auto with = cuda::context<1>(st->context);
        UPCXXI_CU_CHECK_ALWAYS(cuMemFree(reinterpret_cast<CUdeviceptr>(st->segment_to_free)));
        st->segment_to_free = nullptr;
      }
      
      st->alloc_base = nullptr; // deregister
  #endif

  heap_idx_ = -1; // deactivate
}

template
cuda_device::id_type detail::device::heap_idx_to_device_id<cuda_device>(int);

