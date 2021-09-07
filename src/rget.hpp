#ifndef _186bad93_2bea_4643_b31f_81839975287e
#define _186bad93_2bea_4643_b31f_81839975287e

#include <upcxx/backend.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/serialization.hpp>

// For the time being, our implementation of put/get requires the
// gasnet backend. Ideally we would detect gasnet via UPCXXI_BACKEND_GASNET
// and if not present, rely on a reference implementation over
// upcxx::backend generic API.
#include <upcxx/backend/gasnet/runtime.hpp>

namespace upcxx {
  namespace detail {
    ////////////////////////////////////////////////////////////////////
    // Calls gasnet get(). Fills in `cb->handle` and registers `cb` with
    // backend.
    
    enum class rma_get_done { none, operation };
    
    rma_get_done rma_get_nb(
      void *buf_d,
      intrank_t rank_s,
      const void *buf_s,
      std::size_t buf_size,
      backend::gasnet::handle_cb *cb
    );

    ////////////////////////////////////////////////////////////////////
    // Types used as `EventValues` in `detail::completions_state`.

    // by-reference rget always produces no-value events.
    struct rget_byref_event_values {
      template<typename Event>
      using tuple_t = std::tuple<>;
    };

    // by-value rget produces a T for operation_cx, no-value otherwise.
    template<typename T>
    struct rget_byval_event_values {
      template<typename Event>
      using tuple_t = typename std::conditional<
          std::is_same<Event, operation_cx_event>::value,
          std::tuple<T>,
          std::tuple<>
        >::type;
    };

    ////////////////////////////////////////////////////////////////////
    // rget_cb_remote: base class of rget_cb_{byref|byval} for handling
    // remote completion
    
    template<typename CxStateRemote,
             bool has_remote = !CxStateRemote::empty>
    struct rget_cb_remote;

    template<typename CxStateRemote>
    struct rget_cb_remote<CxStateRemote, /*has_remote=*/false> {
      rget_cb_remote(intrank_t, CxStateRemote) {}
      
      void send_remote() {/*nop*/}
    };

    template<typename CxStateRemote>
    struct rget_cb_remote<CxStateRemote, /*has_remote=*/true> {
      intrank_t rank_s;
      CxStateRemote state_remote;
      
      rget_cb_remote(intrank_t rank_s, CxStateRemote state_remote):
        rank_s{rank_s},
        state_remote{std::move(state_remote)} {

        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) += 1;
      }

      void send_remote() {
        backend::send_am_master<progress_level::user>( rank_s,
          state_remote.template bind_event<remote_cx_event>()
        );
        
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) -= 1;
      }
    };
    
    ////////////////////////////////////////////////////////////////////
    // rget_cb_byref: tracks status of of rget by-reference
    
    template<typename CxStateHere, typename CxStateRemote>
    struct rget_cb_byref final:
      rget_cb_remote<CxStateRemote>,
      backend::gasnet::handle_cb {
      
      CxStateHere state_here;
      
      rget_cb_byref(
          intrank_t rank_s,
          CxStateHere state_here,
          CxStateRemote state_remote
        ):
        rget_cb_remote<CxStateRemote>{rank_s, std::move(state_remote)},
        state_here{std::move(state_here)} {
      }

      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        this->send_remote();
        this->state_here.template operator()<operation_cx_event>();
        delete this;
      }
    };

    ////////////////////////////////////////////////////////////////////
    // rget_cb_byval: tracks status of of rget by-value
    
    template<typename T, typename CxStateHere, typename CxStateRemote>
    struct rget_cb_byval final:
      rget_cb_remote<CxStateRemote>,
      backend::gasnet::handle_cb {

      CxStateHere state_here;
      T buffer;
      
      rget_cb_byval(
          intrank_t rank_s,
          CxStateHere state_here,
          CxStateRemote state_remote
        ):
        rget_cb_remote<CxStateRemote>{rank_s, std::move(state_remote)},
        state_here{std::move(state_here)} {
      }

      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        this->send_remote();
        this->state_here.template operator()<operation_cx_event>(std::move(buffer));
        delete this;
      }

      #if 0 // Disable this, GASNet handles out-of-segment gets very well
        // we allocate this class in the segment for performance with gasnet
        static void* operator new(std::size_t size) {
          return upcxx::allocate(size, alignof(rget_cb_byval));
        }
        static void operator delete(void *p) {
          upcxx::deallocate(p);
        }
      #endif
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // rget
  
  template<typename T,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byval_event_values<T>,
      typename std::decay<Cxs>::type
    >::return_t
  rget(
      global_ptr<const T> gp_s,
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {

    using CxsDecayed = typename std::decay<Cxs>::type;
    namespace gasnet = upcxx::backend::gasnet;
    
    static_assert(
      is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types."
    );

    UPCXXI_STATIC_ASSERT_VALUE_SIZE(T, rget); // issue 392: prevent large types by-value

    UPCXX_ASSERT_ALWAYS(
      (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
      "Not requesting operation completion is surely an error. You'll have no "
      "way of ever knowing when then the source or target memory are safe to "
      "access again without incurring a data race."
    );
    /* rget supports remote completion, contrary to the spec */
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value),
      "rget does not support source completion."
    );
  
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_GPTR_CHK(gp_s);
    UPCXX_ASSERT(gp_s, "pointer arguments to rget may not be null");
    
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byval_event_values<T>,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rget_byval_event_values<T>,
      CxsDecayed>;
    
    using detail::rma_get_done;

    if (backend::rank_is_local(gp_s.UPCXXI_INTERNAL_ONLY(rank_))) {
      // local case does copy directly without involving backend
      T *buf_s_local = (T*) backend::localize_memory_nonnull(
        gp_s.UPCXXI_INTERNAL_ONLY(rank_),
        reinterpret_cast<std::uintptr_t>(gp_s.UPCXXI_INTERNAL_ONLY(raw_ptr_))
      );
      // data passed directly to operation-completion trigger below

      cxs_here_t cx_state_here(std::forward<Cxs>(cxs));
      detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byval_event_values<T>,
        CxsDecayed
        > returner(cx_state_here, detail::cx_event_done::operation);
      // no source completion
      if (!cxs_remote_t::empty) {
        backend::send_am_master<progress_level::user>(
          gp_s.UPCXXI_INTERNAL_ONLY(rank_),
          cxs_remote_t::template bind_event_static<remote_cx_event>(std::forward<Cxs>(cxs))
        );
      }
      cx_state_here.template operator()<operation_cx_event>(*buf_s_local);
      return returner();
    }
    
    auto *cb = new detail::rget_cb_byval<T,cxs_here_t,cxs_remote_t>{
      gp_s.UPCXXI_INTERNAL_ONLY(rank_),
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)}
    };
    
    rma_get_done done = detail::rma_get_nb(
      &cb->buffer, gp_s.UPCXXI_INTERNAL_ONLY(rank_),
      gp_s.UPCXXI_INTERNAL_ONLY(raw_ptr_), sizeof(T), cb
    );
    
    gasnet::handle_cb_queue &cb_q = gasnet::get_handle_cb_queue();
    
    // construct returner before post-injection actions potentially
    // destroy cb->state_here
    // we construct the returner after injection for symmetry with the
    // vector case; in the scalar case, the empty-future optimization
    // does not actually apply, but there isn't a downside to delaying
    // construction of the returner until after injection
    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byval_event_values<T>,
        CxsDecayed
      >{cb->state_here,
        done == rma_get_done::operation ?
        detail::cx_event_done::operation :
        detail::cx_event_done::none};

    switch(done) {
    case rma_get_done::none:
      cb_q.enqueue(cb);
      gasnet::after_gasnet();
      break;
      
    case rma_get_done::operation:
    default:
      cb_q.execute_outside(cb);
      break;
    }
    
    return returner();
  }
  
  template<typename T,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byref_event_values,
      typename std::decay<Cxs>::type
    >::return_t
  rget(
      global_ptr<const T> gp_s,
      T *buf_d, std::size_t n,
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {

    using CxsDecayed = typename std::decay<Cxs>::type;
    namespace gasnet = upcxx::backend::gasnet;
    
    static_assert(
      is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types."
    );

    UPCXX_ASSERT_ALWAYS(
      (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
      "Not requesting operation completion is surely an error. You'll have no "
      "way of ever knowing when then the source or target memory are safe to "
      "access again without incurring a data race."
    );
    /* rget supports remote completion, contrary to the spec */
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value),
      "rget does not support source completion."
    );
    
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_GPTR_CHK(gp_s);
    UPCXX_ASSERT(buf_d && gp_s, "pointer arguments to rget may not be null");
    UPCXXI_WARN_EMPTY("upcxx::rget", n);

    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;

    if (backend::rank_is_local(gp_s.UPCXXI_INTERNAL_ONLY(rank_))) {
      // local case does copy directly without involving backend
      void *buf_s_local = backend::localize_memory_nonnull(
        gp_s.UPCXXI_INTERNAL_ONLY(rank_),
        reinterpret_cast<std::uintptr_t>(gp_s.UPCXXI_INTERNAL_ONLY(raw_ptr_))
      );
      std::memcpy(buf_d, buf_s_local, n*sizeof(T));

      cxs_here_t cx_state_here(std::forward<Cxs>(cxs));
      detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byref_event_values,
        CxsDecayed
        > returner(cx_state_here, detail::cx_event_done::operation);
      // no source completion
      if (!cxs_remote_t::empty) {
        backend::send_am_master<progress_level::user>(
          gp_s.UPCXXI_INTERNAL_ONLY(rank_),
          cxs_remote_t::template bind_event_static<remote_cx_event>(std::forward<Cxs>(cxs))
        );
      }
      cx_state_here.template operator()<operation_cx_event>();
      return returner();
    }
    
    detail::rget_cb_byref<cxs_here_t,cxs_remote_t> cb(
      gp_s.UPCXXI_INTERNAL_ONLY(rank_),
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)}
    );
    
    using detail::rma_get_done;
    
    rma_get_done done = detail::rma_get_nb(
      buf_d, gp_s.UPCXXI_INTERNAL_ONLY(rank_),
      gp_s.UPCXXI_INTERNAL_ONLY(raw_ptr_), n*sizeof(T), &cb
    );
    
    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byref_event_values,
        CxsDecayed
      >{cb.state_here,
        done == rma_get_done::operation ?
        detail::cx_event_done::operation :
        detail::cx_event_done::none};

    switch(done) {
    case rma_get_done::none:
      gasnet::register_cb(new decltype(cb)(std::move(cb)));
      gasnet::after_gasnet();
      break;
      
    case rma_get_done::operation:
    default:
      cb.send_remote();
      cb.state_here.template operator()<operation_cx_event>();
      break;
    }
    
    return returner();
  }
}
#endif
