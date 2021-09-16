#ifndef _96368972_b5ed_4e48_ac4f_8c868279e3dd
#define _96368972_b5ed_4e48_ac4f_8c868279e3dd

#include <upcxx/backend.hpp>
#include <upcxx/bind.hpp>
#include <upcxx/future.hpp>
#include <upcxx/lpc_dormant.hpp>
#include <upcxx/persona.hpp>
#include <upcxx/utility.hpp>

#include <tuple>

#ifndef UPCXX_DEFER_COMPLETION
  #define UPCXX_DEFER_COMPLETION 0 // default is eager
#endif
#if UPCXX_DEFER_COMPLETION
  #define UPCXXI_EAGER_DEFAULT false
#else
  #define UPCXXI_EAGER_DEFAULT true
#endif

namespace upcxx {
  //////////////////////////////////////////////////////////////////////////////
  /* Event names for common completion events as used by rput/rget etc. This
  set is extensible from anywhere in the source. These are left as incomplete
  types since only their names matter as template parameters (usually spelled
  "Event" in this file). Spelling is `[what]_cx_event`.
  */
  
  struct source_cx_event;
  struct remote_cx_event;
  struct operation_cx_event;

  namespace detail {
    // Useful type predicates for selecting events (as needed by
    // completions_state's EventPredicate argument).
    template<typename Event>
    struct event_is_here: std::false_type {};
    template<>
    struct event_is_here<source_cx_event>: std::true_type {};
    template<>
    struct event_is_here<operation_cx_event>: std::true_type {};

    template<typename Event>
    struct event_is_remote: std::false_type {};
    template<>
    struct event_is_remote<remote_cx_event>: std::true_type {};
  }
  
  //////////////////////////////////////////////////////////////////////////////
  /* Completion action descriptors holding the information provided by the
  user. Spelling is `[what]_cx`. The type encodes the event this action
  corresponds to. The runtime state should hold copies of whatever runtime
  state the user supplied with no extra processing. The corresponding event can
  be queried via `::event_t`. Since the `rpc_cx` action is shipped remotely to
  fulfill `as_rpc`, to make the other templates manageable, all actions must
  "pretend" to also support serialization by supplying a `::deserialized_cx`
  type to be used as its `deserialized_type`.
  */

  namespace detail {
  // Future completion to be fulfilled during given progress level
  // If eager is true, then fulfillment can be done eagerly and need
  // not be deferred until the given progress level
  template<typename Event, bool eager, progress_level level = progress_level::user>
  struct future_cx {
    using event_t = Event;
    using deserialized_cx = future_cx<Event,eager,level>;
    // stateless
  };

  // Promise completion
  // If eager is true, then fulfillment can be done eagerly and need
  // not be deferred until user progress
  template<typename Event, bool eager, typename ...T>
  struct promise_cx {
    using event_t = Event;
    using deserialized_cx = promise_cx<Event,eager,T...>;
    detail::promise_shref<T...> pro_;
  };

  // Synchronous completion via best-effort buffering
  template<typename Event>
  struct buffered_cx {
    using event_t = Event;
    using deserialized_cx = buffered_cx<Event>;
    // stateless
  };

  // Synchronous completion via blocking on network/peers
  template<typename Event>
  struct blocking_cx {
    using event_t = Event;
    using deserialized_cx = blocking_cx<Event>;
    // stateless
  };

  // LPC completion
  template<typename Event, typename Fn>
  struct lpc_cx {
    using event_t = Event;
    using deserialized_cx = lpc_cx<Event,Fn>;
    
    persona *target_;
    Fn fn_;

    template<typename Fn1>
    lpc_cx(persona &target, Fn1 &&fn):
      target_(&target),
      fn_(std::forward<Fn1>(fn)) {
    }
  };
  
  // RPC completion. Arguments are already bound into fn_ (via detail::bind).
  template<typename Event, typename Fn>
  struct rpc_cx {
    using event_t = Event;
    using deserialized_cx = rpc_cx<Event, typename serialization_traits<Fn>::deserialized_type>;
    
    Fn fn_;
  };
  } // namespace detail
  
  //////////////////////////////////////////////////////////////////////////////
  /* completions<...>: A list of completion actions. We use lisp-like lists where
  the head is the first element and the tail is the list of everything after.
  */
  
  namespace detail {
  template<typename ...Cxs>
  struct completions;
  template<>
  struct completions<> {};
  template<typename H, typename ...T>
  struct completions<H,T...>: completions<T...> {
    H head_;

    H& head() { return head_; }
    const H& head() const { return head_; }
    completions<T...>& tail() { return *this; }
    const completions<T...>& tail() const { return *this; }

    H&& head_moved() {
      return static_cast<H&&>(head_);
    }
    completions<T...>&& tail_moved() {
      return static_cast<completions<T...>&&>(*this);
    }

    constexpr completions(H &&head, T &&...tail):
      completions<T...>(std::move(tail)...),
      head_(std::move(head)) {
    }
    template<typename H1>
    constexpr completions(H1 &&head, completions<T...> &&tail):
      completions<T...>(std::move(tail)),
      head_(std::forward<H1>(head)) {
    }
    template<typename H1>
    constexpr completions(H1 &&head, const completions<T...> &tail):
      completions<T...>(tail),
      head_(std::forward<H1>(head)) {
    }
  };
  } // namespace detail

  //////////////////////////////////////////////////////////////////////////////
  // operator "|": Concatenates two completions lists.
  
  namespace detail {
  template<typename ...B>
  constexpr completions<B...>&& operator|(
      completions<> a, completions<B...> &&b
    ) {
    return std::move(b);
  }
  template<typename ...B>
  constexpr const completions<B...>& operator|(
      completions<> a, const completions<B...> &b
    ) {
    return b;
  }

  template<typename Ah, typename ...At, typename ...B>
  constexpr completions<Ah,At...,B...> operator|(
      completions<Ah,At...> &&a,
      completions<B...> &&b
    ) {
    return completions<Ah,At...,B...>{
      a.head_moved(),
      a.tail_moved() | std::move(b)
    };
  }
  template<typename Ah, typename ...At, typename ...B>
  constexpr completions<Ah,At...,B...> operator|(
      completions<Ah,At...> &&a,
      const completions<B...> &b
    ) {
    return completions<Ah,At...,B...>{
      a.head_moved(),
      a.tail_moved() | b
    };
  }
  template<typename Ah, typename ...At, typename ...B>
  constexpr completions<Ah,At...,B...> operator|(
      const completions<Ah,At...> &a,
      completions<B...> &&b
    ) {
    return completions<Ah,At...,B...>{
      a.head(),
      a.tail() | std::move(b)
    };
  }
  template<typename Ah, typename ...At, typename ...B>
  constexpr completions<Ah,At...,B...> operator|(
      const completions<Ah,At...> &a,
      const completions<B...> &b
    ) {
    return completions<Ah,At...,B...>{
      a.head(),
      a.tail() | b
    };
  }
  } // namespace detail

  //////////////////////////////////////////////////////////////////////////////
  // detail::completions_has_event: detects if there exists an action associated
  // with the given event in the completions list.

  namespace detail {
    template<typename Cxs, typename Event>
    struct completions_has_event;
    
    template<typename Event>
    struct completions_has_event<completions<>, Event> {
      static constexpr bool value = false;
    };
    template<typename CxH, typename ...CxT, typename Event>
    struct completions_has_event<completions<CxH,CxT...>, Event> {
      static constexpr bool value =
        std::is_same<Event, typename CxH::event_t>::value ||
        completions_has_event<completions<CxT...>, Event>::value;
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  // detail::completions_is_event_sync: detects if there exists a buffered_cx or
  // blocking_cx action tagged by the given event in the completions list

  namespace detail {
    template<typename Cxs, typename Event>
    struct completions_is_event_sync;
    
    template<typename Event>
    struct completions_is_event_sync<completions<>, Event> {
      static constexpr bool value = false;
    };
    template<typename ...CxT, typename Event>
    struct completions_is_event_sync<completions<buffered_cx<Event>,CxT...>, Event> {
      static constexpr bool value = true;
    };
    template<typename ...CxT, typename Event>
    struct completions_is_event_sync<completions<blocking_cx<Event>,CxT...>, Event> {
      static constexpr bool value = true;
    };
    template<typename CxH, typename ...CxT, typename Event>
    struct completions_is_event_sync<completions<CxH,CxT...>, Event> {
      static constexpr bool value =
        completions_is_event_sync<completions<CxT...>, Event>::value;
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // detail::is_not_array

  namespace detail {
    template<typename T>
    struct is_not_array :
      std::integral_constant<
          bool,
          !std::is_array<typename std::remove_reference<T>::type>::value
        > {};
  }

  //////////////////////////////////////////////////////////////////////
  // detail::as_rpc_return: computes the return type of as_rpc, while
  // also checking whether the function object can be invoked with the
  // given arguments when converted to their off-wire types

  namespace detail {
    template<typename Call, typename = void>
    struct check_rpc_call : std::false_type {};
    template<typename Fn, typename ...Args>
    struct check_rpc_call<
        Fn(Args...),
        decltype(
          std::declval<typename binding<Fn>::off_wire_type&&>()(
            std::declval<typename binding<Args>::off_wire_type&&>()...
          ),
          void()
        )
      > : std::true_type {};

    template<typename Event, typename Fn, typename ...Args>
    struct as_rpc_return {
        static_assert(
          detail::trait_forall<
              detail::is_not_array,
              Args...
            >::value,
          "Arrays may not be passed as arguments to as_rpc. "
          "To send the contents of an array, use upcxx::make_view() to construct a upcxx::view over the elements."
        );
        static_assert(
          detail::trait_forall<
              is_serializable,
              typename binding<Args>::on_wire_type...
            >::value,
          "All rpc arguments must be Serializable."
        );
        static_assert(
          detail::trait_forall<
              is_lvalue_or_copyable,
              Fn, Args...
            >::value,
          "All rvalue as_rpc arguments must be CopyConstructible."
        );
        static_assert(
          detail::trait_forall<
              detail::is_deserialized_move_constructible,
              Fn, Args...
            >::value,
          "Deserialized type of all as_rpc arguments must be MoveConstructible."
        );
        static_assert(
          check_rpc_call<Fn(Args...)>::value,
          "function object provided to as_rpc cannot be invoked on the given arguments as rvalue references "
          "(after deserialization of the function object and arguments). "
          "Note: make sure that the function object does not have any non-const lvalue-reference parameters."
        );
        static_assert(
          detail::trait_forall<
              detail::type_respects_static_size_limit,
              typename binding<Args>::on_wire_type...
            >::value,
          UPCXXI_STATIC_ASSERT_RPC_MSG(remote_cx::as_rpc)
        );

      using type = completions<
          rpc_cx<Event, typename bind1<Fn&&, Args&&...>::return_type>
        >;
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  // User-interface for obtaining a singleton completion list (one action tied
  // to one event).

  namespace detail {
    template<typename Event>
    struct support_as_future {
      static constexpr completions<future_cx<Event, UPCXXI_EAGER_DEFAULT>> as_future() {
        return {future_cx<Event, UPCXXI_EAGER_DEFAULT>{}};
      }
      static constexpr completions<future_cx<Event, false>> as_defer_future() {
        return {future_cx<Event, false>{}};
      }
      static constexpr completions<future_cx<Event, true>> as_eager_future() {
        return {future_cx<Event, true>{}};
      }
    };

    template<typename Event>
    struct support_as_promise {
      template<typename ...T>
      static constexpr completions<promise_cx<Event, UPCXXI_EAGER_DEFAULT, T...>>
      as_promise(promise<T...> pro) {
        return {promise_cx<Event, UPCXXI_EAGER_DEFAULT, T...>{
          static_cast<promise_shref<T...>&&>(promise_as_shref(pro))
        }};
      }
      template<typename ...T>
      static constexpr completions<promise_cx<Event, false, T...>>
      as_defer_promise(promise<T...> pro) {
        return {promise_cx<Event, false, T...>{
          static_cast<promise_shref<T...>&&>(promise_as_shref(pro))
        }};
      }
      template<typename ...T>
      static constexpr completions<promise_cx<Event, true, T...>>
      as_eager_promise(promise<T...> pro) {
        return {promise_cx<Event, true, T...>{
          static_cast<promise_shref<T...>&&>(promise_as_shref(pro))
        }};
      }
    };

    template<typename Event>
    struct support_as_lpc {
      template<typename Fn>
      static constexpr completions<lpc_cx<Event, typename std::decay<Fn>::type>>
      as_lpc(persona &target, Fn &&func) {
        return {
          lpc_cx<Event, typename std::decay<Fn>::type>{target, std::forward<Fn>(func)}
        };
      }
    };

    template<typename Event>
    struct support_as_buffered {
      static constexpr completions<buffered_cx<Event>> as_buffered() {
        return {buffered_cx<Event>{}};
      }
    };

    template<typename Event>
    struct support_as_blocking {
      static constexpr completions<blocking_cx<Event>> as_blocking() {
        return {blocking_cx<Event>{}};
      }
    };

    template<typename Event>
    struct support_as_rpc {
      template<typename Fn, typename ...Args>
      static typename detail::as_rpc_return<Event, Fn, Args...>::type
      as_rpc(Fn &&fn, Args &&...args) {
        return {
          rpc_cx<Event, typename detail::bind1<Fn&&, Args&&...>::return_type>{
            detail::bind(std::forward<Fn>(fn), std::forward<Args>(args)...)
          }
        };
      }
    };
  }

  struct source_cx:
    detail::support_as_blocking<source_cx_event>,
    detail::support_as_buffered<source_cx_event>,
    detail::support_as_future<source_cx_event>,
    detail::support_as_lpc<source_cx_event>,
    detail::support_as_promise<source_cx_event> {};
  
  struct operation_cx:
    #if UPCXXI_HAS_OPERATION_CX_AS_BLOCKING
      detail::support_as_blocking<operation_cx_event>,
    #endif
    detail::support_as_future<operation_cx_event>,
    detail::support_as_lpc<operation_cx_event>,
    detail::support_as_promise<operation_cx_event> {};
  
  struct remote_cx:
    detail::support_as_rpc<remote_cx_event> {};

  //////////////////////////////////////////////////////////////////////
  // operation_cx_as_future_t: default completions for most operations
  namespace detail {
    using operation_cx_as_future_t =
      completions<future_cx<operation_cx_event, UPCXXI_EAGER_DEFAULT>>;
    using operation_cx_as_internal_future_t =
      completions<future_cx<operation_cx_event, UPCXXI_EAGER_DEFAULT,
                            progress_level::internal>>;
  }

  //////////////////////////////////////////////////////////////////////
  // cx_event_done: used to inform completions_returner what event has
  // already completed, allowing eager futures to be optimized

  namespace detail {
    namespace help {
      enum cx_event_flag: int {
        none_event_flag = 0,
        source_event_flag = 1 << 0,
        remote_event_flag = 1 << 1,
        operation_event_flag = 1 << 2
      };
    }

    enum class cx_event_done: int {
      none = 0,
      source = help::source_event_flag,
      remote = help::source_event_flag | // remote implies source
               help::remote_event_flag,
      operation = help::source_event_flag | // operation implies source
                  help::remote_event_flag | // operation implies remote
                  help::operation_event_flag
    };

    template<help::cx_event_flag category = help::none_event_flag>
    struct cx_event_is_done_base {
      bool operator()(cx_event_done value) {
        return static_cast<bool>(static_cast<int>(value) &
                                 static_cast<int>(category));
      }
    };

    template<typename Event>
    struct cx_event_is_done: cx_event_is_done_base<> {};
    template<>
    struct cx_event_is_done<source_cx_event>:
      cx_event_is_done_base<help::source_event_flag> {};
    template<>
    struct cx_event_is_done<remote_cx_event>:
      cx_event_is_done_base<help::remote_event_flag> {};
    template<>
    struct cx_event_is_done<operation_cx_event>:
      cx_event_is_done_base<help::operation_event_flag> {};
  }

  //////////////////////////////////////////////////////////////////////
  // cx_non_future_return, cx_result_combine, and cx_remote_dispatch:
  // Collect results of as_rpc invocations. The purpose is to ensure
  // that returned futures are propagated all the way out to the
  // top-level binding, so that view-buffer and input-argument
  // lifetime extension can be applied.

  namespace detail {
    struct cx_non_future_return {};

    // collect futures
    template<typename T1, typename T2>
    cx_non_future_return cx_result_combine(T1 &&v1, T2 &&v2) {
      return cx_non_future_return{};
    }
    template<typename T1, typename Kind2, typename ...T2>
    future1<Kind2, T2...> cx_result_combine(T1 &&v1,
                                            future1<Kind2, T2...> &&v2) {
      return std::forward<future1<Kind2, T2...>>(v2);
    }
    template<typename Kind1, typename ...T1, typename T2>
    future1<Kind1, T1...> cx_result_combine(future1<Kind1, T1...> &&v1,
                                            T2 &&v2) {
      return std::forward<future1<Kind1, T1...>>(v1);
    }
    template<typename Kind1, typename ...T1, typename Kind2, typename ...T2>
    auto cx_result_combine(future1<Kind1, T1...> &&v1,
                           future1<Kind2, T2...> &&v2)
      UPCXXI_RETURN_DECLTYPE(
        detail::when_all_fast(std::forward<future1<Kind1, T1...>>(v1),
                              std::forward<future1<Kind2, T2...>>(v2))
      ) {
      return detail::when_all_fast(std::forward<future1<Kind1, T1...>>(v1),
                                   std::forward<future1<Kind2, T2...>>(v2));
    }

    // call fn, converting non-future return type to
    // cx_non_future_return
    // The main purpose of this is actually to deal with a void
    // return type. We need a return value so that it can be passed
    // to cx_result_combine(). Since the return type is not a
    // future, we don't care about the return value in the non-void
    // case and just return a cx_non_future_return unconditionally.
    template<typename Fn>
    cx_non_future_return
    call_convert_non_future(Fn &&fn, std::false_type/* returns_future*/) {
      static_cast<Fn&&>(fn)();
      return {};
    }
    template<typename Fn>
    auto call_convert_non_future(Fn &&fn, std::true_type/* returns_future*/)
      UPCXXI_RETURN_DECLTYPE(static_cast<Fn&&>(fn)()) {
      return static_cast<Fn&&>(fn)();
    }

    // We need to compute the type of combining results manually, since
    // we are C++11. If we were C++14, we could just use auto for the
    // return type of cx_remote_dispatch::operator(). We can't use
    // decltype since operator() calls itself recursively -- the
    // definition is incomplete when decltype would be used in the
    // signature, so the recursive call is not resolved and the whole
    // thing fails to substitute.
    template<typename ...Fn>
    struct cx_remote_dispatch_t;

    template<typename Fn>
    using cx_decayed_result =
      typename std::decay<detail::invoke_result_t<Fn>>::type;

    template<typename Fn>
    using cx_converted_rettype = typename std::conditional<
      detail::is_future1<cx_decayed_result<Fn>>::value,
      detail::invoke_result_t<Fn>,
      cx_non_future_return
    >::type;

    template<typename Fn>
    struct cx_remote_dispatch_t<Fn> {
      using type = cx_converted_rettype<Fn>;
    };

    template<typename Fn1, typename Fn2, typename ...Fns>
    struct cx_remote_dispatch_t<Fn1, Fn2, Fns...> {
      using type = decltype(
        cx_result_combine(
          std::declval<cx_converted_rettype<Fn1>>(),
          std::declval<typename cx_remote_dispatch_t<Fn2, Fns...>::type>()
        )
      );
    };

    // cx_remote_dispatch calls the given functions, combining all
    // future results into one big, conjoined future. This enables
    // lifetime extension for the arguments to as_rpc callbacks; the
    // cleanup gets chained on the resulting future, and it will not
    // execute until the future is ready
    struct cx_remote_dispatch {
      cx_non_future_return operator()() {
        UPCXX_ASSERT_ALWAYS(false,
                            "internal error: empty cx_remote_dispatch "
                            "means that an unnecessary remote completion "
                            "was sent over the wire!");
        return {};
      }
      template<typename Fn>
      typename cx_remote_dispatch_t<Fn&&>::type operator()(Fn &&fn) {
        return call_convert_non_future(
            static_cast<Fn&&>(fn),
            std::integral_constant<
              bool,
              detail::is_future1<cx_decayed_result<Fn&&>>::value
            >{}
          );
      }
      template<typename Fn1, typename Fn2, typename ...Fns>
      typename cx_remote_dispatch_t<Fn1&&, Fn2&&, Fns&&...>::type
      operator()(Fn1 &&fn1, Fn2 &&fn2, Fns &&...fns) {
        // Note: we can't use one big when_all(), as it will result in
        // futures inside of futures. Instead, we combine the results
        // sequentially.
        return cx_result_combine(
            operator()(static_cast<Fn1&&>(fn1)),
            operator()(static_cast<Fn2&&>(fn2),
                       static_cast<Fns&&>(fns)...)
          );
      }
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  /* detail::cx_state: Per action state that survives until the event is
  triggered. For future_cx's this holds a promise instance which seeds the
  future given back to the user. All other cx actions get their information
  stored as-is.

  // Specializations should look like:
  template<typename Event, typename ...T>
  struct cx_state<whatever_cx<Event>, std::tuple<T...>> {
    void set_done(cx_event_done) {
      // This function sets the done state of this completion for eager
      // optimization.
    }

    // There will be exactly one call to one of the following functions before
    // this state destructs...

    void operator()(T...) {
      // This functions should be marked `&&` but isn't to support legacy.
      
      // Event has been satisfied so fire this action, Must work in any progress
      // context. Notice event values are taken sans-reference since an event
      // may have multiple "listeners", each should get a private copy.
    }

    void cancel() {
      // Operation is being cancelled, perform cleanup actions only
    }
    
    lpc_dormant<T...> to_lpc_dormant(lpc_dormant<T...> *tail) && {
      // Convert this state into a dormant lpc (chained against a supplied tail
      // which could be null).
    }
  };
  */
  
  namespace detail {
    template<typename Cx /* the action specialized upon */,
             typename EventArgsTup /* tuple containing list of action's value types*/>
    struct cx_state;
    
    template<typename Event>
    struct cx_state<buffered_cx<Event>, std::tuple<>> {
      void set_done(cx_event_done) {}
      cx_state(buffered_cx<Event>) {}
      void operator()() {}
      void cancel() {}
    };
    
    template<typename Event>
    struct cx_state<blocking_cx<Event>, std::tuple<>> {
      void set_done(cx_event_done) {}
      cx_state(blocking_cx<Event>) {}
      void operator()() {}
      void cancel() {}
    };
    
    // wrapper around make_future<>() that type checks when T... is
    // nonempty
    template<typename ...T>
    future<T...> make_ready_empty_future() {
      // this should never be used
      UPCXXI_FATAL_ERROR("make_ready_empty_future<T...>() called with nonempty T");
      return {};
    }
    template<>
    inline future<> make_ready_empty_future() {
      return make_future<>();
    }

    template<typename Event, bool eager, progress_level level, typename ...T>
    struct cx_state<future_cx<Event,eager,level>, std::tuple<T...>> {
      future_header_promise<T...> *pro_; // holds ref, no need to drop it in destructor since we move out it in either operator() ro to_lpc_dormant
      #if UPCXXI_ASSERT_ENABLED
        bool get_future_invoked = false;
      #endif

      cx_state(future_cx<Event,eager,level>):
        pro_(nullptr) {
      }

      // completions_returner_head handles cx_state<future_cx> specially and requires
      // this additional method rather than set_done().
      future<T...> get_future(cx_event_done value) /*const*/ {
        #if UPCXXI_ASSERT_ENABLED
          get_future_invoked = true;
        #endif

        if (eager && sizeof...(T) == 0 && cx_event_is_done<Event>()(value)) {
          // return ready empty future rather than creating a promise
          return make_ready_empty_future<T...>();
        }

        pro_ = new future_header_promise<T...>;
        return detail::promise_get_future(pro_);
      }
      
      lpc_dormant<T...>* to_lpc_dormant(lpc_dormant<T...> *tail) && {
        UPCXX_ASSERT(pro_, "internal error: pro_ null in to_lpc_dormant");
        return detail::make_lpc_dormant_quiesced_promise<T...>(
          upcxx::current_persona(), progress_level::user, /*move ref*/pro_, tail
        );
      }
      
      void operator()(T ...vals) {
        UPCXX_ASSERT(get_future_invoked,
                     "internal error: operator() called before get_future");
        if (eager && sizeof...(T) == 0 && !pro_) {
          // nothing to do if a promise was not allocated by a call to
          // get_future()
        } else if (eager) {
          backend::fulfill_now(
            /*move ref*/pro_, std::tuple<T...>(static_cast<T&&>(vals)...)
          );
        } else {
          backend::fulfill_during<level>(
            /*move ref*/pro_, std::tuple<T...>(static_cast<T&&>(vals)...)
          );
        }
      }

      void cancel() {
        // balance injection increment and dropref:
        if (pro_) backend::fulfill_now(/*move ref*/pro_, 1);
      }
    };

    /* There are multiple specializations for promise_cx since both the promise
    and event have their own `T...` and either these must match or the event's
    list is empty */
    
    // Case when promise and event have matching (non-empty) type lists T...
    template<typename Event, bool eager, typename ...T>
    struct cx_state<promise_cx<Event,eager,T...>, std::tuple<T...>> {
      future_header_promise<T...> *pro_; // holds ref

      cx_state(promise_cx<Event,eager,T...> &&cx):
        pro_(static_cast<promise_cx<Event,eager,T...>&&>(cx).pro_.steal_header()) {
        detail::promise_require_anonymous(pro_, 1);
      }
      cx_state(const promise_cx<Event,eager,T...> &cx):
        cx_state(promise_cx<Event,eager,T...>(cx)) {}

      void set_done(cx_event_done) {}

      lpc_dormant<T...>* to_lpc_dormant(lpc_dormant<T...> *tail) && {
        future_header_promise<T...> *pro = /*move ref*/pro_;
        return detail::make_lpc_dormant(
          upcxx::current_persona(), progress_level::user,
          [/*move ref*/pro](T &&...results) {
            backend::fulfill_during<progress_level::user>(
              /*move ref*/pro, std::tuple<T...>(static_cast<T&&>(results)...)
            );
          },
          [/*move ref*/pro]() { // upon cancellation:
            // balance injection increment and dropref:
            backend::fulfill_now(/*move ref*/pro, 1);
          },
          tail
        );
      }
      
      void operator()(T ...vals) {
        if (eager) {
          backend::fulfill_now(
            /*move ref*/pro_, std::tuple<T...>(static_cast<T&&>(vals)...)
          );
        } else {
          backend::fulfill_during<progress_level::user>(
            /*move ref*/pro_, std::tuple<T...>(static_cast<T&&>(vals)...)
          );
        }
      }

      void cancel() {
        // balance injection increment and dropref:
        backend::fulfill_now(/*move ref*/pro_, 1);
      }
    };
    // Case when event type list is empty
    template<typename Event, bool eager, typename ...T>
    struct cx_state<promise_cx<Event,eager,T...>, std::tuple<>> {
      future_header_promise<T...> *pro_; // holds ref

      cx_state(promise_cx<Event,eager,T...> &&cx):
        pro_(static_cast<promise_cx<Event,eager,T...>&&>(cx).pro_.steal_header()) {}
      cx_state(const promise_cx<Event,eager,T...> &cx):
        cx_state(promise_cx<Event,eager,T...>(cx)) {}
      
      void set_done(cx_event_done value) {
        if (eager && cx_event_is_done<Event>()(value)) {
          // when eager and done, avoid incrementing the dependency count
          pro_->dropref();
          pro_ = nullptr;
        } else {
          detail::promise_require_anonymous(pro_, 1);
        }
      }

      lpc_dormant<>* to_lpc_dormant(lpc_dormant<> *tail) && {
        UPCXX_ASSERT(pro_, "internal error: pro_ null in to_lpc_dormant");
        future_header_promise<T...> *pro = /*move ref*/pro_;
        return detail::make_lpc_dormant(
          upcxx::current_persona(), progress_level::user,
          [/*move ref*/pro]() {
            backend::fulfill_during<progress_level::user>(/*move ref*/pro, 1);
          },
          [/*move ref*/pro]() { // upon cancellation:
            // balance injection increment and dropref:
            backend::fulfill_now(/*move ref*/pro, 1);
          },
          tail
        );
      }
      
      void operator()() {
        // when pro_ is null, dependency count was not incremented, so
        // nothing to do here
        if (pro_) {
          backend::fulfill_during<progress_level::user>(/*move ref*/pro_, 1);
        }
      }

      void cancel() {
        // balance injection increment and dropref:
        if (pro_) backend::fulfill_now(/*move ref*/pro_, 1);
      }
    };
    // Case when promise and event type list are both empty
    template<typename Event, bool eager>
    struct cx_state<promise_cx<Event,eager>, std::tuple<>> {
      future_header_promise<> *pro_; // holds ref

      cx_state(promise_cx<Event,eager> &&cx):
        pro_(static_cast<promise_cx<Event,eager>&&>(cx).pro_.steal_header()) {}
      cx_state(const promise_cx<Event,eager> &cx):
        cx_state(promise_cx<Event,eager>(cx)) {}

      void set_done(cx_event_done value) {
        if (eager && cx_event_is_done<Event>()(value)) {
          // when eager and done, avoid incrementing the dependency count
          pro_->dropref();
          pro_ = nullptr;
        } else {
          detail::promise_require_anonymous(pro_, 1);
        }
      }

      lpc_dormant<>* to_lpc_dormant(lpc_dormant<> *tail) && {
        UPCXX_ASSERT(pro_, "internal error: pro_ null in to_lpc_dormant");
        future_header_promise<> *pro = /*move ref*/pro_;
        return detail::make_lpc_dormant<>(
          upcxx::current_persona(), progress_level::user,
          [/*move ref*/pro]() {
            backend::fulfill_during<progress_level::user>(/*move ref*/pro, 1);
          },
          [/*move ref*/pro]() { // upon cancellation:
            // balance injection increment and dropref:
            backend::fulfill_now(/*move ref*/pro, 1);
          },
          tail
        );
      }
      
      void operator()() {
        // when pro_ is null, dependency count was not incremented, so
        // nothing to do here
        if (pro_) {
          backend::fulfill_during<progress_level::user>(/*move ref*/pro_, 1);
        }
      }

      void cancel() {
        // balance injection increment and dropref:
        if (pro_) backend::fulfill_now(/*move ref*/pro_, 1);
      }
    };
    
    template<typename Event, typename Fn, typename ...T>
    struct cx_state<lpc_cx<Event,Fn>, std::tuple<T...>> {
      persona *target_;
      Fn fn_;
      
      cx_state(lpc_cx<Event,Fn> &&cx):
        target_(cx.target_),
        fn_(static_cast<Fn&&>(cx.fn_)) {
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) += 1;
      }
      cx_state(const lpc_cx<Event,Fn> &cx):
        target_(cx.target_),
        fn_(cx.fn_) {
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) += 1;
      }

      void set_done(cx_event_done) {}

      lpc_dormant<T...>* to_lpc_dormant(lpc_dormant<T...> *tail) && {
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) -= 1;
        return detail::make_lpc_dormant(*target_, progress_level::user, 
                                        std::move(fn_), [](){}, tail);
      }
      
      void operator()(T ...vals) {
        target_->lpc_ff(
          detail::lpc_bind<Fn,T...>(static_cast<Fn&&>(fn_), static_cast<T&&>(vals)...)
        );
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) -= 1;
      }

      void cancel() { 
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) -= 1;
      }
    };

    // cx_state<rpc_cx<...>> does not fit the usual mold since the event isn't
    // triggered locally. Instead, fn_ is extracted and sent over the wire by
    // completions_state<...>::bind_event().
    template<typename Event, typename Fn, typename ...T>
    struct cx_state<rpc_cx<Event,Fn>, std::tuple<T...>> {
      Fn fn_;
      
      cx_state(rpc_cx<Event,Fn> &&cx):
        fn_(static_cast<Fn&&>(cx.fn_)) {
      }
      cx_state(const rpc_cx<Event,Fn> &cx):
        fn_(cx.fn_) {
      }

      void set_done(cx_event_done) {}
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  // cx_get_remote_fn: extract the remote functions from an individual
  // completion or cx_state

  namespace detail {
    template<typename CxOrCxState>
    std::tuple<> cx_get_remote_fn(const CxOrCxState &) { return {}; }

    template<typename Fn>
    std::tuple<const Fn&> cx_get_remote_fn(const rpc_cx<remote_cx_event,Fn> &cx) {
      return std::tuple<const Fn&>{cx.fn_};
    }

    template<typename Fn, typename ...T>
    std::tuple<const Fn&> cx_get_remote_fn(
        const cx_state<rpc_cx<remote_cx_event,Fn>, std::tuple<T...>> &state
      ) {
      return std::tuple<const Fn&>{state.fn_};
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // cx_bind_remote_fns: binds all the given functions together into a
  // single bound function to be sent over the wire.

  namespace detail {
    template<typename FnRefTuple, int ...i>
    auto cx_bind_remote_fns(FnRefTuple &&fns, detail::index_sequence<i...>)
      UPCXXI_RETURN_DECLTYPE (
        detail::bind(
          cx_remote_dispatch{},
          std::get<i>(std::forward<FnRefTuple>(fns))...
        )
      ) {
      return detail::bind(
          cx_remote_dispatch{},
          std::get<i>(std::forward<FnRefTuple>(fns))...
        );
    }

    template<typename FnRefTuple>
    auto cx_bind_remote_fns(FnRefTuple &&fns)
      UPCXXI_RETURN_DECLTYPE(
        cx_bind_remote_fns(
          fns,
          detail::make_index_sequence<
            std::tuple_size<typename std::decay<FnRefTuple>::type>::value
          >{}
        )
      ) {
      return cx_bind_remote_fns(
          fns,
          detail::make_index_sequence<
            std::tuple_size<typename std::decay<FnRefTuple>::type>::value
          >{}
        );
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /* detail::completions_state: Constructed against a user-supplied
  completions<...> value, converting its action descriptors into tracked
  state (e.g. what_cx<Event> becomes cx_state<what_cx<Event>>). Clients are typically
  given a completions<...> by the user (a list of actions), and should
  construct this class to convert those actions into a list of tracked stateful
  things. As various events complete, this thing should be notified and it will
  visit all its members firing their actions.

  A completions_state takes an EventPredicate to select which events are
  tracked (the unselected carry no state and do nothing when notified). This is
  required to support remote events. When both local and remote events are in
  play the client will construct two completions_state's against the same
  completions<...> list. One instance to track local completions (using
  EventPredicate=detail::event_is_here), and the other (with
  EventPredicate=detail::event_is_remote) to be shipped off and invoked
  remotely.

  We also take an EventValues map which assigns to each event type the runtime
  value types produced by the event (some T...).

  More specifically the template args are...
  
  EventPredicate<Event>::value: Maps an event-type to a compile-time bool value
  for enabling that event in this instance.

  EventValues::tuple_t<Event>: Maps an event-type to a type-list (wrapped as a
  tuple<T...>) which types the values reported by the completed action.
  `operator()` will expect that the runtime values it receives match the types
  reported by this map for the given event.
  
  ordinal: indexes the nesting depth of this type so that base classes
  with identical types can be disambiguated.
  */
  namespace detail {
    template<template<typename> class EventPredicate,
             typename EventValues,
             typename Cxs,
             int ordinal=0> 
    struct completions_state /*{
      using completions_t = Cxs; // retrieve the original completions<...> user type

      // True iff no events contained in `Cxs` are enabled by `EventPredicate`.
      static constexpr bool empty;

      // Fire actions corresponding to `Event` if its enabled. Type-list
      // V... should match the T... in `EventValues::tuple_t<Event>`.
      template<typename Event, typename ...V>
      void operator()(V&&...); // should be &&, but not for legacy

      // Create a callable to fire all actions associated with given event. Note
      // this is only supported for Event==remote_cx_event, and it assumes that
      // the event does not produce any values. The returned bound function
      // contains references to functions inside this completion, so it must be
      // consumed before the completion dies.
      template<typename Event>
      SomeCallable bind_event();

      // Same as the above, but create the callable directly from a completions
      // without needing to create a completions_state first.
      template<typename Event>
      static SomeCallable bind_event_static(const Cxs &);

      // Convert states of actions associated with given Event to dormant lpc list
      template<typename Event>
      lpc_dormant<...> to_lpc_dormant() &&;

      // Cancel actions associated with given Event
      template<typename Event>
      void cancel() &&;
    }*/;

    // completions_state specialization for empty completions<>
    template<template<typename> class EventPredicate,
             typename EventValues,
             int ordinal>
    struct completions_state<EventPredicate, EventValues,
                             completions<>, ordinal> {

      using completions_t = completions<>;
      static constexpr bool empty = true;
      
      completions_state(completions<>) {}
      
      template<typename Event, typename ...V>
      void operator()(V &&...vals) {/*nop*/}

      struct event_bound {
        template<typename ...V>
        void operator()(V &&...vals) {/*nop*/}
      };
      
      template<typename Event>
      event_bound bind_event() const {
        static_assert(std::is_same<Event, remote_cx_event>::value,
                      "internal error: bind_event() currently only "
                      "supported for remote_cx_event");
        return event_bound{};
      }

      template<typename Event>
      static event_bound bind_event_static(completions<>) {
        static_assert(std::is_same<Event, remote_cx_event>::value,
                      "internal error: bind_event() currently only "
                      "supported for remote_cx_event");
        return event_bound{};
      }

      std::tuple<> get_remote_fns() const { return {}; }
      static std::tuple<> get_remote_fns(completions<>) { return {}; }

      template<typename Event>
      typename detail::tuple_types_into<
          typename EventValues::template tuple_t<Event>,
          lpc_dormant
        >::type*
      to_lpc_dormant() && {
        return nullptr; // the empty lpc_dormant list
      }

      template<typename Event>
      void cancel() && {/*nop*/}
    };

    /* completions_state for non-empty completions<...> deconstructs list one
    at a time recursively. The first element is the head, the list of
    everything else is the tail. It inherits a completions_state_head for each
    list item, passing it the predicate boolean evaluated for the item's event,
    and the entire EventValues mapping (not sure why as this feels like the
    right place to evaluate the mapping on the item's event type just like we
    do with the predicate).

    completions_state_head handles the logic of being and doing nothing for
    disabled events. It also exposes action firing mechanisms that test that the
    completed event matches the one associated with the action. This makes the
    job of completions_state easy, as it can just visit all the heads and fire
    them.
    */

    template<bool event_selected, typename EventValues, typename Cx, int ordinal>
    struct completions_state_head;

    // completions_state_head with event disabled (predicate=false)
    template<typename EventValues, typename Cx, int ordinal>
    struct completions_state_head<
        /*event_enabled=*/false, EventValues, Cx, ordinal
      > {
      static constexpr bool empty = true;

      completions_state_head(Cx &&cx) {}
      completions_state_head(const Cx &cx) {}
      
      template<typename Event, typename ...V>
      void operator()(V&&...) {/*nop*/}

      template<typename Event>
      void cancel() && {/*nop*/}

      std::tuple<> get_remote_fn() const { return {}; }
      static std::tuple<> get_remote_fn(const Cx &) { return {}; }

      // Set the done state of this completion for eager optimization.
      void set_done(cx_event_done) {}
    };

    template<typename Cx>
    using cx_event_t = typename Cx::event_t;

    // completions_state_head with event enabled (predicate=true)
    template<typename EventValues, typename Cx, int ordinal>
    struct completions_state_head<
        /*event_enabled=*/true, EventValues, Cx, ordinal
      > {
      static constexpr bool empty = false;

      cx_state<Cx, typename EventValues::template tuple_t<cx_event_t<Cx>>> state_;
      
      completions_state_head(Cx &&cx):
        state_(std::move(cx)) {
      }
      completions_state_head(const Cx &cx):
        state_(cx) {
      }
      
      template<typename ...V>
      void operator_case(std::integral_constant<bool,true>, V &&...vals) {
        // Event matches CxH::event_t
        state_.operator()(std::forward<V>(vals)...);
      }
      template<typename ...V>
      void operator_case(std::integral_constant<bool,false>, V &&...vals) {
        // Event mismatch = nop
      }

      // fire state if Event == CxH::event_t
      template<typename Event, typename ...V>
      void operator()(V &&...vals) {
        this->operator_case(
          std::integral_constant<
            bool,
            std::is_same<Event, typename Cx::event_t>::value
          >{},
          std::forward<V>(vals)...
        );
      }

      auto get_remote_fn() const UPCXXI_RETURN_DECLTYPE(cx_get_remote_fn(state_)) {
        return cx_get_remote_fn(state_);
      }

      static auto get_remote_fn(const Cx &cx)
        UPCXXI_RETURN_DECLTYPE(cx_get_remote_fn(cx)) {
        return cx_get_remote_fn(cx);
      }

      // Set the done state of this completion for eager optimization.
      void set_done(cx_event_done value) {
        state_.set_done(value);
      }
      
      template<typename Event, typename Lpc>
      Lpc* to_lpc_dormant_case(std::true_type, Lpc *tail) && {
        return std::move(state_).to_lpc_dormant(tail);
      }

      template<typename Event, typename Lpc>
      Lpc* to_lpc_dormant_case(std::false_type, Lpc *tail) && {
        return tail; // ignore this event, just return tail (meaning no append)
      }

      // Append dormant lpc to tail iff Event == CxH::event_t
      template<typename Event, typename Lpc>
      Lpc* to_lpc_dormant(Lpc *tail) && {
        return std::move(*this).template to_lpc_dormant_case<Event>(
          std::integral_constant<bool,
            std::is_same<Event, typename Cx::event_t>::value
          >(),
          tail
        );
      }

      // fire state if Event == CxH::event_t
      template<typename Event>
      void cancel() && {
        if ( std::is_same<Event, typename Cx::event_t>::value )
          state_.cancel();
      }
    };

    // Now we can define completions_state for non empty completions<...>
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH, typename ...CxT,
             int ordinal>
    struct completions_state<EventPredicate, EventValues,
                             completions<CxH,CxT...>, ordinal>:
        // head base class
        completions_state_head<EventPredicate<typename CxH::event_t>::value,
                               EventValues, CxH, ordinal>,
        // Tail base class. Incrementing the ordinal is essential so that the
        // head bases of this tail base are disambiguated from our head.
        completions_state<EventPredicate, EventValues,
                          completions<CxT...>, ordinal+1> {

      using completions_t = completions<CxH, CxT...>;
      
      using head_t = completions_state_head<
          /*event_enabled=*/EventPredicate<typename CxH::event_t>::value,
          EventValues, CxH, ordinal
        >;
      using tail_t = completions_state<EventPredicate, EventValues,
                                       completions<CxT...>, ordinal+1>;

      static constexpr bool empty = head_t::empty && tail_t::empty;
      
      completions_state(completions<CxH,CxT...> &&cxs):
        head_t(cxs.head_moved()),
        tail_t(cxs.tail_moved()) {
      }
      completions_state(const completions<CxH,CxT...> &cxs):
        head_t(cxs.head()),
        tail_t(cxs.tail()) {
      }
      completions_state(head_t &&head, tail_t &&tail):
        head_t(std::move(head)),
        tail_t(std::move(tail)) {
      }
      
      head_t& head() { return static_cast<head_t&>(*this); }
      head_t const& head() const { return *this; }
      
      tail_t& tail() { return static_cast<tail_t&>(*this); }
      tail_t const& tail() const { return *this; }

      template<typename Event, typename ...V>
      void operator()(V &&...vals) {
        // fire the head element
        head_t::template operator()<Event>(
          static_cast<
              // An empty tail means we are the lucky one who gets the
              // opportunity to move-out the given values (if caller supplied
              // reference type permits, thank you reference collapsing).
              typename std::conditional<tail_t::empty, V&&, V const&>::type
            >(vals)...
        );
        // recurse to fire remaining elements
        tail_t::template operator()<Event>(static_cast<V&&>(vals)...);
      }

      auto get_remote_fns() const
        UPCXXI_RETURN_DECLTYPE(std::tuple_cat(head().get_remote_fn(),
                                             tail().get_remote_fns())) {
        return std::tuple_cat(head().get_remote_fn(),
                              tail().get_remote_fns());
      }

      static auto get_remote_fns(const completions<CxH,CxT...> &cxs)
        UPCXXI_RETURN_DECLTYPE(std::tuple_cat(head_t::get_remote_fn(cxs.head()),
                                             tail_t::get_remote_fns(cxs.tail()))) {
        return std::tuple_cat(head_t::get_remote_fn(cxs.head()),
                              tail_t::get_remote_fns(cxs.tail()));
      }

      template<typename Event>
      auto bind_event() const
        UPCXXI_RETURN_DECLTYPE(cx_bind_remote_fns(get_remote_fns())) {
        static_assert(std::is_same<Event, remote_cx_event>::value,
                      "internal error: bind_event() currently only "
                      "supported for remote_cx_event");
        return cx_bind_remote_fns(get_remote_fns());
      }

      template<typename Event>
      static auto bind_event_static(const completions<CxH,CxT...> &cxs)
        UPCXXI_RETURN_DECLTYPE(cx_bind_remote_fns(get_remote_fns(cxs))) {
        static_assert(std::is_same<Event, remote_cx_event>::value,
                      "internal error: bind_event() currently only "
                      "supported for remote_cx_event");
        return cx_bind_remote_fns(get_remote_fns(cxs));
      }

      template<typename Event>
      typename detail::tuple_types_into<
          typename EventValues::template tuple_t<Event>,
          lpc_dormant
        >::type*
      to_lpc_dormant() && {
        return static_cast<head_t&&>(*this).template to_lpc_dormant<Event>(
          static_cast<tail_t&&>(*this).template to_lpc_dormant<Event>()
        );
      }

      template<typename Event>
      void cancel() && {
        // cancel the head element
        static_cast<head_t&&>(*this).template cancel<Event>();
        // recurse to cancel remaining elements
        static_cast<tail_t&&>(*this).template cancel<Event>();
      }
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  /* detail::completions_returner: Manage return type for completions<...>
  object. Construct one of these instances against a detail::completions_state&
  *before* any injection is done. Afterwards, call operator() to produce the
  return value desired by user.
  */
  
  namespace detail {
    /* The implementation of completions_returner tears apart the completions<...>
    list matching for future_cx's since those are the only ones that return
    something to user.
    */
    template<template<typename> class EventPredicate,
             typename EventValues, typename Cxs>
    struct completions_returner;

    // completions_returner for empty completions<>
    template<template<typename> class EventPredicate,
             typename EventValues>
    struct completions_returner<EventPredicate, EventValues, completions<>> {
      using return_t = void;

      template<int ordinal>
      completions_returner(
          completions_state<EventPredicate, EventValues, completions<>, ordinal>&,
          cx_event_done completed = cx_event_done::none
        ) {
      }
      
      void operator()() const {/*return void*/}
    };

    // completions_returner_head is inherited by completions_returner to dismantle
    // the head element of the list and recursively inherit completions_returner
    // of the tail.
    template<template<typename> class EventPredicate,
             typename EventValues, typename Cxs/*user completions<...>*/,
             typename TailReturn/*return type computed by tail*/>
    struct completions_returner_head;

    // specialization: we found a future_cx and are appending our return value
    // onto a tuple of return values
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH_event, bool eager,
             progress_level level, typename ...CxT,
             typename ...TailReturn_tuplees>
    struct completions_returner_head<
        EventPredicate, EventValues,
        completions<future_cx<CxH_event,eager,level>, CxT...>,
        std::tuple<TailReturn_tuplees...>
      > {
      
      using return_t = std::tuple<
          detail::future_from_tuple_t<
            detail::future_kind_default,
            typename EventValues::template tuple_t<CxH_event>
          >,
          TailReturn_tuplees...
        >;

      return_t ans_;
      return_t&& operator()() { return std::move(ans_); }
      
      template<typename CxState, typename Tail>
      completions_returner_head(CxState &s, Tail &&tail, cx_event_done completed):
        ans_{
          std::tuple_cat(
            std::make_tuple(s.head().state_.get_future(completed)),
            tail()
          )
        } {
      }
    };

    // specialization: we found a future_cx and one other item is returning a
    // value, so we introduce a two-element tuple.
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH_event, bool eager,
             progress_level level, typename ...CxT,
             typename TailReturn_not_tuple>
    struct completions_returner_head<
        EventPredicate, EventValues,
        completions<future_cx<CxH_event,eager,level>, CxT...>,
        TailReturn_not_tuple
      > {
      
      using return_t = std::tuple<
          detail::future_from_tuple_t<
            detail::future_kind_default,
            typename EventValues::template tuple_t<CxH_event>
          >,
          TailReturn_not_tuple
        >;
      
      return_t ans_;
      return_t&& operator()() { return std::move(ans_); }
      
      template<typename CxState, typename Tail>
      completions_returner_head(CxState &s, Tail &&tail, cx_event_done completed):
        ans_(
          std::make_tuple(
            s.head().state_.get_future(completed),
            tail()
          )
        ) {
      }
    };

    // specialization: we found a future_cx and are the first to want to return
    // a value.
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH_event, bool eager,
             progress_level level, typename ...CxT>
    struct completions_returner_head<
        EventPredicate, EventValues,
        completions<future_cx<CxH_event,eager,level>, CxT...>,
        void
      > {
      
      using return_t = detail::future_from_tuple_t<
          detail::future_kind_default,
          typename EventValues::template tuple_t<CxH_event>
        >;
      
      return_t ans_;
      return_t&& operator()() { return std::move(ans_); }
      
      template<typename CxState, typename Tail>
      completions_returner_head(CxState &s, Tail&&, cx_event_done completed):
        ans_(
          s.head().state_.get_future(completed)
        ) {
      }
    };

    // specialization: the action is not a future, do not muck with return value
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH_not_future, typename ...CxT,
             typename TailReturn>
    struct completions_returner_head<
        EventPredicate, EventValues,
        completions<CxH_not_future, CxT...>,
        TailReturn
      >:
      completions_returner<
          EventPredicate, EventValues, completions<CxT...>
        > {
      
      template<typename CxState>
      completions_returner_head(
          CxState& s,
          completions_returner<
            EventPredicate, EventValues, completions<CxT...>
          > &&tail,
          cx_event_done completed
        ):
        completions_returner<
            EventPredicate, EventValues, completions<CxT...>
          >{std::move(tail)} {
        s.head().set_done(completed);
      }
    };

    // completions_returner for non-empty completions<...>: inherit
    // completions_returner_head which will dismantle the head element and
    // recursively inherit completions_returner on the tail list.
    template<template<typename> class EventPredicate,
             typename EventValues, typename CxH, typename ...CxT>
    struct completions_returner<EventPredicate, EventValues,
                                completions<CxH,CxT...>>:
      completions_returner_head<
        EventPredicate, EventValues,
        completions<CxH,CxT...>,
        typename completions_returner<
            EventPredicate, EventValues, completions<CxT...>
          >::return_t
      > {

      template<int ordinal>
      completions_returner(
          completions_state<
              EventPredicate, EventValues, completions<CxH,CxT...>, ordinal
            > &s,
          cx_event_done completed = cx_event_done::none
        ):
        completions_returner_head<
          EventPredicate, EventValues,
          completions<CxH,CxT...>,
          typename completions_returner<
              EventPredicate, EventValues, completions<CxT...>
            >::return_t
        >{s,
          completions_returner<
              EventPredicate, EventValues, completions<CxT...>
            >{s.tail(), completed},
          completed
        } {
      }
    };
  }
}
#endif

