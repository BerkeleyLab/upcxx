#include <upcxx/vis.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#if UPCXXI_BACKEND_GASNET
  #include <gasnet_vis.h>
#endif

#include <cstddef>

namespace gasnet = upcxx::backend::gasnet;

static_assert(offsetof(gex_Memvec_t, gex_addr) == offsetof(upcxx::detail::memvec_t, gex_addr) &&
              offsetof(gex_Memvec_t, gex_len) == offsetof(upcxx::detail::memvec_t, gex_len) &&
              sizeof(gex_Memvec_t) == sizeof(upcxx::detail::memvec_t),
              "UPC++ internal issue: unsupported gasnet version");

void upcxx::detail::rma_put_irreg_nb(
                                    upcxx::intrank_t rank_d,
                                    std::size_t _dstcount,
                                    upcxx::detail::memvec_t const _dstlist[],
                                    std::size_t _srccount,
                                    upcxx::detail::memvec_t const _srclist[],
                                    backend::gasnet::handle_cb *source_cb,
                                    backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Flags_t flags = 0;
  if(source_cb!=NULL) // user has requested source completion event
    flags = GEX_FLAG_ENABLE_LEAF_LC;
  
  gex_Event_t op_h = gex_VIS_VectorPutNB(gasnet::handle_of(upcxx::world()),
                                         rank_d,
                                         _dstcount,
                                         reinterpret_cast<const gex_Memvec_t*>(_dstlist),
                                         _srccount,
                                         reinterpret_cast<const gex_Memvec_t*>(_srclist),
                                         flags);

  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  if(source_cb!=NULL) // user has asked for source completion
    {
      gex_Event_t VISput_LC = gex_Event_QueryLeaf(op_h, GEX_EC_LC);
      source_cb->handle = reinterpret_cast<uintptr_t>(VISput_LC);
      gasnet::register_cb(source_cb);// it appears to matter in what order I register_cb
    }
  else
    {
      gasnet::register_cb(operation_cb);
    }
  gasnet::after_gasnet();
}



void upcxx::detail::rma_get_irreg_nb(                               
                                    std::size_t _dstcount,
                                    upcxx::detail::memvec_t const _dstlist[],
                                    upcxx::intrank_t rank_s,
                                    std::size_t _srccount,
                                    upcxx::detail::memvec_t const _srclist[],
                                    backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Event_t op_h = gex_VIS_VectorGetNB(gasnet::handle_of(upcxx::world()),
                                         _dstcount,
                                         reinterpret_cast<const gex_Memvec_t*>(_dstlist),
                                         rank_s,
                                         _srccount,
                                         reinterpret_cast<const gex_Memvec_t*>(_srclist),
                                         /* flags */ 0);

  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  gasnet::register_cb(operation_cb);
  gasnet::after_gasnet();
}


void upcxx::detail::rma_put_reg_nb(
                    intrank_t rank_d,
                    size_t _dstcount, void * const _dstlist[], size_t _dstlen,
                    size_t _srccount, void * const _srclist[], size_t _srclen,
                    backend::gasnet::handle_cb *source_cb,
                    backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Event_t op_h;
  gex_Flags_t flags = 0;
  if(source_cb!=NULL) // user has requested source completion event
    flags = GEX_FLAG_ENABLE_LEAF_LC;
 
  op_h = gex_VIS_IndexedPutNB(gasnet::handle_of(upcxx::world()),
                              rank_d,
                              _dstcount, _dstlist, _dstlen,
                              _srccount, _srclist, _srclen,
                              flags);
  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  if(source_cb!=NULL) // user has asked for source completion
    {
      gex_Event_t VISput_LC = gex_Event_QueryLeaf(op_h, GEX_EC_LC);
      source_cb->handle = reinterpret_cast<uintptr_t>(VISput_LC);
      gasnet::register_cb(source_cb);// it appears to matter in what order I register_cb
    }
  else
    {
      gasnet::register_cb(operation_cb);
    }
  gasnet::after_gasnet();
}

void upcxx::detail::rma_get_reg_nb(
                    size_t _dstcount, void * const _dstlist[], size_t _dstlen,
                    intrank_t rank_s,
                    size_t _srccount, void * const _srclist[], size_t _srclen,
                    backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Event_t op_h;

  op_h = gex_VIS_IndexedGetNB(gasnet::handle_of(upcxx::world()),
                              _dstcount, _dstlist, _dstlen,
                              rank_s,
                              _srccount, _srclist, _srclen,
                              /*flags*/ 0);
  
  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  gasnet::register_cb(operation_cb);
  gasnet::after_gasnet();
}

void upcxx::detail::rma_put_strided_nb(
                        intrank_t rank_d,
                        void *_dstaddr, const std::ptrdiff_t _dststrides[],
                        const void *_srcaddr, const std::ptrdiff_t _srcstrides[],
                        std::size_t _elemsz,
                        const std::size_t _count[], std::size_t _stridelevels,
                        backend::gasnet::handle_cb *source_cb,
                        backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Flags_t flags = 0;
  if(source_cb!=NULL) // user has requested source completion event
    flags = GEX_FLAG_ENABLE_LEAF_LC;
  
  gex_Event_t op_h = gex_VIS_StridedPutNB(gasnet::handle_of(upcxx::world()),
                                          rank_d,
                                          _dstaddr, _dststrides,
                                          const_cast<void*>(_srcaddr), _srcstrides,
                                          _elemsz,
                                          _count, _stridelevels,
                                          flags);
  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  if(source_cb!=NULL) // user has asked for source completion
    {
      gex_Event_t VISput_LC = gex_Event_QueryLeaf(op_h, GEX_EC_LC);
      source_cb->handle = reinterpret_cast<uintptr_t>(VISput_LC);
      gasnet::register_cb(source_cb);// it appears to matter in what order I register_cb
    }
  else
    {
      gasnet::register_cb(operation_cb);
    }
  gasnet::after_gasnet();

}

void upcxx::detail::rma_get_strided_nb(
                        void *_dstaddr, const std::ptrdiff_t _dststrides[],
                        intrank_t _rank_s,
                        const void *_srcaddr, const std::ptrdiff_t _srcstrides[],
                        std::size_t _elemsz,
                        const std::size_t _count[], std::size_t _stridelevels,
                        backend::gasnet::handle_cb *operation_cb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

  gex_Event_t op_h = gex_VIS_StridedGetNB(gasnet::handle_of(upcxx::world()),
                                          _dstaddr, _dststrides,
                                          _rank_s,
                                          const_cast<void*>(_srcaddr), _srcstrides,
                                          _elemsz,
                                          _count, _stridelevels,
                                          /*flag */ 0);
 

  operation_cb->handle = reinterpret_cast<uintptr_t>(op_h);
  gasnet::register_cb(operation_cb);
  gasnet::after_gasnet();

}

