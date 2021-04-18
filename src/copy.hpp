#ifndef _f42075e8_08a8_4472_8972_3919ea92e6ff
#define _f42075e8_08a8_4472_8972_3919ea92e6ff

#include <upcxx/backend.hpp>
#include <upcxx/cuda.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/rput.hpp>
#include <upcxx/rget.hpp>

#include <functional>

#ifndef UPCXX_COPY_OPTIMIZEHOST
#define UPCXX_COPY_OPTIMIZEHOST 1 // host-only optimizations can be disabled for debugging library behavior
#endif
#ifndef UPCXX_COPY_PROMOTEPRIVATE
#define UPCXX_COPY_PROMOTEPRIVATE 1 // private promotion optimization can be disabled for debugging library behavior
#endif

namespace upcxx {
  namespace detail {
    void rma_copy_get(void *buf_d, intrank_t rank_s, void const *buf_s, std::size_t size, backend::gasnet::handle_cb *cb);
    void rma_copy_put(intrank_t rank_d, void *buf_d, void const *buf_s, std::size_t size, backend::gasnet::handle_cb *cb);
    void rma_copy_local(
        int heap_d, void *buf_d,
        int heap_s, void const *buf_s, std::size_t size,
        cuda::event_cb *cb
      );
    void rma_copy_remote(
        int heap_s, intrank_t rank_s, void const * buf_s,
        int heap_d, intrank_t rank_d, void * buf_d,
        std::size_t size,      
        backend::gasnet::handle_cb *cb
    );

    constexpr int host_heap = 0;
    constexpr int private_heap = -1;

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

      using cxs_remote_bound_t = decltype(std::declval<cxs_remote_t>().template bind_event<remote_cx_event>());
      using deserialized_cxs_remote_bound_t = deserialized_type_t<cxs_remote_bound_t>;

      static constexpr bool want_op = completions_has_event<CxsDecayed, operation_cx_event>::value;
      static constexpr bool want_remote = completions_has_event<CxsDecayed, remote_cx_event>::value;
      static constexpr bool want_source = completions_has_event<CxsDecayed, source_cx_event>::value;
      static constexpr bool want_initevt = want_op || want_source;

      static deserialized_cxs_remote_bound_t UPCXX_NOINLINE
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
    typename copy_traits<Cxs>::deserialized_cxs_remote_bound_t UPCXX_NOINLINE
    copy_traits<Cxs>::cxs_remote_deserialized_value(typename copy_traits<Cxs>::cxs_remote_bound_t const &cxs_remote) {
      return serialization_traits<typename copy_traits<Cxs>::cxs_remote_bound_t>::deserialized_value(cxs_remote);
    }

  // forward declaration
  template<typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_general(const int heap_s, const intrank_t rank_s, void *const buf_s,
               const int heap_d, const intrank_t rank_d, void *const buf_d,
               const std::size_t size, Cxs &&cxs);

  // special case: 3rd party copy
  template<bool HostOnly, typename Cxs>
  typename detail::copy_traits<Cxs>::return_t UPCXX_NOINLINE
  copy_3rdparty(const int heap_s, const intrank_t rank_s, void *const buf_s,
                const int heap_d, const intrank_t rank_d, void *const buf_d,
                const std::size_t size, Cxs &&cxs) {

    using copy_traits = detail::copy_traits<Cxs>;
    using deserialized_cxs_remote_bound_t = typename copy_traits::deserialized_cxs_remote_bound_t;

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
          if (HostOnly) {
            UPCXX_ASSERT(heap_s == detail::host_heap && heap_d == detail::host_heap);
            UPCXX_ASSERT(rank_d == upcxx::rank_me());
            global_ptr<char> src(detail::internal_only(), rank_s, reinterpret_cast<char*>(buf_s));
            f = upcxx::rget(src, reinterpret_cast<char*>(buf_d), size, operation_cx_as_internal_future );
          } else {
            f = detail::copy_general( heap_s, rank_s, buf_s,
                                      heap_d, rank_d, buf_d,
                                      size, operation_cx_as_internal_future );
          } 
          f.then([=]() {
            if (copy_traits::want_remote) {
              std::move(*cxs_remote_heaped)(); // deserialized_bound_function only invocable on an rvalue
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
      typename copy_traits::cxs_remote_t(std::forward<Cxs>(cxs)).template bind_event<remote_cx_event>())
    );
    // initiator
    if (!copy_traits::want_initevt) delete cxs_here;

    return returner();
  } // detail::copy_3rdparty

#if UPCXX_COPY_OPTIMIZEHOST
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
              typename copy_traits::cxs_remote_t(std::forward<Cxs>(cxs)).template bind_event<remote_cx_event>()
            ));
        std::move(cxs_remote)(); // deserialized_bound_function only invocable on an rvalue
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
              typename copy_traits::cxs_remote_t(std::forward<Cxs>(cxs)).template bind_event<remote_cx_event>()
            ));
      initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)++;
    }

    auto signal_completion = [=]() {
      cxs_here->template operator()<source_cx_event>();
      cxs_here->template operator()<operation_cx_event>();
      delete cxs_here;

      if (copy_traits::want_remote) {
        initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)--;
        std::move(*cxs_remote)(); // deserialized_bound_function only invocable on an rvalue
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
      detail::rma_copy_get(buf_d, rank_s, buf_s, size,
                  backend::gasnet::make_handle_cb(std::move(signal_completion)));
    #endif

    return returner();
  } // detail::copy_as_rget
#endif

  // detail::copy_general
  template<typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy_general(const int heap_s, const intrank_t rank_s, void *const buf_s,
               const int heap_d, const intrank_t rank_d, void *const buf_d,
               const std::size_t size, Cxs &&cxs) {

    #if UPCXX_COPY_OPTIMIZEHOST
      // only reach this function for calls involving device memory
      UPCXX_ASSERT(heap_s > 0 || heap_d > 0);
    #endif
    
    using copy_traits = detail::copy_traits<Cxs>;
    using deserialized_cxs_remote_bound_t = typename copy_traits::deserialized_cxs_remote_bound_t;

    const intrank_t initiator = upcxx::rank_me();
    if (initiator != rank_d && initiator != rank_s) { // 3rd party copy
      return copy_3rdparty</*HostOnly=*/false>(heap_s, rank_s, buf_s, 
                                               heap_d, rank_d, buf_d, size, std::forward<Cxs>(cxs));
    }

    auto cxs_here = new typename copy_traits::cxs_here_t(std::forward<Cxs>(cxs));
    typename copy_traits::cxs_remote_t cxs_remote(std::forward<Cxs>(cxs));

    persona *initiator_per = &upcxx::current_persona();

    auto returner = typename copy_traits::returner(*cxs_here);

    if (rank_d == rank_s) { // fully loopback on the calling process
      UPCXX_ASSERT(rank_d == initiator); 
      // Issue #421: synchronously deserialize remote completions into the heap to avoid a PGI optimizer problem
      deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
        copy_traits::want_remote ?
          new deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              cxs_remote.template bind_event<remote_cx_event>()
            )
          ) : nullptr);
      if (copy_traits::want_remote) initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)++;
      detail::rma_copy_local(heap_d, buf_d, heap_s, buf_s, size,
        cuda::make_event_cb([=]() {
          cxs_here->template operator()<source_cx_event>();
          cxs_here->template operator()<operation_cx_event>();
          delete cxs_here;
          if (copy_traits::want_remote) {
            initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)--;
            std::move(*cxs_remote_heaped)(); // deserialized_bound_function only invocable on an rvalue
            delete cxs_remote_heaped;
          }
        })
      );
    }
    else if (backend::heap_state::use_mk() && 
             rank_s == initiator && // MK put to different-rank
             ( ( copy_traits::want_remote && !copy_traits::want_op ) // RC but not OC
               || // using GDR and UPCXX_BUG4148_WORKAROUND
               ((heap_d > 0 || heap_s > 0) && backend::heap_state::bug4148_workaround())
             ) 
      ) { // convert MK put into MK get, either as an optimization or to avoid correctness bug 4148
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
                std::move(*cxs_remote_heaped)(); // deserialized_bound_function only invocable on an rvalue
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
        }, cxs_remote.template bind_event<remote_cx_event>()) // bind
      ); // AM to target

      // initiator
      if (!must_ack) delete cxs_here;
    }
    else if (backend::heap_state::use_mk()) { // MK-enabled GASNet backend
      // GASNet will do a direct source-to-dest memory transfer.
      // No bounce buffering, we just need to orchestrate the completions
      
      deserialized_cxs_remote_bound_t *cxs_remote_heaped_local = nullptr;
      using cxs_remote_am_t = decltype(backend::prepare_deferred_am_master(rank_d, 
                                       cxs_remote.template bind_event<remote_cx_event>()));
      cxs_remote_am_t *cxs_remote_am = nullptr;

      if (copy_traits::want_remote) {
        if (rank_d == initiator) { // in-place RC
          cxs_remote_heaped_local = new deserialized_cxs_remote_bound_t(
            copy_traits::cxs_remote_deserialized_value(
              cxs_remote.template bind_event<remote_cx_event>()
            ));
        } else { // initiator-chained RC, serialize remote_cx now to ensure synchronous source_cx for as_rpc arguments
          cxs_remote_am = new cxs_remote_am_t(backend::prepare_deferred_am_master(rank_d,
                                       cxs_remote.template bind_event<remote_cx_event>()));
        }

        initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)++;
      } // want_remote

      detail::rma_copy_remote(heap_s, rank_s, buf_s, heap_d, rank_d, buf_d, size,
        backend::gasnet::make_handle_cb([=]() {
              cxs_here->template operator()<source_cx_event>();
              cxs_here->template operator()<operation_cx_event>();
              delete cxs_here;
          
              if (copy_traits::want_remote) {
                initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)--;
                if (rank_d == initiator) { // in-place RC
                  std::move(*cxs_remote_heaped_local)(); // deserialized_bound_function only invocable on an rvalue
                  delete cxs_remote_heaped_local;
                } else { // initiator-chained RC
                  backend::send_prepared_am_master(progress_level::internal, rank_d, std::move(*cxs_remote_am));
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
              cxs_remote.template bind_event<remote_cx_event>()
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

      if (copy_traits::want_remote) initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)++;
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
                          initiator_per->UPCXX_INTERNAL_ONLY(undischarged_n_)--;
                          std::move(*cxs_remote_heaped)(); // deserialized_bound_function only invocable on an rvalue
                          delete cxs_remote_heaped;
                        }
                        cxs_here->template operator()<operation_cx_event>();
                        delete cxs_here;
                      };
                      
                      if(heap_d == host_heap)
                        bounce_d_cont();
                      else
                        detail::rma_copy_local(heap_d, buf_d, host_heap, bounce_d, size, cuda::make_event_cb(std::move(bounce_d_cont)));
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
              host_heap, bounce_s, heap_s, buf_s, size,
              cuda::make_event_cb(make_bounce_s_cont(bounce_s))
            );
          }
        }
      );
    }
    else {
      UPCXX_ASSERT(rank_s == initiator && rank_d != initiator);
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
                        std::move(*cxs_remote_heaped)(); // deserialized_bound_function only invocable on an rvalue
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
                      detail::rma_copy_local(heap_d, buf_d, host_heap, bounce_d, size, cuda::make_event_cb(std::move(bounce_d_cont)));
                  }) // make_handle_cb
                ); // rma_copy_get
              }, cxs_remote.template bind_event<remote_cx_event>()
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
        
        detail::rma_copy_local(host_heap, bounce_s, heap_s, buf_s, size, cuda::make_event_cb(make_bounce_s_cont(bounce_s)));
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
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, T *dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(src);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    using copy_traits = detail::copy_traits<Cxs>;
    copy_traits::template assert_sane<T>();

    #if UPCXX_COPY_OPTIMIZEHOST
      if (Ks == memory_kind::host || src.dynamic_kind() == memory_kind::host)
        return detail::copy_as_rget(
                 src.UPCXX_INTERNAL_ONLY(rank_),
                 src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 dest, n * sizeof(T), std::forward<Cxs>(cxs) );
      else
    #endif
      { int heap_d = detail::private_heap;
        intrank_t rank_d = upcxx::rank_me();
        T * buf_d = dest;
        #if UPCXX_COPY_PROMOTEPRIVATE
          if (src.UPCXX_INTERNAL_ONLY(rank_) != rank_d) { // not loopback (where promotion not profitable)
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
        return detail::copy_general( 
                 src.UPCXX_INTERNAL_ONLY(heap_idx_),
                 src.UPCXX_INTERNAL_ONLY(rank_),
                 src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 heap_d, rank_d, buf_d,
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      }
  }

  template<typename T, memory_kind Kd,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(T const *src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    detail::copy_traits<Cxs>::template assert_sane<T>();

    #if UPCXX_COPY_OPTIMIZEHOST
      if (Kd == memory_kind::host || dest.dynamic_kind() == memory_kind::host)
        return detail::copy_as_rput(
                 const_cast<T*>(src),
                 dest.UPCXX_INTERNAL_ONLY(rank_),
                 dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      else
    #endif
      { int heap_s = detail::private_heap;
        intrank_t rank_s = upcxx::rank_me();
        T * buf_s = const_cast<T*>(src);
        #if UPCXX_COPY_PROMOTEPRIVATE
          if (dest.UPCXX_INTERNAL_ONLY(rank_) != rank_s) { // not loopback (where promotion not profitable)
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
              else if (backend::heap_state::use_mk()) { // performance: peer-segment only profitable for MK, see above
                rank_s = p_rank; // a co-located peer
                buf_s = reinterpret_cast<T*>(p_raw);
                heap_s = detail::host_heap;
              }
            }
          }
        #endif
        return detail::copy_general( 
                 heap_s, rank_s, buf_s,
                 dest.UPCXX_INTERNAL_ONLY(heap_idx_),
                 dest.UPCXX_INTERNAL_ONLY(rank_),
                 dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 n * sizeof(T), std::forward<Cxs>(cxs) );
      }
  }
  
  template<typename T, memory_kind Ks, memory_kind Kd,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(src); UPCXX_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    using copy_traits = detail::copy_traits<Cxs>;
    copy_traits::template assert_sane<T>();

    #if UPCXX_COPY_OPTIMIZEHOST
      if ( ( Ks == memory_kind::host || src.dynamic_kind()  == memory_kind::host ) &&
           ( Kd == memory_kind::host || dest.dynamic_kind() == memory_kind::host ) ) {
        // generalized host-to-host copy
        // Here we use is_local/local to leverage shared-memory bypass for pointers that
        // happen to reference shared objects owned by a co-located peer.
        // puts are preferred over gets for more efficient mapping of completions
        if (src.is_local()) 
          return detail::copy_as_rput(
                 const_cast<T*>(src.local()),
                 dest.UPCXX_INTERNAL_ONLY(rank_),
                 dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 n * sizeof(T), std::forward<Cxs>(cxs) );
        else if (  ( !copy_traits::want_remote && dest.is_local() ) 
                || (  copy_traits::want_remote && dest.UPCXX_INTERNAL_ONLY(rank_) == upcxx::rank_me() ))
          return detail::copy_as_rget(
                 src.UPCXX_INTERNAL_ONLY(rank_),
                 src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 dest.local(), n * sizeof(T), std::forward<Cxs>(cxs) );
        else
          return detail::copy_3rdparty</*HostOnly=*/true>(
                 src.UPCXX_INTERNAL_ONLY(heap_idx_), src.UPCXX_INTERNAL_ONLY(rank_),
                 src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 dest.UPCXX_INTERNAL_ONLY(heap_idx_), dest.UPCXX_INTERNAL_ONLY(rank_),
                 dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 n*sizeof(T), std::forward<Cxs>(cxs) );
      } else
    #endif
        return detail::copy_general(
                 src.UPCXX_INTERNAL_ONLY(heap_idx_), src.UPCXX_INTERNAL_ONLY(rank_),
                 src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 dest.UPCXX_INTERNAL_ONLY(heap_idx_), dest.UPCXX_INTERNAL_ONLY(rank_),
                 dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                 n*sizeof(T), std::forward<Cxs>(cxs) );
  }

} // namespace upcxx
#endif
