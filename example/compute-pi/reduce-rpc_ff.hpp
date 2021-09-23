// This example shows how to implement a naive sum reduction 
// using one-way RPC and a barrier
//
// This reduction algorithm is non-scalable and only shown here for 
// demonstration purposes. UPC++ includes scalable reduction operations 
// upcxx::reduce_{one,all}() that should be preferred in real codes.

int64_t hits = 0; 
// counts the number of ranks for which the RPC has completed
int n_done = 0;

int64_t reduce_to_rank0(int64_t my_hits)
{
    // send a one-way RPC to rank 0 with value from this process
    // Note we cannot wait for the RPC locally - there is no return
    upcxx::rpc_ff(0, [](int64_t my_hits) { hits += my_hits; n_done++; }, my_hits);
    if (upcxx::rank_me() == 0) {
        // spin waiting for RPCs from all ranks to complete
        // When spinning, call the progress function to 
        // ensure rank 0 processes waiting RPCs
        while (n_done != upcxx::rank_n()) upcxx::progress();
    }

    // hits is only set for rank 0 at this point, which is OK because only 
    // rank 0 will print out the result
    return hits;
}
