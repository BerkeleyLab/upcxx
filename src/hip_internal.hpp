#ifndef _e6080d2d_0c40_4da2_badc_a99744b713d5
#define _e6080d2d_0c40_4da2_badc_a99744b713d5

#include <upcxx/hip.hpp>
#include <upcxx/diagnostic.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/device_internal.hpp>

#include <stack>

#if UPCXXI_HIP_ENABLED
  #include <hip/hip_runtime_api.h>

  #if UPCXXI_GEX_MK_HIP
    // Validate GASNet native memory kinds support
    #if GASNET_MAXEPS <= 1 || !GASNET_HAVE_MK_CLASS_HIP
    #error Internal error: missing expected GASNet MK HIP support
    #endif
  #endif

  #define UPCXXI_HIP_CHECK_ALWAYS(expr) do { \
      hipError_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != hipSuccess) \
        ::upcxx::detail::hip::hip_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #define UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(expr) do { \
      hipError_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != hipSuccess) \
        ::upcxx::detail::hip::hip_failed(res_xxxxxx, __FILE__, __LINE__, #expr, true); \
    } while(0)


  #if UPCXXI_ASSERT_ENABLED
    #define UPCXXI_HIP_CHECK(expr)   UPCXXI_HIP_CHECK_ALWAYS(expr)
  #else
    #define UPCXXI_HIP_CHECK(expr)   ((void)(expr))
  #endif

  namespace upcxx {
   namespace detail {
    namespace hip {
      UPCXXI_ATTRIB_NORETURN
      void hip_failed(hipError_t res, const char *file, int line, const char *expr, bool report_verbose = false);
      // hip::context: RAII helper for HIP device push/pop, with optional checking
      template<unsigned check_level = 0> // 0=debug only, 1=always, 2=always+verbose
      class context {
        static_assert(check_level <= 2, "invalid check_level in hip::context");
        int dev_old;
        int dev_new; // only valid for check_level > 0
        static constexpr int tombstone = 0xBADBABE;
       public:
        inline context(int device_id) {
          UPCXX_ASSERT(device_id >= 0, "tried to push an invalid device");
          UPCXXI_HIP_CHECK(hipGetDevice(&dev_old)); // no documented errors
          UPCXX_ASSERT(dev_old != tombstone);
          switch (check_level) {
            case 0: UPCXXI_HIP_CHECK(hipSetDevice(device_id)); break;
            case 1: UPCXXI_HIP_CHECK_ALWAYS(hipSetDevice(device_id)); break;
            case 2: UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipSetDevice(device_id)); break;
          }
          if (check_level) dev_new = device_id;
        }
        inline context(context&& other) : dev_old(other.dev_old), dev_new(other.dev_new) {
          other.dev_old = tombstone;
        }
        context(context const &) = delete;
        inline ~context() {
          UPCXXI_IF_PF (dev_old == tombstone) return; // moved out
          if (check_level) {
            int tmp = tombstone;
            UPCXXI_HIP_CHECK(hipGetDevice(&tmp));
            UPCXX_ASSERT_ALWAYS(tmp == dev_new, "Unexpected hipGetDevice outcome -- misbalanced context?");
          }
          switch (check_level) {
            case 0: UPCXXI_HIP_CHECK(hipSetDevice(dev_old)); break;
            case 1: UPCXXI_HIP_CHECK_ALWAYS(hipSetDevice(dev_old)); break;
            case 2: UPCXXI_HIP_CHECK_ALWAYS_VERBOSE(hipSetDevice(dev_old)); break;
          }
        }
      };
  }}} // namespace upcxx::detail::hip
  
  namespace upcxx { namespace backend {
    template<>
    struct device_heap_state<hip_device> : public device_heap_state_base<hip_device> {
        hipStream_t stream;

        detail::par_mutex lock;
        std::stack<hipEvent_t> eventFreeList;
    };
    using hip_heap_state = device_heap_state<hip_device>;
  }} // namespace
#endif
#endif
