#ifndef _CECA99E6_CB27_41E2_9478_E2A9B106BBF2
#define _CECA99E6_CB27_41E2_9478_E2A9B106BBF2

#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/rput.hpp>
#include <upcxx/rget.hpp>
#include <tuple>
#include <type_traits>
#include <vector>
#include <numeric> // accumulate

//  I'm not sure we will have a Vector-Index-Strided interface that is
//  not GASNet-based, so if an application wants to make use of the VIS
//  interface it is likely they will want to be running GASNet-Ex 
#include <upcxx/backend/gasnet/runtime.hpp>

namespace upcxx
{
  namespace detail {
    // In the following classes, FinalType will be one of:
    //   rput_cbs_byref<CxStateHere, CxStateRemote>
    //   rput_cbs_byval<T, CxStateHere, CxStateRemote>
    // So there is a pattern of a class inheriting a base-class which has
    // the derived class as a template argument. This allows the base
    // class to access the derived class without using virtual functions.
    
    // rput_cb_source: rput_cbs_{byref|byval} inherits this to hold
    // source-copmletion details.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote,
             bool sync = completions_is_event_sync<
                 typename CxStateHere::completions_t,
                 source_cx_event
               >::value,
             // only use handle when the user has asked for source_cx
             // notification AND hasn't specified that it be sync.
             bool use_handle = !sync && completions_has_event<
                 typename CxStateHere::completions_t,
                 source_cx_event
               >::value
            >
    struct rput_cb_source;

    // rput_cb_operation: rput_cbs_{byref|byval} inherits this to hold
    // operation-completion details.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote,
             bool definitely_static_scope = completions_is_event_sync<
                 typename CxStateHere::completions_t,
                 operation_cx_event
               >::value
            >
    struct rput_cb_operation;

    ////////////////////////////////////////////////////////////////////

    // rput_cb_source: Case when the user has an action for source_cx_event.
    // We extend a handle_cb which will fire the completions and transition
    // control to rput_cb_operation.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote>
    struct rput_cb_source<
        FinalType, CxStateHere, CxStateRemote,
        /*sync=*/false, /*use_handle=*/true
      >:
      backend::gasnet::handle_cb {
      
      static constexpr auto mode = rma_put_sync::src_cb;

      backend::gasnet::handle_cb* source_cb() {
        return this;
      }
      backend::gasnet::handle_cb* first_handle_cb() {
        return this;
      }
      
      void execute_and_delete(backend::gasnet::handle_cb_successor add_succ) {
        auto *cbs = static_cast<FinalType*>(this);
        
        cbs->state_here.template operator()<source_cx_event>();
        
        add_succ(
          static_cast<
              rput_cb_operation<FinalType, CxStateHere, CxStateRemote>*
            >(cbs)
        );
      }
    };

    // rput_cb_source: Case user does not care about source_cx_event.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote,
             bool sync>
    struct rput_cb_source<
        FinalType, CxStateHere, CxStateRemote,
        sync, /*use_handle=*/false
      > {
      static constexpr auto mode =
        sync ? rma_put_sync::src_now : rma_put_sync::src_into_op_cb;
      
      backend::gasnet::handle_cb* source_cb() {
        return nullptr;
      }
      backend::gasnet::handle_cb* first_handle_cb() {
        return static_cast<FinalType*>(this)->operation_cb();
      }
    };

    ////////////////////////////////////////////////////////////////////

    // rput_cb_remote: Inherited by rput_cb_operation to handle sending
    // remote rpc events upon operation completion.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote,
             bool has_remote = !CxStateRemote::empty>
    struct rput_cb_remote;
    
    template<typename FinalType, typename CxStateHere, typename CxStateRemote>
    struct rput_cb_remote<FinalType, CxStateHere, CxStateRemote, /*has_remote=*/true> {
      rput_cb_remote() {
        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) += 1;
      }

      void send_remote() {
        auto *cbs = static_cast<FinalType*>(this);
       
        backend::send_prepared_am_master(progress_level::internal, cbs->rank_d, std::move(cbs->state_remote));

        upcxx::current_persona().UPCXXI_INTERNAL_ONLY(undischarged_n_) -= 1;
      }
    };
    
    template<typename FinalType, typename CxStateHere, typename CxStateRemote>
    struct rput_cb_remote<FinalType, CxStateHere, CxStateRemote, /*has_remote=*/false> {
      void send_remote() {/*nop*/}
    };
    
    // rput_cb_operation: Case when the user wants synchronous operation
    // completion.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote>
    struct rput_cb_operation<
        FinalType, CxStateHere, CxStateRemote, /*definitely_static_scope=*/true
      >:
      rput_cb_remote<FinalType, CxStateHere, CxStateRemote> {

      static constexpr bool definitely_static_scope = true;
      
      backend::gasnet::handle_cb* operation_cb() {
        return nullptr;
      }
    };

    // rput_cb_operation: Case with non-blocking operation completion. We
    // inherit from rput_cb_remote which will dispatch to either sending remote
    // completion events or nop.
    template<typename FinalType, typename CxStateHere, typename CxStateRemote>
    struct rput_cb_operation<
        FinalType, CxStateHere, CxStateRemote, /*definitely_static_scope=*/false
      >:
      rput_cb_remote<FinalType, CxStateHere, CxStateRemote>,
      backend::gasnet::handle_cb {

      static constexpr bool definitely_static_scope = false;
      
      backend::gasnet::handle_cb* operation_cb() {
        return this;
      }
      
      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        auto *cbs = static_cast<FinalType*>(this);
        
        this->send_remote(); // may nop depending on rput_cb_remote case.
        
        cbs->state_here.template operator()<operation_cx_event>();
        
        delete cbs; // cleanup object, no more events
      }
    };
  }

  namespace detail {

    struct memvec_t {
      memvec_t() { }
      const void  *gex_addr;  // TODO: When gasnet changes Memvec we need to track it
      size_t gex_len;
    };

    
    void rma_put_irreg_nb(
                         intrank_t rank_d,
                         std::size_t _dstcount,
                         upcxx::detail::memvec_t const _dstlist[],
                         std::size_t _srcount,
                         upcxx::detail::memvec_t const _srclist[],
                         backend::gasnet::handle_cb *source_cb,
                         backend::gasnet::handle_cb *operation_cb);
    void rma_get_irreg_nb(                               
                         std::size_t _dstcount,
                         upcxx::detail::memvec_t const _dstlist[],
                         upcxx::intrank_t rank_s,
                         std::size_t _srccount,
                         upcxx::detail::memvec_t const _srclist[],
                         backend::gasnet::handle_cb *operation_cb);
    
    
    void rma_put_reg_nb(
                        intrank_t rank_d,
                        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
                        size_t _srccount, void * const _srclist[], size_t _srclen,
                        backend::gasnet::handle_cb *source_cb,
                        backend::gasnet::handle_cb *operation_cb);
    
    void rma_get_reg_nb(
                        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
                        intrank_t ranks,
                        size_t _srccount, void * const _srclist[], size_t _srclen,
                        backend::gasnet::handle_cb *operation_cb);
    
    void rma_put_strided_nb(
                            intrank_t rank_d,
                            void *_dstaddr, const std::ptrdiff_t _dststrides[],
                            const void *_srcaddr, const std::ptrdiff_t _srcstrides[],
                            std::size_t _elemsz,
                            const std::size_t _count[], std::size_t _stridelevels,
                            backend::gasnet::handle_cb *source_cb,
                            backend::gasnet::handle_cb *operation_cb);
    void rma_get_strided_nb(
                            void *_dstaddr, const std::ptrdiff_t _dststrides[],
                            intrank_t _rank_s,
                            const void *_srcaddr, const std::ptrdiff_t _srcstrides[],
                            std::size_t _elemsz,
                            const std::size_t _count[], std::size_t _stridelevels,
                            backend::gasnet::handle_cb *operation_cb);
   
    template<typename CxStateRemote>
    using CxRemoteAm = decltype(backend::prepare_deferred_am_master(0, 
                                std::declval<CxStateRemote>().template bind_event<remote_cx_event>()));

    template<typename CxStateHere, typename CxStateRemote>
    struct rput_cbs_irreg final:
      rput_cb_source</*FinalType=*/rput_cbs_irreg<CxStateHere, CxStateRemote>,
                                   CxStateHere, CxStateRemote>,
      rput_cb_operation</*FinalType=*/rput_cbs_irreg<CxStateHere, CxStateRemote>,
                                      CxStateHere, CxStateRemote>
    {
      intrank_t rank_d;
      CxStateHere state_here;
      CxRemoteAm<CxStateRemote> state_remote;
      std::vector<upcxx::detail::memvec_t> src;
      std::vector<upcxx::detail::memvec_t> dest;
      rput_cbs_irreg(intrank_t rank_d, CxStateHere here, CxStateRemote remote,
                     std::vector<upcxx::detail::memvec_t>&& src,
                     std::vector<upcxx::detail::memvec_t>&& dest):
        rank_d(rank_d),
        state_here(std::move(here)),
        state_remote(backend::prepare_deferred_am_master(rank_d,remote.template bind_event<remote_cx_event>())),
        src(src),
        dest(dest) {
      }
      static constexpr bool static_scope = false;
      void initiate()
      {
        detail::rma_put_irreg_nb(rank_d, dest.size(), &(dest[0]),
                                src.size(), &(src[0]),
                                this->source_cb(), this->operation_cb()); 
      }

    };

    template<typename CxStateHere, typename CxStateRemote>
    struct rput_cbs_reg final:
      rput_cb_source</*FinalType=*/rput_cbs_reg<CxStateHere, CxStateRemote>,
                                   CxStateHere, CxStateRemote>,
      rput_cb_operation</*FinalType=*/rput_cbs_reg<CxStateHere, CxStateRemote>,
                                      CxStateHere, CxStateRemote>
    {
      intrank_t rank_d;
      CxStateHere state_here;
      CxRemoteAm<CxStateRemote> state_remote;
      std::vector<void*> src;
      std::vector<void*> dest;
      rput_cbs_reg(intrank_t rank_d, CxStateHere here, CxStateRemote remote,
                    std::vector<void*>&& src,
                    std::vector<void*>&& dest):
        rank_d(rank_d),
        state_here(std::move(here)),
        state_remote(backend::prepare_deferred_am_master(rank_d,remote.template bind_event<remote_cx_event>())),
        src(src),
        dest(dest) {
      }
      static constexpr bool static_scope = false;
      void initiate(std::size_t destlen, std::size_t srclen)
      {
        detail::rma_put_reg_nb(rank_d, dest.size(), &(dest[0]), destlen,
                               src.size(), &(src[0]), srclen,
                               this->source_cb(), this->operation_cb());
      }

    };
    
    template<typename CxStateHere, typename CxStateRemote>
    struct rput_cbs_strided final:
      rput_cb_source</*FinalType=*/rput_cbs_strided<CxStateHere, CxStateRemote>,
                                   CxStateHere, CxStateRemote>,
      rput_cb_operation</*FinalType=*/rput_cbs_strided<CxStateHere, CxStateRemote>,
                                      CxStateHere, CxStateRemote>
    {
      intrank_t rank_d;
      CxStateHere state_here;
      CxRemoteAm<CxStateRemote> state_remote;
      rput_cbs_strided(intrank_t rank_d, CxStateHere here, CxStateRemote remote):
        rank_d(rank_d),
        state_here(std::move(here)),
        state_remote(backend::prepare_deferred_am_master(rank_d,remote.template bind_event<remote_cx_event>()))
      {
      }
      static constexpr bool static_scope = false;
      void initiate(intrank_t rd,
                    void* dst_addr, const std::ptrdiff_t* dststrides,
                    const void* src_addr, const std::ptrdiff_t* srcstrides,
                    std::size_t elemsize,
                    const std::size_t* count, std::size_t stridelevels)
      {
        detail::rma_put_strided_nb(rd, dst_addr, dststrides,
                                   src_addr, srcstrides,
                                   elemsize, count, stridelevels,
                                   this->source_cb(), this->operation_cb()); 
      }

    };
    
    template<typename CxStateHere, typename CxStateRemote>
    struct rget_cb_irreg final: rget_cb_remote<CxStateRemote>, backend::gasnet::handle_cb {
      CxStateHere state_here;
      std::vector<upcxx::detail::memvec_t> src;
      std::vector<upcxx::detail::memvec_t> dest;
      rget_cb_irreg(intrank_t rank_s, CxStateHere here, CxStateRemote remote,
                   std::vector<upcxx::detail::memvec_t>&& Src,
                   std::vector<upcxx::detail::memvec_t>&& Dest)
        : rget_cb_remote<CxStateRemote>{rank_s, std::move(remote)},
        state_here{std::move(here)}, src(Src), dest(Dest) { }
      void initiate(intrank_t rank_s)
      {
        detail::rma_get_irreg_nb(dest.size(), &(dest[0]),
                                rank_s  ,src.size(), &(src[0]),
                                this);
      }
      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        this->send_remote();
        this->state_here.template operator()<operation_cx_event>();
        delete this;
      }
    };

    template<typename CxStateHere, typename CxStateRemote>
    struct rget_cb_reg final: rget_cb_remote<CxStateRemote>, backend::gasnet::handle_cb {
      CxStateHere state_here;
      std::vector<void*> src;
      std::vector<void*> dest;
      rget_cb_reg(intrank_t rank_s, CxStateHere here, CxStateRemote remote,
                   std::vector<void*>&& Src,
                   std::vector<void*>&& Dest)
        : rget_cb_remote<CxStateRemote>{rank_s, std::move(remote)},
        state_here{std::move(here)}, src(Src), dest(Dest) { }
      void initiate(intrank_t rank_s, std::size_t srclength, std::size_t dstlength)
      {
        detail::rma_get_reg_nb(dest.size(), &(dest[0]), dstlength, 
                               rank_s  ,src.size(), &(src[0]), srclength,
                               this);
      }
      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        this->send_remote();
        this->state_here.template operator()<operation_cx_event>();
        delete this;
      }
    };

    template<typename CxStateHere, typename CxStateRemote>
    struct rget_cbs_strided final: rget_cb_remote<CxStateRemote>, backend::gasnet::handle_cb {
      CxStateHere state_here;
      
      rget_cbs_strided(intrank_t rank_s, CxStateHere here, CxStateRemote remote)
        : rget_cb_remote<CxStateRemote>{rank_s, std::move(remote)},
        state_here{std::move(here)} { }
      void initiate(
                    void* dst_addr, const std::ptrdiff_t* dststrides,
                    intrank_t rank_s,
                    const void* src_addr, const std::ptrdiff_t* srcstrides,
                    std::size_t elemsize,
                    const std::size_t* count, std::size_t stridelevels)
      {
        detail::rma_get_strided_nb(dst_addr, dststrides,
                                   rank_s, src_addr, srcstrides,
                                   elemsize, count, stridelevels,
                                   this); 
      }

      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        this->send_remote();
        this->state_here.template operator()<operation_cx_event>();
        delete this;
      }
    };
  }
  /////////////////////////////////////////////////////////////////////
  //  Actual public API for rput_fragmented, rput_regular, rput_strided
  //
  
  template<typename SrcIter, typename DestIter,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
  rput_irregular(
                  SrcIter src_runs_begin, SrcIter src_runs_end,
                  DestIter dst_runs_begin, DestIter dst_runs_end,
                  Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {


    using CxsDecayed = typename std::decay<Cxs>::type;
    using T = typename std::tuple_element<0,typename std::iterator_traits<DestIter>::value_type>::type::element_type;
    using S = typename std::tuple_element<0,typename std::iterator_traits<SrcIter>::value_type>::type;

    static_assert(is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types.");
    
    static_assert(std::is_convertible<S, const T*>::value,
                  "SrcIter and DestIter need to be over same base T type");

    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
                  detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
                 "Not requesting either operation or remote completion is surely an "
                 "error. You'll have no way of ever knowing when the target memory is "
                 "safe to read or write again.");

                 
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;

 
    constexpr std::size_t tsize=sizeof(T);
 
 
    std::vector<upcxx::detail::memvec_t>  dest(std::distance(dst_runs_begin, dst_runs_end));
    auto dv=dest.begin();
    std::size_t dstsize=0;
    intrank_t gpdrank = upcxx::rank_me(); // default for empty sequence is self
    if(dest.size()!=0) {
      //hoist gpdrank assign out of loop
      gpdrank = std::get<0>(*dst_runs_begin).UPCXXI_INTERNAL_ONLY(rank_);
    }
    for(DestIter d=dst_runs_begin; !(d==dst_runs_end); ++d,++dv)
      {
        UPCXXI_GPTR_CHK(std::get<0>(*d));
	UPCXX_ASSERT(std::get<0>(*d), "pointer arguments to rput_irregular may not be null");
        UPCXX_ASSERT(gpdrank==std::get<0>(*d).UPCXXI_INTERNAL_ONLY(rank_),
                     "pointer arguments to rput_irregular must all target the same affinity");
        dv->gex_addr=(std::get<0>(*d)).UPCXXI_INTERNAL_ONLY(raw_ptr_);
        dv->gex_len =std::get<1>(*d)*tsize;
        dstsize+=dv->gex_len;
      }

    std::size_t srcsize=0;
    std::vector<upcxx::detail::memvec_t> src(std::distance(src_runs_begin, src_runs_end));
    auto sv=src.begin();
    for(SrcIter s=src_runs_begin; !(s==src_runs_end); ++s,++sv)
      {
	UPCXX_ASSERT(std::get<0>(*s), "pointer arguments to rput_irregular may not be null");
        sv->gex_addr=std::get<0>(*s);
        sv->gex_len =std::get<1>(*s)*tsize;
        srcsize+=sv->gex_len;
      }
    
    UPCXX_ASSERT(dstsize==srcsize, 
       "rput_irregular: destination size (" << dstsize << "bytes ) does not match source size (" << srcsize << " bytes)");

    UPCXXI_WARN_EMPTY("upcxx::rput_irregular", srcsize);

    detail::rput_cbs_irreg<cxs_here_t, cxs_remote_t> cbs_static{
      gpdrank,
        cxs_here_t(std::forward<Cxs>(cxs)),
        cxs_remote_t(std::forward<Cxs>(cxs)),
        std::move(src), std::move(dest)
        };

    auto *cbs = decltype(cbs_static)::static_scope
      ? &cbs_static
      : new decltype(cbs_static){std::move(cbs_static)};
    
    auto returner = detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed
      >{cbs->state_here};
    
    cbs->initiate();
    
    return returner();
  }

  template<typename SrcIter, typename DestIter,
  typename Cxs=decltype(operation_cx::as_future())>
    UPCXXI_NODISCARD
    typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byref_event_values,
      typename std::decay<Cxs>::type
    >::return_t
    rget_irregular(
                   SrcIter src_runs_begin, SrcIter src_runs_end,
                   DestIter dst_runs_begin, DestIter dst_runs_end,
                   Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {

    using CxsDecayed = typename std::decay<Cxs>::type;
    using U = typename std::tuple_element<0,typename std::iterator_traits<SrcIter>::value_type>::type::element_type;
    using T = typename std::remove_const<U>::type;
    using D = typename std::tuple_element<0,typename std::iterator_traits<DestIter>::value_type>::type;

    static_assert(is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types.");
    
    static_assert(std::is_convertible<D, const T*>::value,
                  "SrcIter and DestIter need to be over same base T type");
 
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
                  detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
                 "Not requesting either operation or remote completion is surely an "
                 "error. You'll have no way of ever knowing when the target memory is "
                 "safe to read or write again.");
    /* rget_irregular supports remote completion, contrary to the spec */
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value),
      "rget_irregular does not support source completion."
    );

    
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;


    constexpr std::size_t tsize=sizeof(T);
    
    std::vector<upcxx::detail::memvec_t> dest(std::distance(dst_runs_begin, dst_runs_end));
    auto dv=dest.begin();
    std::size_t dstsize=0;
    for(DestIter d=dst_runs_begin; !(d==dst_runs_end); ++d,++dv)
      {
	UPCXX_ASSERT(std::get<0>(*d), "pointer arguments to rget_irregular may not be null");
        dv->gex_addr=(std::get<0>(*d));
        dv->gex_len =std::get<1>(*d)*tsize;
        dstsize+=dv->gex_len;
      }
    
    std::vector<upcxx::detail::memvec_t> src(std::distance(src_runs_begin, src_runs_end));
    auto sv=src.begin();
    std::size_t srcsize=0;
    intrank_t rank_s = upcxx::rank_me(); // default for empty sequence is self
    if(src.size()!=0) {
      // hoist rank_s assign out of loop
      rank_s = std::get<0>(*src_runs_begin).UPCXXI_INTERNAL_ONLY(rank_);
    }
    for(SrcIter s=src_runs_begin; !(s==src_runs_end); ++s,++sv)
      {
        UPCXXI_GPTR_CHK(std::get<0>(*s));
	UPCXX_ASSERT(std::get<0>(*s), "pointer arguments to rget_irregular may not be null");
        UPCXX_ASSERT(rank_s==std::get<0>(*s).UPCXXI_INTERNAL_ONLY(rank_),
                     "pointer arguments to rget_irregular must all target the same affinity");
        sv->gex_addr=std::get<0>(*s).UPCXXI_INTERNAL_ONLY(raw_ptr_);
        sv->gex_len =std::get<1>(*s)*tsize;
        srcsize+=sv->gex_len;
      }
    

    UPCXX_ASSERT(dstsize==srcsize, 
       "rget_irregular: destination size (" << dstsize << " bytes) does not match source size (" << srcsize << " bytes)");

    UPCXXI_WARN_EMPTY("upcxx::rget_irregular", srcsize);

    auto *cb = new detail::rget_cb_irreg<cxs_here_t,cxs_remote_t>{
      rank_s,
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)},
      std::move(src), std::move(dest)
    };

    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byref_event_values,
        CxsDecayed
      >{cb->state_here};

    cb->initiate(rank_s);

    
    return returner();
  }
  
  template<typename SrcIter, typename DestIter,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
  rput_regular(
               SrcIter src_runs_begin, SrcIter src_runs_end,
               std::size_t src_run_length,
               DestIter dst_runs_begin, DestIter dst_runs_end,
               std::size_t dst_run_length,
               Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {
    using CxsDecayed = typename std::decay<Cxs>::type;
   // This computes T by pulling it out of global_ptr<T>.
    using T = typename std::iterator_traits<DestIter>::value_type::element_type;
    
    static_assert(
                  is_trivially_serializable<T>::value,
                  "RMA operations only work on TriviallySerializable types."
                  );
    
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((
                  detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
                  detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
                 "Not requesting either operation or remote completion is surely an "
                 "error. You'll have no way of ever knowing when the target memory is "
                 "safe to read or write again.");
 
    static_assert(std::is_convertible<
                  /*from*/typename std::iterator_traits<SrcIter>::value_type,
                  /*to*/T const*
                  >::value,
                  "Source iterator's value type not convertible to T const*." );

    
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;

 
 
    // Construct list of dest run pointers
    std::vector<void*> dst_ptrs;
    
       // Construct list of src run pointers. The old code called `resize` followed
    // by setting elements, which incurred an unnecessary zeroing of the elements
    // during the resize. This new way is to do a `reserve` followed by `push_back's`.
    dst_ptrs.reserve(std::distance(dst_runs_begin, dst_runs_end));
 
    intrank_t dst_rank;
    if(dst_ptrs.capacity()) {
      dst_rank = (*dst_runs_begin).UPCXXI_INTERNAL_ONLY(rank_);
    } else {
      dst_rank = upcxx::rank_me(); // default for empty sequence is self
    }
    for(DestIter d=dst_runs_begin; !(d == dst_runs_end); ++d) {
      UPCXXI_GPTR_CHK(*d);
      UPCXX_ASSERT(*d, "pointer arguments to rput_regular may not be null");
      UPCXX_ASSERT(dst_rank==(*d).UPCXXI_INTERNAL_ONLY(rank_),
                   "pointer arguments to rput_regular must all target the same affinity");
      dst_ptrs.push_back((*d).UPCXXI_INTERNAL_ONLY(raw_ptr_));
    }

    std::vector<void*> src_ptrs;

    src_ptrs.reserve(std::distance(src_runs_begin, src_runs_end));
  

    for(SrcIter s=src_runs_begin; !(s == src_runs_end); ++s) {
      UPCXX_ASSERT((*s), "pointer arguments to rput_regular may not be null");
      src_ptrs.push_back(const_cast<void*>((void const*)*s));
    }

    UPCXX_ASSERT(src_ptrs.size()*src_run_length == dst_ptrs.size()*dst_run_length,
       "rput_regular: destination size (" << dst_ptrs.size()*dst_run_length 
       << " bytes) does not match source size (" << src_ptrs.size()*src_run_length << " bytes)");

    UPCXXI_WARN_EMPTY("upcxx::rput_regular", src_ptrs.size()*src_run_length);

    detail::rput_cbs_reg<cxs_here_t, cxs_remote_t> cbs_static{
      dst_rank,
      cxs_here_t(std::forward<Cxs>(cxs)),
      cxs_remote_t(std::forward<Cxs>(cxs)),
      std::move(src_ptrs), std::move(dst_ptrs)
    };

    auto *cbs = decltype(cbs_static)::static_scope
      ? &cbs_static
      : new decltype(cbs_static){std::move(cbs_static)};
    
    auto returner = detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed
      >{cbs->state_here};
    
    cbs->initiate(dst_run_length*sizeof(T), src_run_length*sizeof(T));
    
    return returner();
  }
  
  template<typename SrcIter, typename DestIter,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rget_byref_event_values,
    typename std::decay<Cxs>::type
    >::return_t
  rget_regular(
                  SrcIter src_runs_begin, SrcIter src_runs_end,
                  std::size_t src_run_length,
                  DestIter dst_runs_begin, DestIter dst_runs_end,
                  std::size_t dst_run_length,
                  Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {

    using CxsDecayed = typename std::decay<Cxs>::type;
    // Pull T out of global_ptr</*const*/ T> from SrcIter
    using U = typename std::iterator_traits<SrcIter>::value_type::element_type;
    using T = typename std::remove_const<U>::type;
    using D = typename std::iterator_traits<DestIter>::value_type;
    
    static_assert(std::is_convertible</*from*/D, /*to*/const T*>::value,
                  "Destination iterator's value type not convertible to T*." );

    static_assert(is_trivially_serializable<T>::value,
                  "RMA operations only work on TriviallySerializable types.");
    
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
                  detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
                 "Not requesting either operation or remote completion is surely an "
                 "error. You'll have no way of ever knowing when the target memory is "
                 "safe to read or write again.");
    /* rget_regular supports remote completion, contrary to the spec */
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value),
      "rget_regular does not support source completion."
    );
    

    static_assert( is_trivially_serializable<T>::value,
                   "RMA operations only work on TriviallySerializable types.");
    
    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rget_byref_event_values,
      CxsDecayed>;

    // Construct list of src run pointers. The old code called `resize` followed
    // by setting elements, which incurred an unnecessary zeroing of the elements
    // during the resize. This new way is to do a `reserve` followed by `push_back's`.
    
       // Construct list of dest run pointers
    std::vector<void*> dst_ptrs;
    dst_ptrs.reserve(std::distance(dst_runs_begin, dst_runs_end));
 
    for(DestIter d=dst_runs_begin; !(d == dst_runs_end); ++d) {
      UPCXX_ASSERT((*d), "pointer arguments to rget_regular may not be null");
      dst_ptrs.push_back((void*)*d);
    }

    
    std::vector<void*> src_ptrs;
    src_ptrs.reserve(std::distance(src_runs_begin, src_runs_end));
   
    intrank_t src_rank;
    if(src_ptrs.capacity()) {
      src_rank = (*src_runs_begin).UPCXXI_INTERNAL_ONLY(rank_);
    } else {
      src_rank = upcxx::rank_me(); // default for empty sequence is self
    }
    for(SrcIter s=src_runs_begin; !(s == src_runs_end); ++s) {
      UPCXXI_GPTR_CHK(*s);
      UPCXX_ASSERT((*s), "pointer arguments to rget_regular may not be null");
      UPCXX_ASSERT(src_rank==(*s).UPCXXI_INTERNAL_ONLY(rank_),
                   "pointer arguments to rget_regular must all target the same affinity");
      src_ptrs.push_back((*s).UPCXXI_INTERNAL_ONLY(raw_ptr_));
    }
 
    
    UPCXX_ASSERT(src_ptrs.size()*src_run_length == dst_ptrs.size()*dst_run_length,
       "rget_regular: destination size (" << dst_ptrs.size()*dst_run_length 
       << " bytes) does not match source size (" << src_ptrs.size()*src_run_length << " bytes)");

    UPCXXI_WARN_EMPTY("upcxx::rget_regular", src_ptrs.size()*src_run_length);
    
    auto *cb = new detail::rget_cb_reg<cxs_here_t,cxs_remote_t>{
      src_rank,
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)},
      std::move(src_ptrs), std::move(dst_ptrs)
    };

    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rget_byref_event_values,
        CxsDecayed
      >{cb->state_here};

    cb->initiate(src_rank, src_run_length*sizeof(T), dst_run_length*sizeof(T));
    
    return returner();
  }

  
  template<std::size_t Dim, typename T,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
    rput_strided(
       T const *src_base,
       std::ptrdiff_t const *src_strides,
       global_ptr<T> dest_base,
       std::ptrdiff_t const *dest_strides,
       std::size_t const *extents,
       Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {
    using CxsDecayed = typename std::decay<Cxs>::type;
    static_assert(
      is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types."
    );
    
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((
      detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
      detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
      "Not requesting either operation or remote completion is surely an "
      "error. You'll have no way of ever knowing when the target memory is "
      "safe to read or write again."
                         );
    
    UPCXXI_GPTR_CHK(dest_base);
    UPCXX_ASSERT(src_base && dest_base, "pointer arguments to rput_strided may not be null");

    UPCXXI_WARN_EMPTY("upcxx::rput_strided", 
                     std::accumulate(extents, extents+Dim, (size_t)1, std::multiplies<size_t>()));

    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;

    detail::rput_cbs_strided<cxs_here_t, cxs_remote_t> cbs_static{
      dest_base.UPCXXI_INTERNAL_ONLY(rank_),
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)}
    };
        auto *cbs = decltype(cbs_static)::static_scope
      ? &cbs_static
      : new decltype(cbs_static){std::move(cbs_static)};
    
    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rput_event_values,
        CxsDecayed
      >{cbs->state_here};
    
    cbs->initiate(dest_base.UPCXXI_INTERNAL_ONLY(rank_),
                  dest_base.UPCXXI_INTERNAL_ONLY(raw_ptr_), dest_strides,
                  src_base, src_strides, sizeof(T), extents, Dim);
    
    return returner();
  }

  template<std::size_t Dim, typename T,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
  rput_strided(
               T const *src_base,
               std::array<std::ptrdiff_t,Dim> const &src_strides,
               global_ptr<T> dest_base,
               std::array<std::ptrdiff_t,Dim> const &dest_strides,
               std::array<std::size_t,Dim> const &extents,
               Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {
    return rput_strided<Dim, T, Cxs>(src_base,&src_strides.front(),
                                     dest_base, &dest_strides.front(),
                                     &extents.front(), std::forward<Cxs>(cxs));
  }
  
  template<std::size_t Dim, typename T,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
  rget_strided(
               global_ptr<const T> src_base,
               std::ptrdiff_t const *src_strides,
               T* dest_base,
               std::ptrdiff_t const *dest_strides,
               std::size_t const *extents,
               Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {
    using CxsDecayed = typename std::decay<Cxs>::type;
    static_assert(is_trivially_serializable<T>::value,
      "RMA operations only work on TriviallySerializable types.");
    
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT_ALWAYS((detail::completions_has_event<CxsDecayed, operation_cx_event>::value |
                  detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
                 "Not requesting either operation or remote completion is surely an "
                 "error. You'll have no way of ever knowing when the target memory is "
                 "safe to read or write again.");
    /* rget_strided supports remote completion, contrary to the spec */
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value),
      "rget_strided does not support source completion."
    );
 
    UPCXXI_GPTR_CHK(src_base);
    UPCXX_ASSERT(src_base && dest_base, "pointer arguments to rget_strided may not be null");

    UPCXXI_WARN_EMPTY("upcxx::rget_strided", 
                     std::accumulate(extents, extents+Dim, (size_t)1, std::multiplies<size_t>()));

    using cxs_here_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;
    using cxs_remote_t = detail::completions_state<
      /*EventPredicate=*/detail::event_is_remote,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed>;

    auto *cbs = new detail::rget_cbs_strided<cxs_here_t, cxs_remote_t>{
      src_base.UPCXXI_INTERNAL_ONLY(rank_),
      cxs_here_t{std::forward<Cxs>(cxs)},
      cxs_remote_t{std::forward<Cxs>(cxs)}
    };
    
    auto returner = detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::rput_event_values,
      CxsDecayed
      >{cbs->state_here};
    
    cbs->initiate(dest_base, dest_strides,
                  src_base.UPCXXI_INTERNAL_ONLY(rank_),
                  src_base.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                  src_strides, sizeof(T), extents, Dim);
    
    return returner();
  }
  


  template<std::size_t Dim, typename T,
           typename Cxs=decltype(operation_cx::as_future())>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
    /*EventPredicate=*/detail::event_is_here,
    /*EventValues=*/detail::rput_event_values,
    typename std::decay<Cxs>::type>::return_t  
  rget_strided(
               global_ptr<const T> src_base,
               std::array<std::ptrdiff_t,Dim> const &src_strides,
               T *dest_base,
               std::array<std::ptrdiff_t,Dim> const &dest_strides,
               std::array<std::size_t,Dim> const &extents,
               Cxs &&cxs=detail::operation_cx_as_future_t{{}})
  {
    return rget_strided<Dim, T, Cxs>(src_base,&src_strides.front(),
                              dest_base, &dest_strides.front(),
                              &extents.front(), std::forward<Cxs>(cxs));
  }
 
}


#endif
