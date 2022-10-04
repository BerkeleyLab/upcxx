#ifndef _2b6c472e_b888_4b91_9051_8f0b7aad9192
#define _2b6c472e_b888_4b91_9051_8f0b7aad9192

#include <upcxx/backend_fwd.hpp>
#include <upcxx/future.hpp>
#include <upcxx/lpc.hpp>

namespace upcxx {
  namespace detail {
    template<typename ...T>
    struct deserialized_raw_tuple;

    /* A dormant lpc is one that hasn't been enqueued to run, but knows its target
     * persona and progress level so that it may be enqueued later. The `T...`
     * values are a tuple of data this lpc is waiting for before it can be
     * enqueued.
     *
     * The address of a lpc_dormant is particularly useful to for communicating
     * the address of a continuation to survive a roundtrip.
     */
    template<typename ...T>
    struct lpc_dormant: lpc_base {
      persona *target;
      progress_level level;

      // enqueue all lpc's contained in the list for which `this` is the head.
      template<typename ...U>
      void awaken(deserialized_raw_tuple<U...> &&results);

      // cancel a list of lpc_dormant, freeing storage without enqueue/execute
      static void cancel_and_delete(lpc_dormant *);
    };

    // Make a lpc_dormant* from lambda.
    template<typename ...T, typename Fn, typename CancelFn>
    lpc_dormant<T...>* make_lpc_dormant(
        persona &target, progress_level level, 
        Fn &&fn, CancelFn &&cancel,
        lpc_dormant<T...> *tail
      );
    
    // Make lpc_dormant* which will enqueue a given quiesced promise (one which
    // has no requirement/fulfillment activity concurrently occurring) to apply
    // its deferred fulfillments.
    template<typename ...T>
    lpc_dormant<T...>* make_lpc_dormant_quiesced_promise(
        persona &target, progress_level level,
        detail::future_header_promise<T...> *pro, // takes ref
        lpc_dormant<T...> *tail
      );
    
    ////////////////////////////////////////////////////////////////////////////
    
    template<typename ...T>
    struct lpc_dormant_qpromise final: lpc_dormant<T...> {
      lpc_dormant_qpromise(persona &target, progress_level level, detail::future_header_promise<T...> *pro) {
        this->target = &target;
        this->level = level;
        this->vtbl = reinterpret_cast<lpc_vtable*>(0x1 | reinterpret_cast<std::uintptr_t>(pro));
      }
    };
    
    template<typename ...T>
    struct lpc_dormant_fn_base: lpc_dormant<T...> {
      union { std::tuple<T...> results; };
      lpc_dormant_fn_base() {}
      ~lpc_dormant_fn_base() {}
    };
    
    template<typename Fn, typename CancelFn, typename ...T>
    struct lpc_dormant_fn final: lpc_dormant_fn_base<T...> {
      Fn fn;
      CancelFn cancel;

      template<int ...i>
      void apply_help(detail::index_sequence<i...>) {
        std::move(this->fn)(std::move(std::get<i>(this->results))...);
      }
      
      static void the_execute_and_delete(lpc_base *me1) {
        auto *me = static_cast<lpc_dormant_fn*>(me1);
        me->apply_help(detail::make_index_sequence<sizeof...(T)>());
        using results_t = std::tuple<T...>;
        me->results.~results_t();
        delete me;
      }

      static void the_cancel_and_delete(lpc_base *me1) {
        auto *me = static_cast<lpc_dormant_fn*>(me1);
        std::move(me->cancel)();
        delete me;
      }
      
      static constexpr lpc_vtable the_vtbl = {&the_execute_and_delete, &the_cancel_and_delete};
      
      lpc_dormant_fn(persona &target, progress_level level, Fn &&fn, CancelFn &&cancel):
        fn(std::forward<Fn>(fn)), cancel(std::forward<CancelFn>(cancel)) {
        this->vtbl = &the_vtbl;
        this->target = &target;
        this->level = level;
      }
    };
    
    template<typename Fn, typename CancelFn, typename ...T>
    constexpr lpc_vtable lpc_dormant_fn<Fn,CancelFn,T...>::the_vtbl;

    // Make a lpc_dormant* from lambda.
    template<typename ...T, typename Fn1, typename CancelFn1>
    lpc_dormant<T...>* make_lpc_dormant(
        persona &target, progress_level level, Fn1 &&fn, CancelFn1 &&cancel,
        lpc_dormant<T...> *tail
      ) {
      using Fn = typename std::decay<Fn1>::type;
      using CancelFn = typename std::decay<CancelFn1>::type;
      auto *lpc = new lpc_dormant_fn<Fn,CancelFn,T...>(target, level, 
                                                       std::forward<Fn1>(fn),
                                                       std::forward<CancelFn1>(cancel));
      lpc->intruder.p.store(tail, std::memory_order_relaxed);
      return lpc;
    }

    // Make lpc_dormant* which will enqueue a given quiesced promise (one which
    // has no requirement/fulfillment activity concurrently occurring) to apply
    // its deferred fulfillments.
    template<typename ...T>
    lpc_dormant<T...>* make_lpc_dormant_quiesced_promise(
        persona &target, progress_level level,
        detail::future_header_promise<T...> *pro, // takes ref
        lpc_dormant<T...> *tail
      ) {
      auto *lpc = new lpc_dormant_qpromise<T...>(target, level, /*move ref*/pro);
      lpc->intruder.p.store(tail, std::memory_order_relaxed);
      return lpc;
    }

    ////////////////////////////////////////////////////////////////////////////

    class serialization_reader;

    // [de]serialized_raw_tuple allow the arguments of a dormant lpc
    // to be deserialized on demand. This reduces the number of moves
    // in the common case that there is only one completion registered
    // against a result. In that case, the result is deserialized
    // directly into the completion's result cell.
    // Note that this implementation relies on two assumptions:
    // 1) A serialized_raw_tuple does not outlive its underlying tuple
    //    object. This is satisfied because send_awaken_lpc
    //    immediately passes it to bind and then serializes the
    //    result.
    // 2) The deserialization buffer is not reclaimed until all
    //    completions have been processed. This is satisfied because
    //    the cleanup callback is chained on the lambda that invokes
    //    lpc_dormant::awaken.

    template<typename ...T>
    struct deserialized_raw_tuple {
      serialization_reader &r;
      template<typename ...U>
      void read_into_raw_tuple(std::tuple<U...> *dst) {
        serialization_reader tmp{r};
        tmp.read_into<std::tuple<T...>>((void*) dst);
      }
      template<typename ...U>
      void read_into_future_header_result(future_header_result<U...> &fhr) {
        serialization_reader tmp{r};
        tmp.read_into<std::tuple<T...>>(&fhr.results_raw_);
        fhr.base_header.status_ = future_header_result<T...>::status_results_yes;
      }
      void read_into_future_header_result(future_header_result<> &fhr) {
        fhr.construct_results();
      }
    };

    template<typename ...T>
    struct serialized_raw_tuple {
      const std::tuple<T...> &tup;
      serialized_raw_tuple(const std::tuple<T...> &tup_) : tup(tup_) {}
      struct upcxx_serialization {
        template<typename Writer>
        static void serialize(Writer &w, const serialized_raw_tuple &x) {
          w.write(x.tup);
        }
        template<typename Reader, typename Storage>
        static deserialized_raw_tuple<T...>* deserialize(Reader &r, Storage storage) {
          return storage.construct(r);
        }
      };
    };

    ////////////////////////////////////////////////////////////////////////////

    template<typename ...T>
    template<typename ...U>
    void lpc_dormant<T...>::awaken(deserialized_raw_tuple<U...> &&results) {
      // You'll see this thrown into if()'s to permit dead code detection by compiler
      constexpr bool results_is_copyable = std::is_copy_constructible<std::tuple<T...>>::value;

      // tuple<T...> const& if copyable otherwise tuple<T...>&&
      using copyable_reference = typename std::conditional<results_is_copyable,
          std::tuple<T...> const&,
          std::tuple<T...>&&
        >::type;

      persona_tls &tls = the_persona_tls;
      lpc_dormant *p = this;
      detail::raw_storage<std::tuple<T...>> storage;
      std::tuple<T...> *deserialized = nullptr;

      do {
        lpc_dormant *next = static_cast<lpc_dormant*>(p->intruder.p.load(std::memory_order_relaxed));
        std::uintptr_t vtbl_u = reinterpret_cast<std::uintptr_t>(p->vtbl);

        if(deserialized == nullptr && next != nullptr) {
          // multiple completions; deserialize into a new tuple and
          // then copy from there for each completion
          results.read_into_raw_tuple(static_cast<std::tuple<T...>*>(storage.raw()));
          deserialized = &storage.value();
        }

        if(vtbl_u & 0x1) {
          auto *pro = reinterpret_cast<future_header_promise<T...>*>(vtbl_u ^ 0x1);

          if(deserialized == nullptr)
            results.read_into_future_header_result(pro->base_header_result);
          else if(next == nullptr || !results_is_copyable)
            pro->base_header_result.construct_results(std::move(*deserialized));
          else
            pro->base_header_result.construct_results(static_cast<copyable_reference>(*deserialized));

          tls.enqueue_quiesced_promise(
            *p->target, p->level,
            /*move ref*/pro, /*result*/1 + /*anon*/0,
            /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
          );

          delete static_cast<lpc_dormant_qpromise<T...>*>(p);
        }
        else {
          auto *p1 = static_cast<lpc_dormant_fn_base<T...>*>(p);

          if(deserialized == nullptr)
            results.read_into_raw_tuple(&p1->results);
          else if(next == nullptr || !results_is_copyable)
            ::new(&p1->results) std::tuple<T...>(std::move(*deserialized));
          else
            ::new(&p1->results) std::tuple<T...>(static_cast<copyable_reference>(*deserialized));

          tls.enqueue(
            *p->target, p->level, p,
            /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
          );
        }

        p = next;

        if(!results_is_copyable)
          UPCXX_ASSERT(p == nullptr, "You have attempted to register multiple completions against a non-copyable results type.");
      }
      while(p != nullptr && results_is_copyable);

      if(deserialized) storage.destruct();
    }

    ////////////////////////////////////////////////////////////////////////////
    template<typename ...T>
    void lpc_dormant<T...>::cancel_and_delete(lpc_dormant<T...> *list) {
      lpc_dormant *p = list;
      do {
        lpc_dormant *next = static_cast<lpc_dormant*>(p->intruder.p.load(std::memory_order_relaxed));
        std::uintptr_t vtbl_u = reinterpret_cast<std::uintptr_t>(p->vtbl);
        if(vtbl_u & 0x1) {
          auto *pro = reinterpret_cast<future_header_promise<T...>*>(vtbl_u ^ 0x1);
          std::intptr_t depcount = pro->pro_meta.countdown;
          if (depcount > 1) { // this is a user-provided promise
            // balance injection increment and dropref:
            backend::fulfill_now(/*move ref*/pro, 1);
          } else {
            UPCXX_ASSERT(depcount == 1);
            // This can only be an implicitly created promise
            // However if it's non-empty then we cannot safely fulfill it without a value (issue #522)
            // So just drop the reference and allow it to be deleted unfulfilled.
            pro->dropref();
          }
          delete static_cast<lpc_dormant_qpromise<T...>*>(p);
        } else {
          auto *p1 = static_cast<lpc_dormant_fn_base<T...>*>(p);
          UPCXX_ASSERT(p1->vtbl);
          auto *cancel_and_delete = p1->vtbl->cancel_and_delete;
          if (cancel_and_delete) cancel_and_delete(p1);
          else delete p1;
        }
        p = next;
      } while(p != nullptr);
    }

  } // namespace detail
} // namespace upcxx
#endif
