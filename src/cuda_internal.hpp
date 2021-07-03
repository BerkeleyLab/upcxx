#ifndef _f49d3597_3d5a_4d7a_822c_d7e602400723
#define _f49d3597_3d5a_4d7a_822c_d7e602400723

#include <upcxx/cuda.hpp>
#include <upcxx/diagnostic.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>

#if UPCXX_CUDA_ENABLED
  #include <cuda.h>
  #include <cuda_runtime_api.h>

  // Decide whether GASNet has native memory kinds support
  #include <gasnet_mk.h>
  #ifndef UPCXXI_MAXEPS
  #error Missing UPCXXI_MAXEPS definition
  #endif
  #if UPCXXI_MAXEPS > 1 && GASNET_HAVE_MK_CLASS_CUDA_UVA
    #define UPCXX_CUDA_USE_MK 1
  #endif

  namespace upcxx {
    namespace cuda {
      UPCXXI_ATTRIB_NORETURN
      void cu_failed(CUresult res, const char *file, int line, const char *expr);
      UPCXXI_ATTRIB_NORETURN
      void curt_failed(cudaError_t res, const char *file, int line, const char *expr);
    }
  }
  
  #define CU_CHECK_ALWAYS(expr) do { \
      CUresult res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != CUDA_SUCCESS) \
        ::upcxx::cuda::cu_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #define CURT_CHECK_ALWAYS(expr) do { \
      cudaError_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != cudaSuccess) \
        ::upcxx::cuda::curt_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #if UPCXX_ASSERT_ENABLED
    #define CU_CHECK(expr)   CU_CHECK_ALWAYS(expr)
    #define CURT_CHECK(expr) CURT_CHECK_ALWAYS(expr)
  #else
    #define CU_CHECK(expr)   ((void)(expr))
    #define CURT_CHECK(expr) ((void)(expr))
  #endif

  namespace upcxx {
    namespace cuda {
      bool use_mk(); // true iff using GASNet memory kinds
      struct device_state : public backend::heap_state {
        int device_id;
        CUcontext context;
        CUstream stream;
        CUdeviceptr segment_to_free;

        #if UPCXX_CUDA_USE_MK
          // gex objects...
          gex_EP_t ep;
          gex_MK_t kind;
          gex_Segment_t segment;
        #endif

	device_state() : backend::heap_state(backend::heap_state::memory_kind::cuda) {}

        static device_state *get(std::int32_t heap_idx, bool allow_null = false) {
          backend::heap_state *hs = backend::heap_state::get(heap_idx, allow_null);
	  if (hs) UPCXX_ASSERT(hs->kind() == backend::heap_state::memory_kind::cuda);
          return static_cast<device_state*>(hs);
        }
      };
    }
  }
#endif
#endif
