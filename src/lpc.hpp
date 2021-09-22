#ifndef _2d897dc6_1696_4bd9_b530_6d923356fa84
#define _2d897dc6_1696_4bd9_b530_6d923356fa84

#include <upcxx/diagnostic.hpp>
#include <upcxx/intru_queue.hpp>
#include <upcxx/utility.hpp>

namespace upcxx {
  namespace detail {
    struct lpc_base;
    
    struct lpc_vtable {
      // Function pointer to be called against `this` instance whose job is to
      // do *something* and take responsibility for the memory behind this
      // instance.
      void(*execute_and_delete)(lpc_base *me);

      // same basic idea as execute_and_delete, but this function pointer is used 
      // during cancellation operations where we need to unwind and reclaim resources 
      // instead of actually doing the *something*.
      // May be null for objects that do not support cancellation.
      void(*cancel_and_delete)(lpc_base *me);

      // Aggregate initialization would cause an overly zealous warning about
      // uninitialized fields with -Wmissing-field-initializers as part of -Wextra.
      // By creating constructors instead of relying on aggregate initialization,
      // it is ensured that this warning won't occur on any construction of lpc_vtable.
      constexpr lpc_vtable(void(*ed)(lpc_base*) = nullptr, void(*cd)(lpc_base*) = nullptr)
        : execute_and_delete(ed)
        , cancel_and_delete(cd)
      {}
      constexpr lpc_vtable(const lpc_vtable&) = default;
      constexpr lpc_vtable(lpc_vtable&&) = default;
    };
    
    // Base class for generic callbacks that can be queued into `lpc_inbox`'s.
    struct lpc_base {
      lpc_vtable const *vtbl;
      detail::intru_queue_intruder<lpc_base> intruder;
    };
    
    // Implementation of `lpc_base` that executes a lambda/function object
    // and deletes itself.
    template<typename Fn>
    struct lpc_impl_fn final: lpc_base {
      static void the_execute_and_delete(lpc_base *me) {
        auto *me1 = static_cast<lpc_impl_fn*>(me);
        static_cast<Fn&&>(me1->fn_)();
        delete me1;
      }
      
      static constexpr lpc_vtable the_vtbl = {&the_execute_and_delete};
      
      Fn fn_;
      
      template<typename Fn1>
      lpc_impl_fn(Fn1 &&fn, int): // extra argument to distinguish from copy ctor
        fn_(static_cast<Fn1&&>(fn)) {
        this->vtbl = &the_vtbl;
      }
    };

    template<typename Fn>
    constexpr lpc_vtable lpc_impl_fn<Fn>::the_vtbl;

    template<typename Fn1, typename Fn = typename std::decay<Fn1>::type>
    lpc_impl_fn<Fn>* make_lpc(Fn1 &&fn) {
      return new lpc_impl_fn<Fn>(std::forward<Fn1>(fn), 0);
    }

    ////////////////////////////////////////////////////////////////////////////
    
    template<typename Fn, typename ...Arg>
    struct lpc_bind {
      Fn fn;
      std::tuple<Arg...> arg;

      template<typename Fn1, typename ...Arg1>
      lpc_bind(Fn1 &&fn, Arg1 &&...arg):
        fn(static_cast<Fn1&&>(fn)),
        arg(static_cast<Arg1&&>(arg)...) {
      }

      template<int ...i>
      void apply_rval(detail::index_sequence<i...>) {
        static_cast<Fn&&>(this->fn)(
          std::get<i>(static_cast<std::tuple<Arg...>&&>(this->arg))...
        );
      }
      
      void operator()() && {
        this->apply_rval(detail::make_index_sequence<sizeof...(Arg)>());
      }
    };

    template<typename Fn>
    struct lpc_bind<Fn> {
      Fn fn;

      template<typename Fn1>
      lpc_bind(Fn1 &&fn):
        fn(static_cast<Fn1&&>(fn)) {
      }

      void operator()() && { static_cast<Fn&&>(fn)(); }
    };

    ////////////////////////////////////////////////////////////////////////////
    // This type is contained within `__thread` storage, so it must be:
    //   1. trivially destructible.
    //   2. constexpr constructible equivalent to zero-initialization.
    template<detail::intru_queue_safety safety>
    class lpc_inbox {
      detail::intru_queue<lpc_base, safety, &lpc_base::intruder> q_;
    
    public:
      constexpr lpc_inbox(): q_() {}
      
      bool empty() const {
        return q_.empty();
      }
      
      template<typename Fn1>
      void send(Fn1 &&fn) {
        q_.enqueue(make_lpc(std::forward<Fn1>(fn)));
      }
      
      void enqueue(lpc_base *m) {
        q_.enqueue(m);
      }
      
      // returns num lpc's executed
      int burst(int max_n = 100) {
        return q_.burst(max_n, [](lpc_base *m) { m->vtbl->execute_and_delete(m); });
      }
    };
  }
}

#endif
