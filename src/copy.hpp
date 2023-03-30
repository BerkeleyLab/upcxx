#ifndef _f42075e8_08a8_4472_8972_3919ea92e6ff
#define _f42075e8_08a8_4472_8972_3919ea92e6ff

#include <upcxx/backend.hpp>
#include <upcxx/cuda.hpp>
#include <upcxx/hip.hpp>
#include <upcxx/ze.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/rput.hpp>
#include <upcxx/rget.hpp>

#include <functional>

#ifndef UPCXXI_COPY_OPTIMIZEHOST
#define UPCXXI_COPY_OPTIMIZEHOST 1 // host-only optimizations can be disabled for debugging library behavior
#endif
#ifndef UPCXXI_COPY_PROMOTEPRIVATE
#define UPCXXI_COPY_PROMOTEPRIVATE 1 // private promotion optimization can be disabled for debugging library behavior
#endif

namespace upcxx {
  namespace detail {
    void rma_copy_get_nonlocal(void *buf_d, intrank_t rank_s, void const *buf_s, std::size_t size, backend::gasnet::handle_cb *cb);
    void rma_copy_get(void *buf_d, intrank_t rank_s, void const *buf_s, std::size_t size, backend::gasnet::handle_cb *cb);
    void rma_copy_put(intrank_t rank_d, void *buf_d, void const *buf_s, std::size_t size, backend::gasnet::handle_cb *cb);
    void rma_copy_remote(
        int heap_s, intrank_t rank_s, void const * buf_s,
        int heap_d, intrank_t rank_d, void * buf_d,
        std::size_t size,      
        backend::gasnet::handle_cb *cb
    );

    constexpr int host_heap = 0;
    constexpr int private_heap = -1;

    template<typename Fn>     
    inline void rma_copy_local(
      int heap_d, void *buf_d, memory_kind kind_d,
      int heap_s, void const *buf_s, memory_kind kind_s,
      std::size_t size, Fn &&fn) {
      UPCXX_ASSERT((kind_s == memory_kind::host) == (heap_s == host_heap || heap_s == private_heap));
      UPCXX_ASSERT((kind_d == memory_kind::host) == (heap_d == host_heap || heap_d == private_heap));
      
      if (kind_d == memory_kind::host && kind_s == memory_kind::host) {
        UPCXX_ASSERT((char*)buf_d + size <= buf_s || (char*)buf_s + size <= buf_d,
                     "Source and destination regions in upcxx::copy must not overlap");
        std::memcpy(buf_d, buf_s, size);
        fn();
        return;
      }

      // one or both sides on (same) device kind
      UPCXX_ASSERT(kind_s == kind_d || kind_d == memory_kind::host || kind_s == memory_kind::host);
      auto cb = backend::make_device_cb(std::forward<Fn>(fn));

      #if UPCXXI_CUDA_ENABLED
        if (kind_d == memory_kind::cuda_device || kind_s == memory_kind::cuda_device) {
          detail::cuda_copy_local(heap_d,buf_d,heap_s,buf_s,size,cb);
          return;
        }
      #endif
      #if UPCXXI_HIP_ENABLED
        if (kind_d == memory_kind::hip_device || kind_s == memory_kind::hip_device) {
          detail::hip_copy_local(heap_d,buf_d,heap_s,buf_s,size,cb);
          return;
        }
      #endif
      #if UPCXXI_ZE_ENABLED
        if (kind_d == memory_kind::ze_device || kind_s == memory_kind::ze_device) {
          detail::ze_copy_local(heap_d,buf_d,heap_s,buf_s,size,cb);
          return;
        }
      #endif

      UPCXXI_INVOKE_UB("Unrecognized device kinds in upcxx::copy() -- gptr corruption?");      
    }

    UPCXXI_ATTRIB_CONST inline bool native_gex_mk(memory_kind k) {
      switch (k) {
        case memory_kind::host:        
                     return true;
        #if UPCXXI_CUDA_ENABLED
          case memory_kind::cuda_device: 
                     return cuda_device::use_gex_mk(detail::internal_only());
        #endif
        #if UPCXXI_HIP_ENABLED
          case memory_kind::hip_device: 
                     return hip_device::use_gex_mk(detail::internal_only());
        #endif
        #if UPCXXI_ZE_ENABLED
          case memory_kind::ze_device: 
                     return ze_device::use_gex_mk(detail::internal_only());
        #endif
        default: // includes memory_kind::any
          UPCXXI_INVOKE_UB("Internal error, bad kind query: " << to_string(k));
      }
    }

    template<typename Cxs>
    struct copy_traits {
      using CxsDecayed = typename std::decay<Cxs>::type;

      using returner = typename detail::completions_returner<
            /*EventPredicate=*/detail::event_is_here,
            /*EventValues=*/detail::rput_event_values,
            CxsDecayed>;
      using return_t = typename returner::return_t;
    
      using cxs_here_t = detail::completions_state<
            /*EventPredicate=*/detail::event_is_here,
            /*EventValues=*/detail::rput_event_values,
            CxsDecayed>;
      using cxs_remote_t = detail::completions_state<
            /*EventPredicate=*/detail::event_is_remote,
            /*EventValues=*/detail::rput_event_values,
            CxsDecayed>;

      using cxs_remote_bound_t = decltype(cxs_remote_t::template bind_event_static<remote_cx_event>(std::declval<CxsDecayed>()));
      using deserialized_cxs_remote_bound_t = deserialized_type_t<cxs_remote_bound_t>;

      static constexpr auto bind_remote = cxs_remote_t::template bind_event_static<remote_cx_event>;

      static constexpr bool want_op = completions_has_event<CxsDecayed, operation_cx_event>::value;
      static constexpr bool want_remote = completions_has_event<CxsDecayed, remote_cx_event>::value;
      static constexpr bool want_source = completions_has_event<CxsDecayed, source_cx_event>::value;
      static constexpr bool want_initevt = want_op || want_source;

      static deserialized_cxs_remote_bound_t UPCXXI_ATTRIB_NOINLINE
      cxs_remote_deserialized_value(cxs_remote_bound_t const &cxs_remote);

      template<typename T>
      static void assert_sane() {
        static_assert(
          is_trivially_serializable<T>::value,
          "RMA operations only work on TriviallySerializable types."
        );

        UPCXX_ASSERT_ALWAYS((want_op || want_remote),
          "Not requesting either operation or remote completion is surely an "
          "error. You'll have no way of ever knowing when the target memory is "
          "safe to read or write again."
        );
      }
    }; // detail::copy_traits

    template<typename Cxs>
    typename copy_traits<Cxs>::deserialized_cxs_remote_bound_t UPCXXI_ATTRIB_NOINLINE
    copy_traits<Cxs>::cxs_remote_deserialized_value(typename copy_traits<Cxs>::cxs_remote_bound_t const &cxs_remote) {
      return serialization_traits<typename copy_traits<Cxs>::cxs_remote_bound_t>::deserialized_value(cxs_remote);
    }

  // forward declaration
  template<memory_kind Ks, memory_kind Kd, typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_general(const int heap_s, const intrank_t rank_s, void *const buf_s, memory_kind kind_s_,
               const int heap_d, const intrank_t rank_d, void *const buf_d, memory_kind kind_d_,
               const std::size_t size, Cxs &&cxs);

  // special case: 3rd party copy
  template<memory_kind Ks, memory_kind Kd, typename Cxs>
  typename detail::copy_traits<Cxs>::return_t UPCXXI_ATTRIB_NOINLINE
  copy_3rdparty(const int heap_s, const intrank_t rank_s, void *const buf_s, memory_kind kind_s_,
                const int heap_d, const intrank_t rank_d, void *const buf_d, memory_kind kind_d_,
                const std::size_t size, Cxs &&cxs) {

    using copy_traits = detail::copy_traits<Cxs>;
    using deserialized_cxs_remote_bound_t = typename copy_traits::deserialized_cxs_remote_bound_t;

    // dynamic_kind arguments are only used for kind::any
    const memory_kind kind_s = ( Ks == memory_kind::any ? kind_s_ : Ks);
    const memory_kind kind_d = ( Kd == memory_kind::any ? kind_d_ : Kd);
    UPCXX_ASSERT(kind_s != memory_kind::any); UPCXX_ASSERT(kind_d != memory_kind::any);

    const intrank_t initiator = upcxx::rank_me();
    persona *initiator_per = &upcxx::current_persona();

    auto cxs_here = new typename copy_traits::cxs_here_t(std::forward<Cxs>(cxs));
    auto returner = typename copy_traits::returner(*cxs_here);

    UPCXX_ASSERT(initiator != rank_d && initiator != rank_s);
    UPCXX_ASSERT(heap_s != detail::private_heap && heap_d != detail::private_heap);

    backend::send_am_master<progress_level::internal>( rank_d,
      detail::bind([=](deserialized_cxs_remote_bound_t &&cxs_remote_bound) {
        // at target
        auto operation_cx_as_internal_future = detail::operation_cx_as_internal_future_t{{}};
        deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
            copy_traits::want_remote ?
              new deserialized_cxs_remote_bound_t(std::move(cxs_remote_bound)) : nullptr);
         
          future<> f;
          if (kind_s == memory_kind::host && kind_d == memory_kind::host) { // host-only
            UPCXX_ASSERT(heap_s == detail::host_heap && heap_d == detail::host_heap);
            UPCXX_ASSERT(rank_d == upcxx::rank_me());
            global_ptr<char> src(detail::internal_only(), rank_s, reinterpret_cast<char*>(buf_s));
            f = upcxx::rget(src, reinterpret_cast<char*>(buf_d), size, operation_cx_as_internal_future );
          } else {
            f = detail::copy_general<Ks,Kd>( heap_s, rank_s, buf_s, kind_s,
                                             heap_d, rank_d, buf_d, kind_d,
                                             size, operation_cx_as_internal_future );
          } 
          f.then([=]() {
            if (copy_traits::want_remote) {
              detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped),
                                             /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
              delete cxs_remote_heaped;
            }

            if (copy_traits::want_initevt) {
              backend::send_am_persona<progress_level::internal>(
                initiator, initiator_per,
                [=]() {
                  // at initiator
                  cxs_here->template operator()<source_cx_event>();
                  cxs_here->template operator()<operation_cx_event>();
                  delete cxs_here;
                }
              );
            }
          });
      }, 
      copy_traits::bind_remote(std::forward<Cxs>(cxs)) )
    );
    // initiator
    if (!copy_traits::want_initevt) delete cxs_here;

    return returner();
  } // detail::copy_3rdparty

#if UPCXXI_COPY_OPTIMIZEHOST
  // special case: host-to-host copy-put
  template<typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_as_rput(void *const buf_s, 
               const intrank_t rank_d, void *const buf_d,
               const std::size_t size, Cxs &&cxs) {

    global_ptr<char> dst(detail::internal_only(), rank_d, reinterpret_cast<char*>(buf_d));
    return upcxx::rput<char>(reinterpret_cast<char*>(buf_s), dst, size, std::forward<Cxs>(cxs));
  } // detail::copy_as_rput

  // special case: host-to-host copy-get
  // Precondition: (want_remote && rank_d == rank_me) || (!want_remote && dest.is_local())
  template<typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_as_rget(const intrank_t rank_s, void *const buf_s,
               void *const buf_d,
               const std::size_t size, Cxs &&cxs) {

    using copy_traits = detail::copy_traits<Cxs>;

    if (backend::rank_is_local(rank_s)) { // fully local/synchronous case
      void *src = backend::localize_memory_nonnull(rank_s, reinterpret_cast<std::uintptr_t>(buf_s));
      std::memcpy(buf_d, src, size);

      // do the completions goop
      typename copy_traits::cxs_here_t cxs_here(std::forward<Cxs>(cxs));
      auto returner = typename copy_traits::returner(cxs_here);
      cxs_here.template operator()<source_cx_event>();
      cxs_here.template operator()<operation_cx_event>();
      if (copy_traits::want_remote) {
        typename copy_traits::deserialized_cxs_remote_bound_t cxs_remote(
            copy_traits::cxs_remote_deserialized_value(
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            ));
        detail::the_persona_tls.during(backend::master, progress_level::user, std::move(cxs_remote),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
      }

      return returner();
    } // fully local/synchronous case

    auto cxs_here = new typename copy_traits::cxs_here_t(std::forward<Cxs>(cxs));
    auto returner = typename copy_traits::returner(*cxs_here);

    persona *initiator_per = &upcxx::current_persona();

    typename copy_traits::deserialized_cxs_remote_bound_t *cxs_remote = nullptr;
    if (copy_traits::want_remote) {
      cxs_remote = new typename copy_traits::deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            ));
      initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)++;
    }

    auto signal_completion = [=]() {
      cxs_here->template operator()<source_cx_event>();
      cxs_here->template operator()<operation_cx_event>();
      delete cxs_here;

      if (copy_traits::want_remote) {
        initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)--;
        detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
        delete cxs_remote;
      }
    };
    #if 0
      // using rget directly works, but imposes some future overheads with no real benefit:
      auto operation_cx_as_internal_future = detail::operation_cx_as_internal_future_t{{}};
      global_ptr<char> src(detail::internal_only(), rank_s, reinterpret_cast<char*>(buf_s));
      upcxx::rget<char>(src, reinterpret_cast<char*>(buf_d), size, 
                      operation_cx_as_internal_future) // TODO: as_eager_future
        .then(std::move(signal_completion));
    #else
      // this is simpler and faster:
      detail::rma_copy_get_nonlocal(buf_d, rank_s, buf_s, size,
                  backend::gasnet::make_handle_cb(std::move(signal_completion)));
    #endif

    return returner();
  } // detail::copy_as_rget
#endif

  // detail::copy_general
  template<memory_kind Ks, memory_kind Kd, typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_general(const int heap_s, const intrank_t rank_s, void *const buf_s, memory_kind kind_s_,
               const int heap_d, const intrank_t rank_d, void *const buf_d, memory_kind kind_d_,
               const std::size_t size, Cxs &&cxs) {

    // dynamic_kind arguments are only used for kind::any
    const memory_kind kind_s = ( Ks == memory_kind::any ? kind_s_ : Ks);
    const memory_kind kind_d = ( Kd == memory_kind::any ? kind_d_ : Kd);
    UPCXX_ASSERT(kind_s != memory_kind::any); UPCXX_ASSERT(kind_d != memory_kind::any);

    #if UPCXXI_COPY_OPTIMIZEHOST
      // only reach this function for calls involving device memory
      UPCXX_ASSERT(heap_s > 0 || heap_d > 0);
      UPCXX_ASSERT(kind_s != memory_kind::host || kind_d != memory_kind::host);
    #endif
    
    using copy_traits = detail::copy_traits<Cxs>;
    using deserialized_cxs_remote_bound_t = typename copy_traits::deserialized_cxs_remote_bound_t;

    const intrank_t initiator = upcxx::rank_me();
    UPCXXI_IF_PF (initiator != rank_d && initiator != rank_s) { // 3rd party copy
      return copy_3rdparty<Ks,Kd>(heap_s, rank_s, buf_s, kind_s,
                                  heap_d, rank_d, buf_d, kind_d,
                                  size, std::forward<Cxs>(cxs));
    }

    auto cxs_here = new typename copy_traits::cxs_here_t(std::forward<Cxs>(cxs));

    persona *initiator_per = &upcxx::current_persona();

    auto returner = typename copy_traits::returner(*cxs_here);

    if (rank_d == rank_s) { // fully loopback on the calling process
      UPCXX_ASSERT(rank_d == initiator && rank_s == initiator); 
      #if UPCXXI_MANY_DEVICE_KINDS
        const bool dual_device_kind = (kind_s != kind_d && kind_s != memory_kind::host && kind_d != memory_kind::host);
      #else
        constexpr bool dual_device_kind = false;
      #endif
      // Issue #421: synchronously deserialize remote completions into the heap to avoid a PGI optimizer problem
      deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
        copy_traits::want_remote ?
          new deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            )
          ) : nullptr);
      if (copy_traits::want_remote) initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)++;
      auto completion =
        [=]() {
          cxs_here->template operator()<source_cx_event>();
          cxs_here->template operator()<operation_cx_event>();
          delete cxs_here;
          if (copy_traits::want_remote) {
            initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)--;
            detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped),
                                           /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
            delete cxs_remote_heaped;
          }
        };
      if (!dual_device_kind) { // easy/common case: at most one device kind
        detail::rma_copy_local(heap_d, buf_d, kind_d, 
                               heap_s, buf_s, kind_s, size, 
                               std::move(completion));
      } else { // hard/uncommon case: dual_device_kind
        // we don't currently have support for same-process direct copies between different kinds,
        // (and GASNet MK forbids same-process loopback) so we stage through host heap for this case. 
        // This case is not intended to be optimal (e.g. wrt source_cx), just correct.
        void *bounce = detail::alloc_aligned(size, 64);
        detail::rma_copy_local(private_heap, bounce, memory_kind::host, 
                               heap_s, buf_s, kind_s, size, 
          [=]() {
            detail::rma_copy_local(heap_d, buf_d, kind_d, 
                                   private_heap, bounce, memory_kind::host, size, 
                                   [=]() {
                                     std::free(bounce);
                                     completion();
                                   });
          });
      }
      return returner();
    }
    #if UPCXXI_GEX_MK_ALL // all devices using native kinds
      constexpr bool use_gex_mk = true;
    #elif UPCXXI_GEX_MK_ANY // devices using mix of native and reference kinds
      const bool use_gex_mk = native_gex_mk(kind_d) && native_gex_mk(kind_s);
    #else // all we have is reference kinds
      constexpr bool use_gex_mk = false;
    #endif
    if (  use_gex_mk && rank_s == initiator && // MK put to different-rank
        ( copy_traits::want_remote && !copy_traits::want_op ) // RC but not OC
      ) { // convert MK put into MK get, as an optimization to reduce completion latency
      UPCXX_ASSERT(rank_d != initiator);
      UPCXX_ASSERT(heap_d != private_heap);
      void *eff_buf_s = buf_s;
      int eff_heap_s = heap_s;
      void *bounce_s = nullptr;
      bool must_ack = copy_traits::want_op; // must_ack is true iff initiator_per is awaiting an event
      if (heap_s == private_heap) {
        // must use a bounce buffer to make the source remotely accessible
        bounce_s = backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
        std::memcpy(bounce_s, buf_s, size);
        eff_buf_s = bounce_s;
        eff_heap_s = host_heap;
        // we can signal source_cx as soon as it's populated
        cxs_here->template operator()<source_cx_event>();
      } else must_ack |= copy_traits::want_source;

      backend::send_am_master<progress_level::internal>( rank_d,
        detail::bind([=](deserialized_cxs_remote_bound_t &&cxs_remote_bound) {
          // at target
          deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
            copy_traits::want_remote ?
               new deserialized_cxs_remote_bound_t(std::move(cxs_remote_bound)) : nullptr);

          detail::rma_copy_remote(eff_heap_s, rank_s, eff_buf_s, heap_d, rank_d, buf_d, size,
            backend::gasnet::make_handle_cb([=]() {
              // RMA complete at target
              if (copy_traits::want_remote) {
                detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
                delete cxs_remote_heaped;
              }

              if (copy_traits::want_op || must_ack) {
                 backend::send_am_persona<progress_level::internal>(
                   rank_s, initiator_per,
                   [=]() {
                     // back at initiator
                     if (bounce_s) backend::gasnet::deallocate(bounce_s, &backend::gasnet::sheap_footprint_rdzv);
                     else cxs_here->template operator()<source_cx_event>();
                     cxs_here->template operator()<operation_cx_event>();
                     delete cxs_here;
                   }); // AM to initiator
              } else if (bounce_s) {
                 // issue #432: initiator persona might be defunct, just need to free the bounce buffer
                 backend::gasnet::send_am_restricted( rank_s,
                   [=]() { backend::gasnet::deallocate(bounce_s, &backend::gasnet::sheap_footprint_rdzv); }
                 );
              }
            }) // gasnet::make_handle_cb
          ); // rma_copy_remote
        }, 
        copy_traits::bind_remote(std::forward<Cxs>(cxs)) ) // bind
      ); // AM to target

      // initiator
      if (!must_ack) delete cxs_here;
    }
    else if (use_gex_mk) { // MK-enabled GASNet backend
      // GASNet will do a direct source-to-dest memory transfer.
      // No bounce buffering, we just need to orchestrate the completions
      
      deserialized_cxs_remote_bound_t *cxs_remote_heaped_local = nullptr;
      using cxs_remote_am_t = decltype(backend::prepare_deferred_am_master(rank_d, 
                                       std::declval<typename copy_traits::cxs_remote_bound_t>()));
      cxs_remote_am_t *cxs_remote_am = nullptr;

      if (copy_traits::want_remote) {
        if (rank_d == initiator) { // in-place RC
          cxs_remote_heaped_local = new deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            ));
        } else { // initiator-chained RC, serialize remote_cx now to ensure synchronous source_cx for as_rpc arguments
          cxs_remote_am = new cxs_remote_am_t(backend::prepare_deferred_am_master(rank_d,
                              copy_traits::bind_remote(std::forward<Cxs>(cxs)) ));
        }

        initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)++;
      } // want_remote

      detail::rma_copy_remote(heap_s, rank_s, buf_s, heap_d, rank_d, buf_d, size,
        backend::gasnet::make_handle_cb([=]() {
              cxs_here->template operator()<source_cx_event>();
              cxs_here->template operator()<operation_cx_event>();
              delete cxs_here;
          
              if (copy_traits::want_remote) {
                initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)--;
                if (rank_d == initiator) { // in-place RC
                  detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped_local),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
                  delete cxs_remote_heaped_local;
                } else { // initiator-chained RC
                  backend::send_prepared_am_master(progress_level::user, rank_d, std::move(*cxs_remote_am));
                  delete cxs_remote_am;
                }
              } // want_remote
        })
      );
    }
    else if(rank_d == initiator) {
      UPCXX_ASSERT(rank_s != initiator);
      UPCXX_ASSERT(heap_s != private_heap);
      deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
        copy_traits::want_remote ?
          new deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            )
          ) : nullptr);
      
      /* We are the destination, so semantically like a GET, even though a PUT
       * is used to transfer on the network
       */
      void *bounce_d;
      if(heap_d == host_heap)
        bounce_d = buf_d;
      else {
        bounce_d = backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
      }

      if (copy_traits::want_remote) initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)++;
      backend::send_am_master<progress_level::internal>( rank_s,
        [=]() {
          auto make_bounce_s_cont = [=](void *bounce_s) {
            return [=]() {
              detail::rma_copy_put(rank_d, bounce_d, bounce_s, size,
              backend::gasnet::make_handle_cb([=]() {
                  if (heap_s != host_heap)
                    backend::gasnet::deallocate(bounce_s, &backend::gasnet::sheap_footprint_rdzv);
                  
                  backend::send_am_persona<progress_level::internal>(
                    rank_d, initiator_per,
                    [=]() {
                      // at initiator
                      cxs_here->template operator()<source_cx_event>();
                      
                      auto bounce_d_cont = [=]() {
                        if (heap_d != host_heap)
                          backend::gasnet::deallocate(bounce_d, &backend::gasnet::sheap_footprint_rdzv);

                        if (copy_traits::want_remote) {
                          initiator_per->UPCXXI_INTERNAL_ONLY(undischarged_n_)--;
                          detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
                          delete cxs_remote_heaped;
                        }
                        cxs_here->template operator()<operation_cx_event>();
                        delete cxs_here;
                      };
                      
                      if(heap_d == host_heap)
                        bounce_d_cont();
                      else
                        detail::rma_copy_local(heap_d, buf_d, kind_d, 
                                               host_heap, bounce_d, memory_kind::host, 
                                               size, std::move(bounce_d_cont));
                    }
                  );
                })
              );
            };
          };
          
          if (heap_s == host_heap)
            make_bounce_s_cont(buf_s)();
          else {
            void *bounce_s = backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
            
            detail::rma_copy_local(
              host_heap, bounce_s, memory_kind::host,
              heap_s, buf_s, kind_s,
              size, make_bounce_s_cont(bounce_s)
            );
          }
        }
      );
    }
    else {
      UPCXX_ASSERT(rank_s == initiator);
      UPCXX_ASSERT(rank_d != initiator);
      UPCXX_ASSERT(heap_d != private_heap);
      /* We are the source, so semantically this is a PUT even though we use a
       * GET to transfer over network.
       */
      // must_ack is true iff initiator_per is left awaiting an event
      const bool must_ack = copy_traits::want_op || (copy_traits::want_source && heap_s == host_heap);

      // this lambda runs synchronously to serialize remote_cx and generate the AM payload we'll eventually send
      auto make_am = [&](void *bounce_s) {
        return backend::prepare_deferred_am_master(rank_d,
            detail::bind(
              [=](deserialized_cxs_remote_bound_t &&cxs_remote_bound) {
                // at target
                void *bounce_d = heap_d == host_heap ? buf_d : backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
                deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
                  copy_traits::want_remote ?
                    new deserialized_cxs_remote_bound_t(std::move(cxs_remote_bound)) : nullptr);
                
                detail::rma_copy_get(bounce_d, rank_s, bounce_s, size,
                  backend::gasnet::make_handle_cb([=]() {
                    auto bounce_d_cont = [=]() {
                      if (heap_d != host_heap)
                        backend::gasnet::deallocate(bounce_d, &backend::gasnet::sheap_footprint_rdzv);
                      
                      if (copy_traits::want_remote) {
                        detail::the_persona_tls.during(backend::master, progress_level::user, std::move(*cxs_remote_heaped),
                                       /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>());
                        delete cxs_remote_heaped;
                      }

                      if (must_ack) {
                        backend::send_am_persona<progress_level::internal>(
                          rank_s, initiator_per,
                          [=]() {
                            // at initiator
                            if (heap_s != host_heap)
                              backend::gasnet::deallocate(bounce_s, &backend::gasnet::sheap_footprint_rdzv);
                            else {
                              // source didnt use bounce buffer, need to source_cx now
                              cxs_here->template operator()<source_cx_event>();
                            }
                            cxs_here->template operator()<operation_cx_event>();
                          
                            delete cxs_here;
                          }
                        );
                      } else if (heap_s != host_heap) {
                        // issue #432: initiator persona might be defunct, just need to free the bounce buffer
                       backend::gasnet::send_am_restricted( rank_s,
                          [=]() { backend::gasnet::deallocate(bounce_s, &backend::gasnet::sheap_footprint_rdzv); }
                       );
                      }
                    }; // bounce_d_cont
                    
                    if(heap_d == host_heap)
                      bounce_d_cont();
                    else
                      detail::rma_copy_local(heap_d, buf_d, kind_d,
                                             host_heap, bounce_d, memory_kind::host,
                                             size, std::move(bounce_d_cont));
                  }) // make_handle_cb
                ); // rma_copy_get
              }, 
              copy_traits::bind_remote(std::forward<Cxs>(cxs))
            ) // bind
        ); // prepare_deferred_am_master
      }; // make_am

      // this lambda runs synchronously to generate a continuation that will run once the source is in host segment
      auto make_bounce_s_cont = [&](void *bounce_s) {
        using am_buf_t = decltype(make_am(nullptr));
        am_buf_t *am_buf_heaped = new am_buf_t(make_am(bounce_s)); // serialize
        return [=]() {
          if(copy_traits::want_source && heap_s != host_heap) {
            // since source side has a bounce buffer, we can signal source_cx as soon
            // as its populated
            cxs_here->template operator()<source_cx_event>();
          }
          backend::send_prepared_am_master(progress_level::internal, rank_d, std::move(*am_buf_heaped));
          delete am_buf_heaped;
        }; // make_bounce_s_cont lambda
      }; // make_bounce_s_cont

      if(heap_s == host_heap)
        make_bounce_s_cont(buf_s)();
      else {
        void *bounce_s = backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
        
        detail::rma_copy_local(host_heap, bounce_s, memory_kind::host,
                               heap_s, buf_s, kind_s,
                               size, make_bounce_s_cont(bounce_s));
      }

      if (!must_ack) delete cxs_here;
    } // copy case

    return returner();
  }
 } // namespace detail
 
 // ----------------------------------------------------------------------------------
 // public upcxx::copy entry points

  template<typename T, memory_kind Ks,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, T *dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) noexcept {
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(src);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    UPCXXI_WARN_EMPTY("upcxx::copy", n);
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    using copy_traits = detail::copy_traits<Cxs>;
    copy_traits::template assert_sane<T>();

    const memory_kind kind_s = ( Ks == memory_kind::any ? src.dynamic_kind() : Ks);

    #if UPCXXI_COPY_OPTIMIZEHOST
      if (kind_s == memory_kind::host)
        return detail::copy_as_rget(
                 src.UPCXXI_INTERNAL_ONLY(rank_),
                 src.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 dest, n * sizeof(T), std::forward<Cxs>(cxs) );
      else
    #endif
      { int heap_d = detail::private_heap;
        intrank_t rank_d = upcxx::rank_me();
        T * buf_d = dest;
        #if UPCXXI_COPY_PROMOTEPRIVATE
          if (src.UPCXXI_INTERNAL_ONLY(rank_) != rank_d) { // not loopback (where promotion not profitable)
            // upcxx::try_global_ptr(buf_d), with less overheads
            intrank_t p_rank;
            std::uintptr_t p_raw;
            std::tie(p_rank, p_raw) = backend::globalize_memory(buf_d, std::make_tuple(0, 0x0));
            // Performance tuning of Private Promotion (Remote GPU to Local Host) "get-like"
            // * Once we've paid the cost of the promotion check, private
            //   promotion to self-segment is never harmful and often helpful.
            // * Currently private promotion to a local_team peer segment is never
            //   profitable for "get-like" copy, because it activates 3rd party copy
            //   and triggers two extra AM hops in the critical path.
            if (p_raw) { // promotion succeeded
              #if 1
                if (rank_d == p_rank) { // performance, see above
                  UPCXX_ASSERT(buf_d == reinterpret_cast<T*>(p_raw));
                  heap_d = detail::host_heap;
                 }
              #else
                if ( !copy_traits::want_remote || rank_d == p_rank ) { // correctness: can't promote to co-located peer with RC
                  rank_d = p_rank; // possibly a co-located peer
                  buf_d = reinterpret_cast<T*>(p_raw);
                  heap_d = detail::host_heap;
                }
              #endif
            }
          }
        #endif
        return detail::copy_general<Ks,memory_kind::host>( 
                 src.UPCXXI_INTERNAL_ONLY(heap_idx_),
                 src.UPCXXI_INTERNAL_ONLY(rank_),
                 src.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 kind_s,
                 heap_d, rank_d, buf_d, memory_kind::host,
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      }
  }

  template<typename T, memory_kind Kd,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(T const *src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) noexcept {
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    UPCXXI_WARN_EMPTY("upcxx::copy", n);
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    detail::copy_traits<Cxs>::template assert_sane<T>();

    const memory_kind kind_d = ( Kd == memory_kind::any ? dest.dynamic_kind() : Kd);

    #if UPCXXI_COPY_OPTIMIZEHOST
      if (kind_d == memory_kind::host)
        return detail::copy_as_rput(
                 const_cast<T*>(src),
                 dest.UPCXXI_INTERNAL_ONLY(rank_),
                 dest.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      else
    #endif
      { int heap_s = detail::private_heap;
        intrank_t rank_s = upcxx::rank_me();
        T * buf_s = const_cast<T*>(src);
        #if UPCXXI_COPY_PROMOTEPRIVATE
          if (dest.UPCXXI_INTERNAL_ONLY(rank_) != rank_s) { // not loopback (where promotion not profitable)
            // upcxx::try_global_ptr(buf_s), with less overheads
            intrank_t p_rank;
            std::uintptr_t p_raw;
            std::tie(p_rank, p_raw) = backend::globalize_memory(buf_s, std::make_tuple(0, 0x0));
            // Performance tuning of Private Promotion (Local Host to Remote GPU) "put-like"
            // * Once we've paid the cost of the promotion check, private
            //   promotion to self-segment is never harmful and often helpful.
            // * Currently private promotion to a local_team peer segment is only
            //   profitable for "put-like" copy using GEX memory kinds (where put-as-get already
            //   imposes 2 extra AMs and promotion saves a local-side bounce buffer alloc/copy/free).
            // * Promotion to peer segment for reference kinds hurts performance, because it activates 
            //   3rd party copy and triggers two extra AM hops in the critical path.
            if (p_raw) { // promotion succeeded
              if (rank_s == p_rank) { // self-segment, never harmful, often helpful
                UPCXX_ASSERT(buf_s == reinterpret_cast<T*>(p_raw));
                heap_s = detail::host_heap;
              }
              else if (detail::native_gex_mk(kind_d)) { // performance: peer-segment only profitable for MK, see above
                rank_s = p_rank; // a co-located peer
                buf_s = reinterpret_cast<T*>(p_raw);
                heap_s = detail::host_heap;
              }
            }
          }
        #endif
        return detail::copy_general<memory_kind::host,Kd>( 
                 heap_s, rank_s, buf_s, memory_kind::host,
                 dest.UPCXXI_INTERNAL_ONLY(heap_idx_),
                 dest.UPCXXI_INTERNAL_ONLY(rank_),
                 dest.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 kind_d,
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      }
  }
  
  template<typename T, memory_kind Ks, memory_kind Kd,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) noexcept {
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(src); UPCXXI_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    UPCXXI_WARN_EMPTY("upcxx::copy", n);
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    using copy_traits = detail::copy_traits<Cxs>;
    copy_traits::template assert_sane<T>();

    const memory_kind kind_s = ( Ks == memory_kind::any ? src.dynamic_kind()  : Ks);
    const memory_kind kind_d = ( Kd == memory_kind::any ? dest.dynamic_kind() : Kd);

    #if UPCXXI_COPY_OPTIMIZEHOST
      if ( kind_s == memory_kind::host && kind_d == memory_kind::host ) {
        // generalized host-to-host copy
        // Here we use is_local/local to leverage shared-memory bypass for pointers that
        // happen to reference shared objects owned by a co-located peer.
        // puts are preferred over gets for more efficient mapping of completions
        if (src.is_local()) 
          return detail::copy_as_rput(
                 const_cast<T*>(src.local()),
                 dest.UPCXXI_INTERNAL_ONLY(rank_),
                 dest.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 n * sizeof(T), std::forward<Cxs>(cxs) );
        else if (  ( !copy_traits::want_remote && dest.is_local() ) 
                || (  copy_traits::want_remote && dest.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me() ))
          return detail::copy_as_rget(
                 src.UPCXXI_INTERNAL_ONLY(rank_),
                 src.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 dest.local(), n * sizeof(T), std::forward<Cxs>(cxs) );
        else
          return detail::copy_3rdparty<memory_kind::host,memory_kind::host>(
                 src.UPCXXI_INTERNAL_ONLY(heap_idx_), src.UPCXXI_INTERNAL_ONLY(rank_),
                 src.UPCXXI_INTERNAL_ONLY(raw_ptr_), kind_s,
                 dest.UPCXXI_INTERNAL_ONLY(heap_idx_), dest.UPCXXI_INTERNAL_ONLY(rank_),
                 dest.UPCXXI_INTERNAL_ONLY(raw_ptr_), kind_d,
                 n*sizeof(T), std::forward<Cxs>(cxs) );
      } else
    #endif
        return detail::copy_general<Ks,Kd>(
                 src.UPCXXI_INTERNAL_ONLY(heap_idx_), src.UPCXXI_INTERNAL_ONLY(rank_),
                 src.UPCXXI_INTERNAL_ONLY(raw_ptr_), kind_s,
                 dest.UPCXXI_INTERNAL_ONLY(heap_idx_), dest.UPCXXI_INTERNAL_ONLY(rank_),
                 dest.UPCXXI_INTERNAL_ONLY(raw_ptr_), kind_d,
                 n*sizeof(T), std::forward<Cxs>(cxs) );
  }

} // namespace upcxx
#endif
