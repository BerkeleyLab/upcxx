#ifndef _db8a5190_941f_4bd0_a035_64685950d252
#define _db8a5190_941f_4bd0_a035_64685950d252

#include <upcxx/ze.hpp>
#include <upcxx/diagnostic.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/device_internal.hpp>

#include <stack>

#if UPCXXI_ZE_ENABLED
  #include <level_zero/ze_api.h>

  #if UPCXXI_GEX_MK_ZE
    // Validate GASNet native memory kinds support
    #if GASNET_MAXEPS <= 1 || !GASNET_HAVE_MK_CLASS_ZE
    #error Internal error: missing expected GASNet MK ZE support
    #endif
  #endif

  #define UPCXXI_ZE_CHECK_ALWAYS(expr) do { \
      ze_result_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != ZE_RESULT_SUCCESS) \
        ::upcxx::detail::ze::ze_failed(res_xxxxxx, __FILE__, __LINE__, #expr); \
    } while(0)

  #define UPCXXI_ZE_CHECK_ALWAYS_VERBOSE(expr) do { \
      ze_result_t res_xxxxxx = (expr); \
      if_pf (res_xxxxxx != ZE_RESULT_SUCCESS) \
        ::upcxx::detail::ze::ze_failed(res_xxxxxx, __FILE__, __LINE__, #expr, true); \
    } while(0)


  #if UPCXXI_ASSERT_ENABLED
    #define UPCXXI_ZE_CHECK(expr)   UPCXXI_ZE_CHECK_ALWAYS(expr)
  #else
    #define UPCXXI_ZE_CHECK(expr)   ((void)(expr))
  #endif

  namespace upcxx {
   namespace detail {
    namespace ze {
      UPCXXI_ATTRIB_NORETURN
      void ze_failed(ze_result_t res, const char *file, int line, const char *expr, bool report_verbose = false);

  }}} // namespace upcxx::detail::ze
  
  namespace upcxx { namespace backend {
    template<>
    struct device_heap_state<ze_device> : public device_heap_state_base<ze_device> {
        ze_driver_handle_t        zeDriver;
        ze_context_handle_t       zeContext;
        ze_device_handle_t        zeDevice;
        ze_command_queue_handle_t zeCmdQueue;
        std::uint32_t             cmdQueueGroup;

        detail::par_mutex         lock;
        std::stack<
          std::tuple<ze_command_list_handle_t, ze_fence_handle_t>> cmdFreeList;
    };
    using ze_heap_state = device_heap_state<ze_device>;
  }} // namespace
#endif
#endif
