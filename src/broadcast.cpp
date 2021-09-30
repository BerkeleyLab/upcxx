#include <upcxx/broadcast.hpp>
#include <upcxx/backend/gasnet/runtime.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <gasnet_coll.h>

using namespace upcxx;
using namespace std;

namespace gasnet = upcxx::backend::gasnet;

void detail::broadcast_trivial(
    const team &tm, intrank_t root, void *buf, size_t size,
    backend::gasnet::handle_cb *cb
  ) {
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  
  gex_Event_t e = gex_Coll_BroadcastNB(
    gasnet::handle_of(tm),
    root,
    buf, buf,
    size,
    /*flags*/0
  );
  
  cb->handle = reinterpret_cast<uintptr_t>(e);
  gasnet::register_cb(cb);
  gasnet::after_gasnet();
}
