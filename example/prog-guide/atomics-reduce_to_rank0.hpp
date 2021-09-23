// This example shows how to implement a naive sum reduction
// using atomic fetch-add and a barrier synchronization
//
// This reduction algorithm is non-scalable and only shown here for 
// demonstration purposes. UPC++ includes scalable reduction operations 
// upcxx::reduce_{one,all}() that should be preferred in real codes.

int64_t reduce_to_rank0(int64_t my_hits)
{
  // construct an atomic domain to mediate atomic update operations
  // In real programs this is normally performed once at program startup
  upcxx::atomic_domain<int64_t> ad_i64({upcxx::atomic_op::load, upcxx::atomic_op::fetch_add});

  // a global pointer to the atomic counter in rank 0's shared segment
  upcxx::global_ptr<int64_t> hits_ptr =
      (!upcxx::rank_me() ? upcxx::new_<int64_t>(0) : nullptr);
  // rank 0 allocates and then broadcasts the global pointer to all other ranks
  hits_ptr = upcxx::broadcast(hits_ptr, 0).wait();
  // now each rank updates the global pointer value using atomics for correctness
  ad_i64.fetch_add(hits_ptr, my_hits, memory_order_relaxed).wait();
  // wait until all ranks have updated the counter
  upcxx::barrier();
  // once a memory location is accessed with atomics, it should only be
  // subsequently accessed using atomics to prevent unexpected results
  int64_t result = 0;
  if (upcxx::rank_me() == 0) {
    result = ad_i64.load(hits_ptr, memory_order_relaxed).wait();
  }
  ad_i64.destroy(); // required before destruction of this local-scope atomic_domain
  return result;
}
