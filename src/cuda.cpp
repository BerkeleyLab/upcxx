#include <upcxx/cuda.hpp>
#include <upcxx/cuda_internal.hpp>
#include <upcxx/reduce.hpp>
#include <upcxx/allocate.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace detail = upcxx::detail;

using std::size_t;
using std::uint64_t;

#if UPCXXI_CUDA_ENABLED
using upcxx::backend::cuda_heap_state;

namespace {
  GASNETT_COLD
  detail::segment_allocator make_segment(int heap_idx, void *base, size_t size) {
    cuda_heap_state *st = heap_idx <= 0 ? nullptr : cuda_heap_state::get(heap_idx);
    uint64_t failed_alloc = 0;

    if (st) { // creating a real device heap, possibly allocating memory

      UPCXX_ASSERT_ALWAYS(size != 0, 
        "device_allocator<cuda_device> constructor requested invalid segment size="<<size);

      CU_CHECK(cuCtxPushCurrent(st->context));
      
      CUdeviceptr p = 0x0;

      if(-size == 1) { // undocumented "largest available"
        size_t lo=1<<20, hi=size_t(16)<<30;
        
        while(hi-lo > 64<<10) {
          if(p) {
            CU_CHECK_ALWAYS(cuMemFree(p));
            p = 0;
          }
          size = (lo + hi)/2;
          CUresult r = cuMemAlloc(&p, size);

          if(r == CUDA_ERROR_OUT_OF_MEMORY) {
            hi = size;
            p = 0;
          } else if(r == CUDA_SUCCESS) {
            lo = size;
          } else {
            CU_CHECK_ALWAYS(r);
          }
        }

        st->segment_to_free = p;
        base = reinterpret_cast<void*>(p);
        if (!p) failed_alloc = size;

      } else if(base == nullptr) { // allocate a particular size
        CUresult r = cuMemAlloc(&p, size);
        if(r == CUDA_ERROR_OUT_OF_MEMORY) {
          failed_alloc = size;
          p = reinterpret_cast<CUdeviceptr>(nullptr);
        } else if (r != CUDA_SUCCESS) {
          std::string s("Requested cuda allocation failed: size=");
          s += std::to_string(size);
          upcxx::cuda::cu_failed(r, __FILE__, __LINE__, s.c_str()); 
        }
        
        st->segment_to_free = p;
        base = reinterpret_cast<void*>(p);

      } else { // client-provided segment
        st->segment_to_free = reinterpret_cast<CUdeviceptr>(nullptr);
      }
      
      CUcontext dump;
      CU_CHECK(cuCtxPopCurrent(&dump));
    }

    // once segment is collectively created, decide whether we are keeping it.
    // this is a effectively a user-level barrier, but not a documented guarantee.
    // This could be a simple boolean bit_or reduction to test if any process failed.
    // However in order to improve error reporting upon allocation failure, 
    // we instead reduce a max over a value constructed as: 
    //   high 44 bits: size_that_failed | low 20 bits: rank_that_failed
    uint64_t alloc_report_max = uint64_t(1LLU<<44) - 1;
    uint64_t reduceval = std::min(failed_alloc, alloc_report_max);
    uint64_t rank_report_max = (1LLU<<20) - 1;
    uint64_t rank_tmp = std::min(std::uint64_t(upcxx::rank_me()), rank_report_max);
    reduceval = (reduceval << 20) | rank_tmp;
    reduceval = upcxx::reduce_all<uint64_t>(reduceval, upcxx::op_fast_max).wait();
    uint64_t largest_failure = reduceval >> 20;


    if (largest_failure > 0) { // single-valued
      // at least one process failed to allocate, so collectively unwind and throw
      if (st && st->segment_to_free) {
        CU_CHECK(cuCtxPushCurrent(st->context));
        CU_CHECK_ALWAYS(cuMemFree(st->segment_to_free));
        CUcontext dump;
        CU_CHECK(cuCtxPopCurrent(&dump));
        st->segment_to_free = reinterpret_cast<CUdeviceptr>(nullptr);
      }

      // collect info to report about the failing request in exn.what()
      uint64_t report_size;
      upcxx::intrank_t report_rank;
      if (failed_alloc) { // ranks that failed report themselves directly
        report_size = failed_alloc;
        report_rank = upcxx::rank_me();
      } else { // others report the largest failing allocation request from the reduction
        UPCXX_ASSERT(largest_failure <= alloc_report_max);
        if (largest_failure == alloc_report_max) report_size = 0; // size overflow
        else report_size = largest_failure;
        reduceval &= rank_report_max;
        if (reduceval == rank_report_max) report_rank = -1; // rank overflow, don't know who
        else report_rank = reduceval;
      }

      throw upcxx::bad_segment_alloc("cuda_device", report_size, report_rank);
    } else {
      #if UPCXXI_GEX_MK_CUDA
      gex_TM_t TM0 = upcxx::backend::gasnet::handle_of(upcxx::world()); UPCXX_ASSERT(TM0 != GEX_TM_INVALID);
      if (st) {
        int ok;
        gex_Client_t client = gex_TM_QueryClient(TM0);

        UPCXX_ASSERT(st->segment == GEX_SEGMENT_INVALID);
        ok = gex_Segment_Create(&st->segment, client, base, size, st->kind, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK && st->segment != GEX_SEGMENT_INVALID, 
          "gex_Segment_Create("<<size<<") failed for CUDA device " << st->device_id);
        
        gex_EP_BindSegment(st->ep, st->segment, 0);
        UPCXX_ASSERT(gex_EP_QuerySegment(st->ep) == st->segment, 
          "gex_EP_BindSegment() failed for CUDA device " << st->device_id);

        ok = gex_EP_PublishBoundSegment(TM0, &st->ep, 1, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK,
          "gex_EP_PublishBoundSegment() failed for CUDA device " << st->device_id);
      } else { // matching collective call for inactive ranks
        int ok = gex_EP_PublishBoundSegment(TM0, nullptr, 0, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK,
          "gex_EP_PublishBoundSegment() failed for inactive CUDA device ");
      }
      #endif

      return detail::segment_allocator(base, size);
    }
  } // make_segment

  detail::device_allocator_core<upcxx::cuda_device> tombstone;
} // anon namespace
#endif

#if UPCXXI_CUDA_ENABLED
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

GASNETT_COLD
void upcxx::cuda::curt_failed(cudaError_t res, const char *file, int line, const char *expr) {
  const char *errname, *errstr;
  errname = cudaGetErrorName(res);
  errstr = cudaGetErrorString(res);
  
  std::stringstream ss;
  ss << expr <<"\n  error="<<errname<<": "<<errstr;
  
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
      CU_CHECK_ALWAYS_VERBOSE(cuCtxPushCurrent(ctx));

      cuda_heap_state *st = new cuda_heap_state{};
      st->context = ctx;
      st->device_id = device;
      st->alloc_base = nullptr;
      st->segment_to_free = reinterpret_cast<CUdeviceptr>(nullptr);

      #if UPCXXI_GEX_MK_CUDA
      {
        int ok;
        gex_TM_t TM0 = backend::gasnet::handle_of(upcxx::world()); UPCXX_ASSERT(TM0 != GEX_TM_INVALID);
        gex_Client_t client = gex_TM_QueryClient(TM0);
        gex_MK_Create_args_t args;

        args.gex_flags = 0;
        args.gex_class = GEX_MK_CLASS_CUDA_UVA;
        args.gex_args.gex_class_cuda_uva.gex_CUdevice = device;
        ok = gex_MK_Create(&st->kind, client, &args, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK, "gex_MK_Create failed for CUDA device " << device);

        ok = gex_EP_Create(&st->ep, client, GEX_EP_CAPABILITY_RMA, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK, "gex_EP_Create failed for heap_idx " << heap_idx_);

        gex_EP_Index_t epidx  = gex_EP_QueryIndex(st->ep);
        UPCXX_ASSERT_ALWAYS(epidx == heap_idx_, 
                           "gex_EP_Create generated unexpected EP_Index "<<epidx<<"for heap_idx "<<heap_idx_);
        st->segment = GEX_SEGMENT_INVALID;
      }
      #endif
      
      CU_CHECK_ALWAYS_VERBOSE(cuStreamCreate(&st->stream, CU_STREAM_NON_BLOCKING));
      backend::heap_state::get(heap_idx_,true) = st;
      
      CU_CHECK_ALWAYS_VERBOSE(cuCtxPopCurrent(&ctx));
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
    // TODO: once they are provided, eventually will do:
    //   gex_Segment_Destroy()
    //   gex_MK_Destroy()
    //   gex_EP_Destroy()  
    // and modify heap_state to allow recycling of heap_idx
      st->segment = GEX_SEGMENT_INVALID;
      st->ep =      GEX_EP_INVALID;
      st->kind =    GEX_MK_INVALID;
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
        CU_CHECK_ALWAYS(cuCtxPushCurrent(st->context));
        CU_CHECK_ALWAYS(cuMemFree(st->segment_to_free));
        st->segment_to_free = reinterpret_cast<CUdeviceptr>(nullptr);
        CUcontext dump;
        CU_CHECK_ALWAYS(cuCtxPopCurrent(&dump));
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
