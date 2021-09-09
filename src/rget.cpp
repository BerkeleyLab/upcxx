#include <upcxx/rget.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace gasnet = upcxx::backend::gasnet;
namespace detail = upcxx::detail;

detail::rma_get_done detail::rma_get_nb(
    void *buf_d,
    intrank_t rank_s,
    const void *buf_s,
    std::size_t buf_size,
    gasnet::handle_cb *cb
  ) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXX_ASSERT(!backend::rank_is_local(rank_s)); // bypass handled in header

  gex_Event_t h = gex_RMA_GetNB(
    gasnet::handle_of(upcxx::world()),
    buf_d, rank_s, const_cast<void*>(buf_s), buf_size,
    UPCXXI_GEX_FLAG_PEER_NEVER_NBRHD
  );
  cb->handle = reinterpret_cast<uintptr_t>(h);
  
  return 0 == gex_Event_Test(h)
    ? rma_get_done::operation
    : rma_get_done::none;
}

