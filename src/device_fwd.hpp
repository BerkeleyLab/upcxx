#ifndef _1c3c7029_0525_47d5_b67d_48d7e2dba80a
#define _1c3c7029_0525_47d5_b67d_48d7e2dba80a

#include <upcxx/intru_queue.hpp>

#include <utility>

namespace upcxx {
  namespace backend {
    struct device_cb {
      detail::intru_queue_intruder<device_cb> intruder;
      void *event;
      virtual void execute_and_delete() = 0;
    };

    template<typename Fn>
    struct device_cb_fn final: device_cb {
      Fn fn;
      device_cb_fn(const Fn &f): fn(f) {}
      device_cb_fn(Fn &&f): fn(std::move(f)) {}
      void execute_and_delete() {
        fn();
        delete this;
      }
    };

    template<typename Fn>
    device_cb_fn<typename std::remove_reference<Fn>::type>*
    make_device_cb(Fn &&fn) {
      return new device_cb_fn<typename std::remove_reference<Fn>::type>(std::forward<Fn>(fn));
    }

    // This type is contained within `__thread` storage, so it must be:
    //   1. trivially destructible.
    //   2. constexpr constructible equivalent to zero-initialization.
    struct persona_device_state {
    #if UPCXXI_CUDA_ENABLED
      struct persona_cuda_state {
        // queue of pending events
        detail::intru_queue<
          device_cb,
          detail::intru_queue_safety::none,
          &device_cb::intruder
        > cbs;
      } cuda;
    #endif
    };
  }
}
#endif
