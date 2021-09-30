#include <upcxx/barrier.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

#include <atomic>

using namespace upcxx;
using namespace std;

// obsolete hand-rolled barrier removed post 2021.3.0 release

void upcxx::barrier(const team &tm) {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);
 
  // memory fencing is handled inside gex_Coll_BarrierNB + gex_Event_Test
  //std::atomic_thread_fence(std::memory_order_release);
  
  gex_Event_t e = gex_Coll_BarrierNB(backend::gasnet::handle_of(tm), 0);
  
  while(0 != gex_Event_Test(e))
    upcxx::progress();
  
  //std::atomic_thread_fence(std::memory_order_acquire);
}

void upcxx::detail::barrier_async_inject(
    const team &tm,
    backend::gasnet::handle_cb *cb
  ) {
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

    gex_Event_t e = gex_Coll_BarrierNB(backend::gasnet::handle_of(tm), 0);
    cb->handle = reinterpret_cast<std::uintptr_t>(e);
    backend::gasnet::register_cb(cb);
}
