#include <upcxx/rput.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace gasnet = upcxx::backend::gasnet;
namespace detail = upcxx::detail;

template<detail::rma_put_sync sync_lb>
detail::rma_put_sync detail::rma_put(
    upcxx::intrank_t rank_d, void *buf_d,
    const void *buf_s, std::size_t size,
    gasnet::handle_cb *src_cb,
    gasnet::handle_cb *op_cb
  ) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXX_ASSERT(!backend::rank_is_local(rank_d)); // bypass handled in header

  if(sync_lb != rma_put_sync::op_now) {
    gex_Event_t src_h = GEX_EVENT_INVALID, *src_ph;

    switch(sync_lb) {
    case rma_put_sync::src_cb:
      src_ph = &src_h;
      break;
    case rma_put_sync::src_into_op_cb:
      src_ph = GEX_EVENT_DEFER;
      break;
    case rma_put_sync::src_now:
    case rma_put_sync::op_now: // silence switch(enum) exhaustiveness warning
      src_ph = GEX_EVENT_NOW;
      break;
    }
    
    gex_Event_t op_h = gex_RMA_PutNB(
      gasnet::handle_of(upcxx::world()), rank_d,
      buf_d, const_cast<void*>(buf_s), size,
      src_ph,
      UPCXXI_GEX_FLAG_PEER_NEVER_NBRHD
    );
    
    op_cb->handle = reinterpret_cast<uintptr_t>(op_h);
    
    if(sync_lb == rma_put_sync::src_cb)
      src_cb->handle = reinterpret_cast<uintptr_t>(src_h);
    
    if(0 == gex_Event_Test(op_h))
      return rma_put_sync::op_now;
    
    if(sync_lb == rma_put_sync::src_cb && 0 == gex_Event_Test(src_h))
      return rma_put_sync::src_now;
    
    return sync_lb;
  }
  else {
    (void)gex_RMA_PutBlocking(
      gasnet::handle_of(upcxx::world()), rank_d,
      buf_d, const_cast<void*>(buf_s), size,
      UPCXXI_GEX_FLAG_PEER_NEVER_NBRHD
    );
    
    return rma_put_sync::op_now;
  }
}

// instantiate all four cases of rma_put
template
detail::rma_put_sync detail::rma_put<
  /*mode=*/detail::rma_put_sync::src_cb
  >(
    upcxx::intrank_t rank_d, void *buf_d,
    const void *buf_s, std::size_t size,
    gasnet::handle_cb *src_cb,
    gasnet::handle_cb *op_cb
  );

template
detail::rma_put_sync detail::rma_put<
  /*mode=*/detail::rma_put_sync::src_into_op_cb
  >(
    upcxx::intrank_t rank_d, void *buf_d,
    const void *buf_s, std::size_t size,
    gasnet::handle_cb *src_cb,
    gasnet::handle_cb *op_cb
  );

template
detail::rma_put_sync detail::rma_put<
  /*mode=*/detail::rma_put_sync::src_now
  >(
    upcxx::intrank_t rank_d, void *buf_d,
    const void *buf_s, std::size_t size,
    gasnet::handle_cb *src_cb,
    gasnet::handle_cb *op_cb
  );

// only used when UPCXXI_HAS_OPERATION_CX_AS_BLOCKING=1
template
detail::rma_put_sync detail::rma_put<
  /*mode=*/detail::rma_put_sync::op_now
  >(
    upcxx::intrank_t rank_d, void *buf_d,
    const void *buf_s, std::size_t size,
    gasnet::handle_cb *src_cb,
    gasnet::handle_cb *op_cb
  );
