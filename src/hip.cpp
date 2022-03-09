#include <upcxx/hip.hpp>
#include <upcxx/hip_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace detail = upcxx::detail;

using std::size_t;
using std::uint64_t;

#if UPCXXI_HIP_ENABLED
using upcxx::backend::hip_heap_state;
namespace hip = upcxx::detail::hip;

namespace {
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

  detail::device_allocator_core<upcxx::hip_device> tombstone;
} // anon namespace

GASNETT_COLD
static std::string get_hip_info() {
  std::stringstream ss;

  int version = -1;
  if ( hipDriverGetVersion(&version) == hipSuccess && version >= 0) {
    ss << "HIP Driver version: " << version << "\n";
  }
  version = -1;
  if ( hipRuntimeGetVersion(&version) == hipSuccess && version >= 0) {
    ss << "HIP Runtime version: " << version << "\n";
  }

  int dev_n = -1;
  if ( hipGetDeviceCount(&dev_n) == hipErrorNoDevice) dev_n = 0;
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
}

GASNETT_COLD
void hip::hip_failed(hipError_t res, const char *file, int line, const char *expr, bool report_verbose) {
  const char *errname = hipGetErrorName(res);
  const char *errstr  = hipGetErrorString(res);
  
  std::stringstream ss;
  ss << expr <<"\n  error=" << (errname?errname:"unknown") << "(" << (int)res << ")"
             << ": " << (errstr?errstr:"unknown");

  if (report_verbose) {
    ss << "\n\nHIP info:\n" << get_hip_info();
  }
  
  upcxx::detail::fatal_error(ss.str(), "HIP call failed", nullptr, file, line);
}

GASNETT_HOT
extern void upcxx::detail::hip_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s_,
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
  UPCXXI_HIP_CHECK(hipEventCreateWithFlags(&event, hipEventDisableTiming));
  UPCXXI_HIP_CHECK(hipEventRecord(event, st->stream));
  cb->event = (void*)event;

  persona *per = detail::the_persona_tls.get_top_persona();
  per->UPCXXI_INTERNAL_ONLY(device_state_).hip.cbs.enqueue(cb);
}
#endif

int upcxx::hip_device::device_n() {
  #if UPCXXI_HIP_ENABLED
    int dev_n = -1;
    hipError_t res = hipInit(0);
    if (res == hipErrorNoDevice) {
      return 0; // HIP-over-CUDA can give this error when no devices are visible
    } else if (res != hipSuccess) {
      UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipInit(0));
    }
    res = hipGetDeviceCount(&dev_n);
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
upcxx::hip_device::hip_device(int device):
  device_(device), heap_idx_(-1) {

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);

  #if UPCXXI_HIP_ENABLED
    if (device != invalid_device_id) {
      heap_idx_ = backend::heap_state::alloc_index(use_gex_mk(detail::internal_only()));

      UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipInit(0));
      auto with = hip::context<2>(device);

      hip_heap_state *st = new hip_heap_state{};
      st->device_id = device;

      #if UPCXXI_GEX_MK_HIP
      { // construct GASNet-level memory kind and endpoint
        std::string where = std::string("HIP device ") + std::to_string(device);
        gex_MK_Create_args_t args;
        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_HIP;
        args.gex_args.gex_class_hip.gex_hipDevice = device;
        st->create_endpoint(args, heap_idx_, where.c_str());
      }
      #endif
      
      UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipStreamCreateWithFlags(&st->stream, hipStreamNonBlocking));
      backend::heap_state::get(heap_idx_,true) = st;
    }
  #else
    UPCXX_ASSERT_ALWAYS(device == invalid_device_id);
  #endif
}

GASNETT_COLD
upcxx::hip_device::~hip_device() {
  if(backend::init_count > 0) { // we don't assert on leaks after finalization
    UPCXX_ASSERT_ALWAYS(!is_active(), "An active upcxx::hip_device must have destroy() called before destructor.");
  }
}

GASNETT_COLD
void upcxx::hip_device::destroy(upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);

  backend::quiesce(upcxx::world(), eb);

  if (!is_active()) return;

  #if UPCXXI_HIP_ENABLED
    hip_heap_state *st = hip_heap_state::get(heap_idx_);
    UPCXX_ASSERT(st != nullptr);
    UPCXX_ASSERT(st->device_id == device_);

    #if UPCXXI_GEX_MK_HIP
      st->destroy_endpoint("hip_device");
    #endif
    
    if (st->alloc_base) {
      detail::device_allocator_core<upcxx::hip_device>* alloc = 
        static_cast<detail::device_allocator_core<upcxx::hip_device>*>(st->alloc_base);
      UPCXX_ASSERT(alloc);
      alloc->destroy();
      UPCXX_ASSERT(st->alloc_base == &::tombstone);
    }

    UPCXXI_HIP_CHECK_ALWAYS(hipStreamDestroy(st->stream));
    
    backend::heap_state::get(heap_idx_) = nullptr;
    backend::heap_state::free_index(heap_idx_);
    delete st;
  #endif
  
  device_ = invalid_device_id; // deactivate
  heap_idx_ = -1;
}

upcxx::hip_device::id_type 
upcxx::hip_device::device_id(detail::internal_only, int heap_idx) {
  #if UPCXXI_HIP_ENABLED
    hip_heap_state *st = hip_heap_state::get(heap_idx);
    int id = st->device_id;
    UPCXX_ASSERT(id != invalid_device_id);
    return id;
  #else
    UPCXXI_FATAL_ERROR("Internal error on device_allocator::device_id()");
    return invalid_device_id;
  #endif
}

// non-collective default constructor
GASNETT_COLD
detail::device_allocator_core<upcxx::hip_device>::device_allocator_core():
  detail::device_allocator_base(-1/*inactive*/, segment_allocator(nullptr, 0)) { }

// collective constructor with a (possibly inactive) device
GASNETT_COLD
detail::device_allocator_core<upcxx::hip_device>::device_allocator_core(
    upcxx::hip_device &dev, void *base, size_t size
  ):
  detail::device_allocator_base(
    dev.heap_idx_,
    #if UPCXXI_HIP_ENABLED
      make_segment(dev.heap_idx_, base, size)
    #else
      segment_allocator(nullptr, 0)
    #endif
  ) {

  #if UPCXXI_HIP_ENABLED
    if (dev.is_active()) {
      backend::heap_state *hs = backend::heap_state::get(dev.heap_idx_);
      UPCXX_ASSERT(hs->alloc_base == this); // registration handled by device_allocator_base
    }
  #endif
}

GASNETT_COLD
void detail::device_allocator_core<upcxx::hip_device>::destroy() {
  if (!is_active()) return;

  #if UPCXXI_HIP_ENABLED  
      hip_heap_state *st = hip_heap_state::get(heap_idx_);
      UPCXX_ASSERT(st);
     
      if(st->segment_to_free) {
        auto with = hip::context<1>(st->device_id);
        UPCXXI_HIP_CHECK_ALWAYS(hipFree(st->segment_to_free));
        st->segment_to_free = nullptr;
      }
      
      st->alloc_base = &::tombstone; // deregister
  #endif

  heap_idx_ = -1; // deactivate
}

GASNETT_COLD
void detail::device_allocator_core<upcxx::hip_device>::real_destructor() {
  if(upcxx::initialized()) {
    // The thread safety restriction of this call still applies when upcxx isn't
    // initialized, we just have no good way of asserting it so we conditionalize
    // on initialized().
    UPCXXI_ASSERT_ALWAYS_MASTER();
  }

  destroy();
}
