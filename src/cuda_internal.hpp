#ifndef _f49d3597_3d5a_4d7a_822c_d7e602400723
#define _f49d3597_3d5a_4d7a_822c_d7e602400723

#include <upcxx/cuda.hpp>
#include <upcxx/diagnostic.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/device_internal.hpp>

#if UPCXXI_CUDA_ENABLED
  #include <cuda.h>
  #include <cuda_runtime_api.h>

  #if UPCXXI_GEX_MK_CUDA
    #include <gasnet_mk.h>
    // Validate GASNet native memory kinds support
    #if GASNET_MAXEPS <= 1 || !GASNET_HAVE_MK_CLASS_CUDA_UVA
    #error Internal error: missing expected GASNet MK CUDA support
    #endif
  #endif

  namespace upcxx {
    namespace cuda {
      UPCXXI_ATTRIB_NORETURN
      void cu_failed(CUresult res, const char *file, int line, const char *expr, bool report_verbose = false);
      UPCXXI_ATTRIB_NORETURN
      void curt_failed(cudaError_t res, const char *file, int line, const char *expr);
    }
  }
  
  #define CU_CHECK_ALWAYS(expr) do { \
      CUresult res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != CUDA_SUCCESS) \
        ::upcxx::cuda::cu_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #define CU_CHECK_ALWAYS_VERBOSE(expr) do { \
      CUresult res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != CUDA_SUCCESS) \
        ::upcxx::cuda::cu_failed(res_xxxxxx, __FILE__, __LINE__, #expr, true); \
    } while(0)


  #define CURT_CHECK_ALWAYS(expr) do { \
      cudaError_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != cudaSuccess) \
        ::upcxx::cuda::curt_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #if UPCXXI_ASSERT_ENABLED
    #define CU_CHECK(expr)   CU_CHECK_ALWAYS(expr)
    #define CURT_CHECK(expr) CURT_CHECK_ALWAYS(expr)
  #else
    #define CU_CHECK(expr)   ((void)(expr))
    #define CURT_CHECK(expr) ((void)(expr))
  #endif

  namespace upcxx { namespace backend {
    template<>
    struct device_heap_state<cuda_device> : public device_heap_state_base<cuda_device> {
        int device_id;
        CUcontext context;
        CUstream stream;
        CUdeviceptr segment_to_free;
    };
    using cuda_heap_state = device_heap_state<cuda_device>;
  }} // namespace
#endif
#endif
