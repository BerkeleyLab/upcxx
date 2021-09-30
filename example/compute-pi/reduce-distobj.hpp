// This example shows how to implement a naive sum reduction
// using a distributed object and barrier synchronization
//
// This reduction algorithm is non-scalable and only shown here for 
// demonstration purposes. UPC++ includes scalable reduction operations 
// upcxx::reduce_{one,all}() that should be preferred in real codes.

int64_t reduce_to_rank0(int64_t my_hits)
{
    // declare a distributed object across all the ranks, 
    // initialized to each rank's provided value
    upcxx::dist_object<int64_t> all_hits(my_hits);

    // Note: no synchronization required at this point, because
    // fetches below will automatically stall as needed 
    // to await construction of the dist_object above on each rank,

    // sum up the values
    int64_t hits = 0;
    if (upcxx::rank_me() == 0) {
        // rank 0 gets all the values
        for (int i = 0; i < upcxx::rank_n(); i++) {
            // fetch the distributed object from remote rank i
            hits += all_hits.fetch(i).wait();
        }
    }
    // ensure that no distributed objects are destructed before rank 0 is done
    upcxx::barrier();
    return hits;
}
