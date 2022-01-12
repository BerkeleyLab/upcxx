#include <upcxx/cuda.hpp>
#include <upcxx/cuda_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace detail = upcxx::detail;

using std::size_t;
using std::uint64_t;

#if UPCXXI_CUDA_ENABLED
using upcxx::backend::cuda_heap_state;
namespace cuda = upcxx::cuda;

namespace {
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
          upcxx::cuda::cu_failed(r, __FILE__, __LINE__, s.c_str());
      }
      return reinterpret_cast<void*>(p);
    };
    auto dev_free = [st](void *p) {
      auto with = cuda::context<1>(st->context);
      CU_CHECK_ALWAYS(cuMemFree(reinterpret_cast<CUdeviceptr>(p)));
    };
    std::string where("device_allocator<cuda_device> constructor for ");
    if (st) where += "CUDA device " + std::to_string(st->device_id);
    else    where += "inactive cuda_device";
    return cuda_heap_state::make_gpu_segment(st, heap_idx, base, size, 
                                             where.c_str(), dev_alloc, dev_free);
  } // make_segment

  detail::device_allocator_core<upcxx::cuda_device> tombstone;
} // anon namespace

GASNETT_COLD
static std::string get_cuda_info() {
  std::stringstream ss;

  int version = -1;
  if ( cuDriverGetVersion(&version) == CUDA_SUCCESS && version >= 0) {
    ss << "CUDA Driver version: " << version/1000 << "." << (version%1000)/10 << '\n';
  }

  int dev_n = -1;
  if ( cuDeviceGetCount(&dev_n) == CUDA_SUCCESS && dev_n >= 0) {
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
}

GASNETT_COLD
void upcxx::cuda::cu_failed(CUresult res, const char *file, int line, const char *expr, bool report_verbose) {
  const char *errname="", *errstr="";
  cuGetErrorName(res, &errname);
  cuGetErrorString(res, &errstr);
  
  std::stringstream ss;
  ss << expr <<"\n  error="<<errname<<": "<<errstr;

  if (report_verbose) {
    ss << "\n\nCUDA info:\n" << get_cuda_info();
  }
  
  upcxx::detail::fatal_error(ss.str(), "CUDA call failed", nullptr, file, line);
}

#endif

GASNETT_COLD
upcxx::cuda_device::cuda_device(int device):
  device_(device), heap_idx_(-1) {

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);

  #if UPCXXI_CUDA_ENABLED
    if (device != invalid_device_id) {
      heap_idx_ = backend::heap_state::alloc_index();
      CUcontext ctx;
      CUresult res = cuDevicePrimaryCtxRetain(&ctx, device);
      if(res == CUDA_ERROR_NOT_INITIALIZED) {
        CU_CHECK_ALWAYS_VERBOSE(cuInit(0));
        res = cuDevicePrimaryCtxRetain(&ctx, device);
      }
      if (res != CUDA_SUCCESS) {
        std::string callstr("cuDevicePrimaryCtxRetain() failed for device=");
        callstr += std::to_string(device);
        upcxx::cuda::cu_failed(res, __FILE__, __LINE__, callstr.c_str(), true);
      }
      auto with = cuda::context<2>(ctx);

      cuda_heap_state *st = new cuda_heap_state{};
      st->context = ctx;
      st->device_id = device;
      st->segment_to_free = nullptr;

      #if UPCXXI_GEX_MK_CUDA
      { // construct GASNet-level memory kind and endpoint
        std::string where = std::string("CUDA device ") + std::to_string(device);
        gex_MK_Create_args_t args;
        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_CUDA_UVA;
        args.gex_args.gex_class_cuda_uva.gex_CUdevice = device;
        st->create_endpoint(args, heap_idx_, where.c_str());
      }
      #endif
      
      CU_CHECK_ALWAYS_VERBOSE(cuStreamCreate(&st->stream, CU_STREAM_NON_BLOCKING));
      backend::heap_state::get(heap_idx_,true) = st;
    }
  #else
    UPCXX_ASSERT_ALWAYS(device == invalid_device_id);
  #endif
}

GASNETT_COLD
upcxx::cuda_device::~cuda_device() {
  if(backend::init_count > 0) { // we don't assert on leaks after finalization
    UPCXX_ASSERT_ALWAYS(!is_active(), "An active upcxx::cuda_device must have destroy() called before destructor.");
  }
}

GASNETT_COLD
void upcxx::cuda_device::destroy(upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);

  backend::quiesce(upcxx::world(), eb);

  if (!is_active()) return;

  #if UPCXXI_CUDA_ENABLED
    cuda_heap_state *st = cuda_heap_state::get(heap_idx_);
    UPCXX_ASSERT(st != nullptr);
    UPCXX_ASSERT(st->device_id == device_);

    if (st->alloc_base) {
      detail::device_allocator_core<upcxx::cuda_device>* alloc = 
        static_cast<detail::device_allocator_core<upcxx::cuda_device>*>(st->alloc_base);
      UPCXX_ASSERT(alloc);
      alloc->destroy();
      UPCXX_ASSERT(st->alloc_base == &::tombstone);
    }

    #if UPCXXI_GEX_MK_CUDA
      st->destroy_endpoint("cuda_device");
    #endif
    
    CU_CHECK_ALWAYS(cuStreamDestroy(st->stream));
    CU_CHECK_ALWAYS(cuCtxSetCurrent(nullptr));
    CU_CHECK_ALWAYS(cuDevicePrimaryCtxRelease(st->device_id));
    
    backend::heap_state::get(heap_idx_) = nullptr;
    backend::heap_state::free_index(heap_idx_);
    delete st;
  #endif
  
  device_ = invalid_device_id; // deactivate
  heap_idx_ = -1;
}

upcxx::cuda_device::id_type 
upcxx::cuda_device::device_id(detail::internal_only, int heap_idx) {
  #if UPCXXI_CUDA_ENABLED
    cuda_heap_state *st = cuda_heap_state::get(heap_idx);
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
detail::device_allocator_core<upcxx::cuda_device>::device_allocator_core():
  detail::device_allocator_base(-1/*inactive*/, segment_allocator(nullptr, 0)) { }

// collective constructor with a (possibly inactive) device
GASNETT_COLD
detail::device_allocator_core<upcxx::cuda_device>::device_allocator_core(
    upcxx::cuda_device &dev, void *base, size_t size
  ):
  detail::device_allocator_base(
    dev.heap_idx_,
    #if UPCXXI_CUDA_ENABLED
      make_segment(dev.heap_idx_, base, size)
    #else
      segment_allocator(nullptr, 0)
    #endif
  ) {

  #if UPCXXI_CUDA_ENABLED
    if (dev.is_active()) {
      backend::heap_state *hs = backend::heap_state::get(dev.heap_idx_);
      UPCXX_ASSERT(hs->alloc_base == this); // registration handled by device_allocator_base
    }
  #endif
}

GASNETT_COLD
void detail::device_allocator_core<upcxx::cuda_device>::destroy() {
  if (!is_active()) return;

  #if UPCXXI_CUDA_ENABLED  
      cuda_heap_state *st = cuda_heap_state::get(heap_idx_);
      UPCXX_ASSERT(st);
     
      if(st->segment_to_free) {
        auto with = cuda::context<1>(st->context);
        CU_CHECK_ALWAYS(cuMemFree(reinterpret_cast<CUdeviceptr>(st->segment_to_free)));
        st->segment_to_free = nullptr;
      }
      
      st->alloc_base = &::tombstone; // deregister
  #endif

  heap_idx_ = -1; // deactivate
}

GASNETT_COLD
void detail::device_allocator_core<upcxx::cuda_device>::real_destructor() {
  if(upcxx::initialized()) {
    // The thread safety restriction of this call still applies when upcxx isn't
    // initialized, we just have no good way of asserting it so we conditionalize
    // on initialized().
    UPCXXI_ASSERT_ALWAYS_MASTER();
  }

  destroy();
}
