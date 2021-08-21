#ifndef _b514e330_beb4_41df_835b_9dde85882b76
#define _b514e330_beb4_41df_835b_9dde85882b76

#include <upcxx/backend_fwd.hpp>
#include <upcxx/lpc.hpp>
#include <upcxx/persona.hpp>

namespace upcxx {
namespace backend {
namespace gasnet {
  // reply_cb: Am internal-progress lpc which also knows to which persona
  // it is to be enqueued. Useful for handling zero-data AMReply's where we
  // just want to remember what to do once the remote event has been learned.
  // Subclassing is a bit tricky due to the manual vtbl management of lpc's.
  //
  // This is conceptually a restricted form of `lpc_dormant` where:
  //   1. There is no data being delivered (no `T...`).
  //   2. Quiesced promises don't get a special encoding.
  //   3. Lists of lpc's aren't handled.
  //   4. `progress_level::internal` is assumed.
  //   5. Subclassing is expected (and therefor no factory function taking lambda).
  struct reply_cb: detail::lpc_base {
    persona *target;

    reply_cb(detail::lpc_vtable const *vtbl, persona *target) {
      detail::lpc_base::vtbl = vtbl;
      this->target = target;
    }

    // Your subclass should probably have this:
    //
    // static void the_execute_and_delete(detail::lpc_base *me1) {
    //   auto *me = static_cast<my_reply_cb*>(me1);
    //   ...
    // }
    //
    // constexpr detail::lpc_vtable the_vtbl = {&the_execute_and_delete};
    //
    // my_reply_cb(persona *target):
    //   reply_cb(&the_vtbl, target) {
    //   ...
    // }
    
    void fire() {
      detail::persona_tls &tls = detail::the_persona_tls;
      tls.enqueue(
        *this->target, progress_level::internal, this,
        /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
      );
    }
  };
}}}
#endif
