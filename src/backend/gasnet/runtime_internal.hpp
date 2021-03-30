#ifndef _1468755b_5808_4dd6_b81e_607919176956
#define _1468755b_5808_4dd6_b81e_607919176956

/* This header pulls in <gasnet.h> and should not be included from
 * upcxx headers that are exposed to the user.
 */

#include <upcxx/backend.hpp>

#if UPCXX_BACKEND_GASNET
    #include <gasnet.h>
    #include <gasnet_coll.h>
    #define UPCXX_REQUIRES_GEX_SPEC_VERSION_MAJOR  0
    #define UPCXX_REQUIRES_GEX_SPEC_VERSION_MINOR  10 // if you change this number, also change the package version below!!!
    #if GASNET_RELEASE_VERSION_MAJOR < 2000
      // User is trying to compile against GASNet-1, or some other gasnet.h header that is not GASNet-EX
      #error UPC++ requires a current version of GASNet-EX (not to be confused with GASNet-1). Please rerun configure without '--with-gasnet=...' to use the default GASNet-EX layer.
    #elif GEX_SPEC_VERSION_MAJOR <  UPCXX_REQUIRES_GEX_SPEC_VERSION_MAJOR || \
         (GEX_SPEC_VERSION_MAJOR == UPCXX_REQUIRES_GEX_SPEC_VERSION_MAJOR && \
          GEX_SPEC_VERSION_MINOR <  UPCXX_REQUIRES_GEX_SPEC_VERSION_MINOR)
      // User is trying to compile with a GASNet-EX version that does not meet our current minimum requirement:
      #error This version of UPC++ requires GASNet-EX version 2020.3.8 or newer. Please rerun configure (without '--with-gasnet=...') to fetch and use the default GASNet-EX layer.
    #endif
#else
    #error "You've either pulled in this header without first including" \
           "<upcxx/backend.hpp>, or you've made the assumption that" \
           "gasnet is the desired backend (which it isn't)."
#endif

namespace upcxx {
namespace backend {
namespace gasnet {
  inline gex_TM_t handle_of(const upcxx::team &tm) {
    return reinterpret_cast<gex_TM_t>(tm.base(detail::internal_only()).handle);
  }
  
  // Register a handle as a future with the current persona
  inline future<> register_handle_as_future(gex_Event_t h) {
    struct callback: handle_cb {
      promise<> pro;
      void execute_and_delete(handle_cb_successor) {
        backend::fulfill_during<progress_level::user>(
          std::move(detail::promise_as_shref(pro)).steal_header(),
          std::tuple<>()
        );
      }
    };
    
    callback *cb = new callback;
    cb->handle = reinterpret_cast<std::uintptr_t>(h);
    get_handle_cb_queue().enqueue(cb);
    return cb->pro.get_future();
  }
}}}
#endif
