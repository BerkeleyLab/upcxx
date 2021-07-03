#ifndef _1c3c7029_0525_47d5_b67d_48d7e2dba80a
#define _1c3c7029_0525_47d5_b67d_48d7e2dba80a

#include <upcxx/intru_queue.hpp>

#include <utility>

namespace upcxx {
  namespace cuda {
    struct event_cb {
      detail::intru_queue_intruder<event_cb> intruder;
      void *cu_event;
      virtual void execute_and_delete() = 0;
    };

    template<typename Fn>
    struct event_cb_fn final: event_cb {
      Fn fn;
      event_cb_fn(const Fn &f): fn(f) {}
      event_cb_fn(Fn &&f): fn(std::move(f)) {}
      void execute_and_delete() {
        fn();
        delete this;
      }
    };

    template<typename Fn>
    event_cb_fn<typename std::remove_reference<Fn>::type>*
    make_event_cb(Fn &&fn) {
      return new event_cb_fn<typename std::remove_reference<Fn>::type>(std::forward<Fn>(fn));
    }

    // This type is contained within `__thread` storage, so it must be:
    //   1. trivially destructible.
    //   2. constexpr constructible equivalent to zero-initialization.
    struct persona_state {
    #if UPCXXI_CUDA_ENABLED
      // queue of pending events
      detail::intru_queue<
          event_cb,
          detail::intru_queue_safety::none,
          &event_cb::intruder
        > event_cbs;
    #endif
    };
  }
}
#endif
