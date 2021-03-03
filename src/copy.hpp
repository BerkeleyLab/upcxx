#ifndef _f42075e8_08a8_4472_8972_3919ea92e6ff
#define _f42075e8_08a8_4472_8972_3919ea92e6ff

#include <upcxx/backend.hpp>
#include <upcxx/cuda.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/rput.hpp>

#include <functional>

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

      using return_t = typename detail::completions_returner<
            /*EventPredicate=*/detail::event_is_here,
            /*EventValues=*/detail::rput_event_values,
            typename std::decay<Cxs>::type
          >::return_t;
    
      static constexpr bool want_op = completions_has_event<typename std::decay<Cxs>::type, operation_cx_event>::value;
      static constexpr bool want_remote = completions_has_event<typename std::decay<Cxs>::type, remote_cx_event>::value;
      static constexpr bool want_source = completions_has_event<typename std::decay<Cxs>::type, source_cx_event>::value;
      static constexpr bool want_initevt = want_op || want_source;

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
    };

    template<typename Cxs>
    typename detail::copy_traits<Cxs>::return_t
    copy(const int heap_s, const intrank_t rank_s, void *const buf_s,
         const int heap_d, const intrank_t rank_d, void *const buf_d,
         const std::size_t size, Cxs &&cxs);

  } // detail

  
  template<typename T, memory_kind Ks,
           typename Cxs = completions<future_cx<operation_cx_event>>>
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, T *dest, std::size_t n,
       Cxs &&cxs=completions<future_cx<operation_cx_event>>{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(src);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    detail::copy_traits<Cxs>::template assert_sane<T>();
    return detail::copy( src.UPCXX_INTERNAL_ONLY(heap_idx_),
                         src.UPCXX_INTERNAL_ONLY(rank_),
                         src.UPCXX_INTERNAL_ONLY(raw_ptr_),
                         detail::private_heap, upcxx::rank_me(), dest,
                         n * sizeof(T), std::forward<Cxs>(cxs) );
  }

  template<typename T, memory_kind Kd,
           typename Cxs = completions<future_cx<operation_cx_event>>>
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(T const *src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=completions<future_cx<operation_cx_event>>{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    detail::copy_traits<Cxs>::template assert_sane<T>();
    return detail::copy( detail::private_heap, upcxx::rank_me(), const_cast<T*>(src),
                         dest.UPCXX_INTERNAL_ONLY(heap_idx_),
                         dest.UPCXX_INTERNAL_ONLY(rank_),
                         dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
                         n * sizeof(T), std::forward<Cxs>(cxs) );
  }
  
  template<typename T, memory_kind Ks, memory_kind Kd,
           typename Cxs = completions<future_cx<operation_cx_event>>>
  UPCXX_NODISCARD
  inline
  typename detail::copy_traits<Cxs>::return_t
  copy(global_ptr<const T,Ks> src, global_ptr<T,Kd> dest, std::size_t n,
       Cxs &&cxs=completions<future_cx<operation_cx_event>>{{}}) {
    UPCXX_ASSERT_INIT();
    UPCXX_GPTR_CHK(src); UPCXX_GPTR_CHK(dest);
    UPCXX_ASSERT(src && dest, "pointer arguments to copy may not be null");
    detail::copy_traits<Cxs>::template assert_sane<T>();
    return detail::copy(
      src.UPCXX_INTERNAL_ONLY(heap_idx_), src.UPCXX_INTERNAL_ONLY(rank_),
      src.UPCXX_INTERNAL_ONLY(raw_ptr_),
      dest.UPCXX_INTERNAL_ONLY(heap_idx_), dest.UPCXX_INTERNAL_ONLY(rank_),
      dest.UPCXX_INTERNAL_ONLY(raw_ptr_),
      n*sizeof(T), std::forward<Cxs>(cxs)
    );
  }

 namespace detail {
  template<typename Cxs>
  typename detail::copy_traits<Cxs>::return_t
  copy(const int heap_s, const intrank_t rank_s, void *const buf_s,
       const int heap_d, const intrank_t rank_d, void *const buf_d,
       const std::size_t size, Cxs &&cxs) {
    
    using CxsDecayed = typename std::decay<Cxs>::type;
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using copy_traits = detail::copy_traits<Cxs>;

    cxs_here_t *cxs_here = new cxs_here_t(std::forward<Cxs>(cxs));
    cxs_remote_t cxs_remote(std::forward<Cxs>(cxs));

    using cxs_remote_bound_t = decltype(cxs_remote.template bind_event<remote_cx_event>());
    using deserialized_cxs_remote_bound_t = deserialized_type_t<cxs_remote_bound_t>;

    persona *initiator_per = &upcxx::current_persona();
    const intrank_t initiator = upcxx::rank_me();

    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rput_event_values,
        CxsDecayed
      >(*cxs_here);

    if (initiator != rank_d && initiator != rank_s) { // 3rd party copy
      UPCXX_ASSERT(heap_s != detail::private_heap && heap_d != detail::private_heap);
      
      backend::send_am_master<progress_level::internal>( rank_d,
        upcxx::bind([=](deserialized_cxs_remote_bound_t &&cxs_remote_bound) {
          // at target
          auto operation_cx_as_internal_future = upcxx::completions<upcxx::future_cx<upcxx::operation_cx_event, progress_level::internal>>{{}};
          deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
            copy_traits::want_remote ?
              new deserialized_cxs_remote_bound_t(std::move(cxs_remote_bound)) : nullptr);
          
          detail::copy( heap_s, rank_s, buf_s,
                        heap_d, rank_d, buf_d,
                        size, operation_cx_as_internal_future )
          .then([=]() {
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
        }, cxs_remote.template bind_event<remote_cx_event>())
      );
      // initiator
      if (!copy_traits::want_initevt) delete cxs_here;
    }
    else if(rank_d == rank_s) { // fully loopback on the calling process
      UPCXX_ASSERT(rank_d == initiator); 
      // Issue #421: synchronously deserialize remote completions into the heap to avoid a PGI optimizer problem
      deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
        copy_traits::want_remote ?
          new deserialized_cxs_remote_bound_t(
            serialization_traits<cxs_remote_bound_t>::deserialized_value(
              cxs_remote.template bind_event<remote_cx_event>()
            )
          ) : nullptr);
      if (copy_traits::want_remote) initiator_per->undischarged_n_++;
      detail::rma_copy_local(heap_d, buf_d, heap_s, buf_s, size,
        cuda::make_event_cb([=]() {
          cxs_here->template operator()<source_cx_event>();
          cxs_here->template operator()<operation_cx_event>();
          delete cxs_here;
          if (copy_traits::want_remote) {
            initiator_per->undischarged_n_--;
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
        upcxx::bind([=](deserialized_cxs_remote_bound_t &&cxs_remote_bound) {
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
            serialization_traits<cxs_remote_bound_t>::deserialized_value(
              cxs_remote.template bind_event<remote_cx_event>()
            ));
        } else { // initiator-chained RC, serialize remote_cx now to ensure synchronous source_cx for as_rpc arguments
          cxs_remote_am = new cxs_remote_am_t(backend::prepare_deferred_am_master(rank_d,
                                       cxs_remote.template bind_event<remote_cx_event>()));
        }

        initiator_per->undischarged_n_++;
      } // want_remote

      detail::rma_copy_remote(heap_s, rank_s, buf_s, heap_d, rank_d, buf_d, size,
        backend::gasnet::make_handle_cb([=]() {
          // issue #423: Ensure completion is delivered to the correct persona
          detail::the_persona_tls.during(*initiator_per, progress_level::internal,
            [=]() {
              cxs_here->template operator()<source_cx_event>();
              cxs_here->template operator()<operation_cx_event>();
              delete cxs_here;
          
              if (copy_traits::want_remote) {
                initiator_per->undischarged_n_--;
                if (rank_d == initiator) { // in-place RC
                  std::move(*cxs_remote_heaped_local)(); // deserialized_bound_function only invocable on an rvalue
                  delete cxs_remote_heaped_local;
                } else { // initiator-chained RC
                  backend::send_prepared_am_master(progress_level::internal, rank_d, std::move(*cxs_remote_am));
                  delete cxs_remote_am;
                }
              } // want_remote
            }, /*known_active=*/std::false_type()); // during(initiator_per,internal)
        })
      );
    }
    else if(rank_d == initiator) {
      UPCXX_ASSERT(rank_s != initiator);
      UPCXX_ASSERT(heap_s != private_heap);
      deserialized_cxs_remote_bound_t *cxs_remote_heaped = (
        copy_traits::want_remote ?
          new deserialized_cxs_remote_bound_t(
            serialization_traits<cxs_remote_bound_t>::deserialized_value(
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

      if (copy_traits::want_remote) initiator_per->undischarged_n_++;
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
                          initiator_per->undischarged_n_--;
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
      auto make_bounce_s_cont = [&](void *bounce_s) {
        return [=]() {
          if(copy_traits::want_source && heap_s != host_heap) {
            // since source side has a bounce buffer, we can signal source_cx as soon
            // as its populated
            cxs_here->template operator()<source_cx_event>();
          }
          
          backend::send_am_master<progress_level::internal>( rank_d,
            upcxx::bind(
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
                    };
                    
                    if(heap_d == host_heap)
                      bounce_d_cont();
                    else
                      detail::rma_copy_local(heap_d, buf_d, host_heap, bounce_d, size, cuda::make_event_cb(std::move(bounce_d_cont)));
                  })
                );
              }, cxs_remote.template bind_event<remote_cx_event>()
            )
          );
        };
      };

      if(heap_s == host_heap)
        make_bounce_s_cont(buf_s)();
      else {
        void *bounce_s = backend::gasnet::allocate(size, 64, &backend::gasnet::sheap_footprint_rdzv);
        
        detail::rma_copy_local(host_heap, bounce_s, heap_s, buf_s, size, cuda::make_event_cb(make_bounce_s_cont(bounce_s)));
      }

      if (!must_ack) delete cxs_here;
    }

    return returner();
  }
 } // namespace detail
}
#endif
