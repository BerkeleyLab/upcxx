#include <upcxx/hip.hpp>
#include <upcxx/hip_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace detail = upcxx::detail;
using upcxx::hip_device;
using upcxx::gpu_device;

using std::size_t;
using std::uint64_t;

#if UPCXXI_HIP_ENABLED
using upcxx::backend::hip_heap_state;
namespace hip = detail::hip;

namespace {

  hipError_t hip_init(bool errors_return = false) {
    static hipError_t res = []() { // first call
      return hipInit(0);
    }();
    if_pf (res != hipSuccess && !errors_return) {
      hip::hip_failed(res, __FILE__, __LINE__, "hipInit(0)", true);
    }
    return res;
  }

  GASNETT_COLD
  detail::segment_allocator make_segment(int heap_idx, void *base, size_t size) {
    hip_heap_state *st = heap_idx <= 0 ? nullptr : hip_heap_state::get(heap_idx);
    auto dev_alloc = [st](size_t sz) {
      auto with = hip::context<1>(st->device_id);
      void *p = nullptr;
      hipError_t r = hipMalloc(&p, sz);
      switch(r) {
        case hipSuccess: break;
        case hipErrorOutOfMemory: break; // oom returns null
        default: // other unknown errors are immediately fatal:
          std::string s("Requested hip allocation failed: size=");
          s += std::to_string(sz);
          hip::hip_failed(r, __FILE__, __LINE__, s.c_str());
      }
      return p;
    };
    auto dev_free = [st](void *p) {
      auto with = hip::context<1>(st->device_id);
      UPCXXI_HIP_CHECK_ALWAYS(hipFree(p));
    };
    std::string where("device_allocator<hip_device> constructor for ");
    if (st) where += "HIP device " + std::to_string(st->device_id);
    else    where += "inactive hip_device";
    return hip_heap_state::make_gpu_segment(st, heap_idx, base, size, 
                                             where.c_str(), dev_alloc, dev_free);
  } // make_segment

} // anon namespace
#endif

GASNETT_COLD
std::string hip_device::uuid(id_type device_id) {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_HIP_ENABLED
  int dev_n = hip_device::device_n(); // handles hip_init
  UPCXX_ASSERT_ALWAYS(device_id >= 0 && device_id < dev_n);
  #if HIP_VERSION_MAJOR > 5 || (HIP_VERSION_MAJOR == 5 && HIP_VERSION_MINOR >= 2)
    union {
      hipUUID uuid;
      std::uint8_t bytes[16];
    } u;
    if (hipDeviceGetUuid(&u.uuid, device_id) == hipSuccess) {
      std::stringstream ss;
      ss << "GPU-";
      #if __HIP_PLATFORM_NVIDIA__  
        // NVIDIA GPss sse an 8-4-4-4-12 binary UUID
        // e.g. see: nvidia-smi -L
        int i = 0;
        for (auto v : u.bytes) {
          if (i == 4 || i == 6 || i == 8 || i == 10) ss << "-";
          ss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)v;
          i++;
        }
      #else
        // AMD GPUs use an 8-byte textual UUID
        // e.g. see: rocm-smi --showuniqueid
        for (char v : u.bytes) {
          if (std::isprint(v)) ss << v;
          else ss << '?';
        }
      #endif
      return ss.str();
    }
  #endif
  return "Unsupported query";
#else
  return "HIP support is disabled in this UPC++ install.";
#endif
}

GASNETT_COLD
std::string hip_device::kind_info() {
  UPCXXI_ASSERT_INIT();
#if UPCXXI_HIP_ENABLED
  std::stringstream ss;

  hipError_t res = hip_init(true);
  if (res != hipSuccess) {
    ss << "hipInit() failed";
    const char *errname = hipGetErrorName(res);
    if (errname) ss << ": " << errname;
    ss << "\n";
  }

  int version = -1;
  if ( hipDriverGetVersion(&version) == hipSuccess && version >= 0) {
    ss << "HIP Driver version: " << version << "\n";
  }
  version = -1;
  if ( hipRuntimeGetVersion(&version) == hipSuccess && version >= 0) {
    ss << "HIP Runtime version: " << version << "\n";
  }

  int dev_n = -1;
  if (res == hipErrorNoDevice ||
      hipGetDeviceCount(&dev_n) == hipErrorNoDevice) dev_n = 0;
  if ( dev_n >= 0) {
    ss << "Found " << dev_n << " HIP devices:\n";
    for (int d = 0; d < dev_n; d++) {
      hipDeviceProp_t prop = {};
      ss << "  " << d << ": ";
      if (hipGetDeviceProperties(&prop, d) == hipSuccess) {
        if (prop.name) ss << prop.name;
        if (prop.totalGlobalMem) {
          ss << "\n    Total global memory: " << prop.totalGlobalMem/(1024*1024.0) << " MiB";
        }
        if (prop.major || prop.minor) {
          ss << "\n    Compute capability equivalent: " << prop.major << "." << prop.minor;
        }
      }
      ss << "\n    UUID: " << hip_device::uuid(d);
      ss << '\n';
    }
  }
  for (auto s : 
       { "ROCR_VISIBLE_DEVICES", "HIP_VISIBLE_DEVICES",
         "CUDA_VISIBLE_DEVICES", "CUDA_DEVICE_ORDER", "NVIDIA_VISIBLE_DEVICES" }) {
    const char *header = "Environment settings:\n";
    const char *v = std::getenv(s); // deliberately avoid os_env here to get local process env
    if (v) {
      if (header) { ss << header; header = nullptr; }
      ss << "  " << s << "=" << v << '\n';
    }
  }

  return ss.str();
#else
  return "HIP support is disabled in this UPC++ install.";
#endif
}

#if UPCXXI_HIP_ENABLED
GASNETT_COLD
void hip::hip_failed(hipError_t res, const char *file, int line, const char *expr, bool report_verbose) {
  const char *errname = hipGetErrorName(res);
  const char *errstr  = hipGetErrorString(res);
  
  std::stringstream ss;
  ss << expr <<"\n  error=" << (errname?errname:"unknown") << "(" << (int)res << ")"
             << ": " << (errstr?errstr:"unknown");

  if (report_verbose) {
    ss << "\n\nHIP info:\n" << hip_device::kind_info();
  }
  
  detail::fatal_error(ss.str(), "HIP call failed", nullptr, file, line);
}

GASNETT_HOT
extern void detail::hip_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s_,
                                           std::size_t size, backend::device_cb *cb) {
  void *buf_s = const_cast<void *>(buf_s_);
  UPCXX_ASSERT(buf_d && buf_s && cb);
  const bool host_d = heap_d < 1;
  const bool host_s = heap_s < 1;
  UPCXX_ASSERT(!host_d || !host_s);

  // hipMemcpy documentation recommends using the source device as the primary device
  // for cross-device peer-to-peer transfers.
  // HOWEVER: doing so leads to data validation failures in test/copy-cover
  // when the dev-to-dev transfer is surrounded by ROCmRDMA xfers, indicating a consistency problem.
  // This was observed using ROCm/4.5.0 on Spock (Cray EX SS-10) with ucx-conduit
  // and also ROCm/4.5.2 on JLSE MI100 (AMD EPYC 7543) with ucx and ibv conduits.
  // TODO: Figure out what's actually going on here and why the vendor recommendation doesn't work.
  constexpr bool favor_source = false;
  int heap_main = ( favor_source ? ( !host_s ? heap_s : heap_d )
                                 : ( !host_d ? heap_d : heap_s ) );
  UPCXX_ASSERT(heap_main > 0);
  hip_heap_state *st = hip_heap_state::get(heap_main);

  auto with = hip::context<0>(st->device_id);

  if(!host_d && !host_s) {
    // device to device
    hip_heap_state *st_d = hip_heap_state::get(heap_d);
    hip_heap_state *st_s = hip_heap_state::get(heap_s);

    #if UPCXXI_ASSERT_ENABLED && !UPCXXI_HIP_SKIP_PEER_ACCESS_CHECK
      // HIP docs claim that peer access (memory cross-mapping between devices)
      // is optional but important for performance. 
      // It appears to be enabled by default, but let's complain if someone turns it off...
      hip_heap_state *st_other = ( favor_source ? st_d : st_s );
      if (st->device_id != st_other->device_id) {
        int peerAccessEnabled = -1;
        UPCXXI_HIP_CHECK(hipDeviceCanAccessPeer(&peerAccessEnabled, st->device_id, st_other->device_id));
        UPCXX_ASSERT(peerAccessEnabled == 1);
      }
    #endif

  #if 1
    UPCXXI_HIP_CHECK(hipMemcpyPeerAsync(
      buf_d, st_d->device_id,
      buf_s, st_s->device_id,
      size, st->stream
    ));
  #else
    UPCXXI_HIP_CHECK(hipMemcpyDtoDAsync(
      reinterpret_cast<hipDeviceptr_t>(buf_d),
      reinterpret_cast<hipDeviceptr_t>(buf_s),
      size, st->stream
    ));
  #endif
  }
  else if(!host_d) {
    // host to device
    UPCXXI_HIP_CHECK(hipMemcpyHtoDAsync(reinterpret_cast<hipDeviceptr_t>(buf_d), buf_s, size, st->stream));
  }
  else {
    UPCXX_ASSERT(!host_s);
    // device to host
    UPCXXI_HIP_CHECK(hipMemcpyDtoHAsync(buf_d, reinterpret_cast<hipDeviceptr_t>(buf_s), size, st->stream));
  }

  hipEvent_t event;
  st->lock.lock();
  if (!st->eventFreeList.empty()) { // reuse when possible to avoid high construction overheads
    event = st->eventFreeList.top();
    st->eventFreeList.pop();
    st->lock.unlock();
  } else {
    st->lock.unlock();

    // Create an event object
    UPCXXI_HIP_CHECK(hipEventCreateWithFlags(&event, hipEventDisableTiming));
  }
  UPCXXI_HIP_CHECK(hipEventRecord(event, st->stream));
  cb->event = (void*)event;
  cb->hs = st;

  persona *per = detail::the_persona_tls.get_top_persona();
  per->UPCXXI_INTERNAL_ONLY(device_state_).hip.cbs.enqueue(cb);
}
#endif

int hip_device::device_n() {
  UPCXXI_ASSERT_INIT();
  #if UPCXXI_HIP_ENABLED
    int dev_n = -1;
    if (hip_init(true) == hipErrorNoDevice) {
      return 0; // HIP-over-CUDA can give this error when no devices are visible
    }
    hipError_t res = hipGetDeviceCount(&dev_n);
    if (res == hipErrorNoDevice) {
      dev_n = 0;
    } else if (res != hipSuccess) {
      UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipGetDeviceCount(&dev_n));
    }
    return dev_n;
  #else
    return 0;
  #endif
}

GASNETT_COLD
hip_device::hip_device(id_type device_id): 
  gpu_device(detail::internal_only(), device_id, memory_kind::hip_device) {

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);

  #if UPCXXI_HIP_ENABLED
    if (device_id != invalid_device_id) {
      heap_idx_ = backend::heap_state::alloc_index(use_gex_mk(detail::internal_only()));

      hip_init();
      auto with = hip::context<2>(device_id);

      hip_heap_state *st = new hip_heap_state{};
      st->device_base = this;
      st->device_id = device_id;

      #if UPCXXI_GEX_MK_HIP
      { // construct GASNet-level memory kind and endpoint
        std::string where = std::string("HIP device ") + std::to_string(device_id);
        gex_MK_Create_args_t args;
        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_HIP;
        args.gex_args.gex_class_hip.gex_hipDevice = device_id;
        st->create_endpoint(args, heap_idx_, where);
      }
      #endif
      
      UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipStreamCreateWithFlags(&st->stream, hipStreamNonBlocking));
      backend::heap_state::get(heap_idx_,true) = st;
    }
  #else
    UPCXX_ASSERT_ALWAYS(device_id == invalid_device_id);
  #endif
}

GASNETT_COLD
void hip_device::destroy(upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);

  backend::quiesce(upcxx::world(), eb);

  if (!is_active()) return;

  #if UPCXXI_HIP_ENABLED
    hip_heap_state *st = hip_heap_state::get(heap_idx_);
    UPCXX_ASSERT(st->device_base == this);
    UPCXX_ASSERT(st->device_id == device_id_);

    #if UPCXXI_GEX_MK_HIP
      st->destroy_endpoint("hip_device");
    #endif
    
    if (st->alloc_base) {
      auto alloc = static_cast<detail::device_allocator_core<hip_device>*>(st->alloc_base);
      alloc->release();
      UPCXX_ASSERT(!st->alloc_base);
    }

    { std::lock_guard<detail::par_mutex> g(st->lock);
      while (!st->eventFreeList.empty()) { // drain the free list
        hipEvent_t hEvent = st->eventFreeList.top();
        st->eventFreeList.pop();

        UPCXXI_HIP_CHECK(hipEventDestroy(hEvent));
      }
    }

    UPCXXI_HIP_CHECK_ALWAYS(hipStreamDestroy(st->stream));
    
    backend::heap_state::get(heap_idx_) = nullptr;
    backend::heap_state::free_index(heap_idx_);
    delete st;
  #endif
  
  device_id_ = invalid_device_id; // deactivate
  heap_idx_ = -1;
}

// collective constructor with a (possibly inactive) device
GASNETT_COLD
detail::device_allocator_core<hip_device>::device_allocator_core(
    hip_device &dev, void *base, size_t size)
#if UPCXXI_HIP_ENABLED
    :detail::device_allocator_base(dev.heap_idx_,
                                   make_segment(dev.heap_idx_, base, size)) { }
#else  
    { UPCXX_ASSERT(!dev.is_active()); }
#endif

GASNETT_COLD
void detail::device_allocator_core<hip_device>::release() {
  if (!is_active()) return;

  #if UPCXXI_HIP_ENABLED  
      hip_heap_state *st = hip_heap_state::get(heap_idx_);
     
      if(st->segment_to_free) {
        auto with = hip::context<1>(st->device_id);
        UPCXXI_HIP_CHECK_ALWAYS(hipFree(st->segment_to_free));
        st->segment_to_free = nullptr;
      }
      
      st->alloc_base = nullptr; // deregister
  #endif

  heap_idx_ = -1; // deactivate
}

template
hip_device::id_type detail::device::heap_idx_to_device_id<hip_device>(int);

