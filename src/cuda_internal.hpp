#ifndef _f49d3597_3d5a_4d7a_822c_d7e602400723
#define _f49d3597_3d5a_4d7a_822c_d7e602400723

#include <upcxx/cuda.hpp>
#include <upcxx/diagnostic.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/device_internal.hpp>

#include <stack>

#if UPCXXI_CUDA_ENABLED
  #include <cuda.h>

  #if UPCXXI_GEX_MK_CUDA
    // Validate GASNet native memory kinds support
    #if GASNET_MAXEPS <= 1 || !GASNET_HAVE_MK_CLASS_CUDA_UVA
    #error Internal error: missing expected GASNet MK CUDA support
    #endif
  #endif

  #define UPCXXI_CU_CHECK_ALWAYS(expr) do { \
      CUresult res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != CUDA_SUCCESS) \
        ::upcxx::detail::cuda::cu_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #define UPCXXI_CU_CHECK_ALWAYS_VERBOSE(expr) do { \
      CUresult res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != CUDA_SUCCESS) \
        ::upcxx::detail::cuda::cu_failed(res_xxxxxx, __FILE__, __LINE__, #expr, true); \
    } while(0)


  #if UPCXXI_ASSERT_ENABLED
    #define UPCXXI_CU_CHECK(expr)   UPCXXI_CU_CHECK_ALWAYS(expr)
  #else
    #define UPCXXI_CU_CHECK(expr)   ((void)(expr))
  #endif

  namespace upcxx {
   namespace detail {
    namespace cuda {
      UPCXXI_ATTRIB_NORETURN
      void cu_failed(CUresult res, const char *file, int line, const char *expr, bool report_verbose = false);
      // cuda::context: RAII helper for CUDA context push/pop, with optional checking
      template<unsigned check_level = 0> // 0=debug only, 1=always, 2=always+verbose
      class context {
        static_assert(check_level <= 2, "invalid check_level in cuda::context");
        CUcontext ctx_;
       public:
        inline context(CUcontext ctx) : ctx_(ctx) {
          UPCXX_ASSERT(ctx, "tried to push an invalid null context");
          switch (check_level) {
            case 0: UPCXXI_CU_CHECK(cuCtxPushCurrent(ctx)); break;
            case 1: UPCXXI_CU_CHECK_ALWAYS(cuCtxPushCurrent(ctx)); break;
            case 2: UPCXXI_CU_CHECK_ALWAYS_VERBOSE(cuCtxPushCurrent(ctx)); break;
          }
        }
        inline context(context&& other) : ctx_(other.ctx_) {
          other.ctx_ = 0;
        }
        context(context const &) = delete;
        inline ~context() {
          UPCXXI_IF_PF (!ctx_) return; // moved out
          CUcontext out;
          switch (check_level) {
            case 0: UPCXXI_CU_CHECK(cuCtxPopCurrent(&out)); break;
            case 1: UPCXXI_CU_CHECK_ALWAYS(cuCtxPopCurrent(&out)); break;
            case 2: UPCXXI_CU_CHECK_ALWAYS_VERBOSE(cuCtxPopCurrent(&out)); break;
          }
          if (check_level) 
            UPCXX_ASSERT_ALWAYS(out == ctx_, "Unexpected cuCtxPopCurrent outcome -- misbalanced context?");
        }
      };
  }}} // namespace upcxx::detail::cuda
  
  namespace upcxx { namespace backend {
    template<>
    struct device_heap_state<cuda_device> : public device_heap_state_base<cuda_device> {
        CUcontext context;
        CUstream stream;

        detail::par_mutex lock;
        std::stack<CUevent> eventFreeList;
    };
    using cuda_heap_state = device_heap_state<cuda_device>;
  }} // namespace
#endif
#endif
