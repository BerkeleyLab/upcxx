// This example shows how to implement a naive sum reduction
// using RMA put and a barrier synchronization.
//
// This reduction algorithm is non-scalable and only shown here for 
// demonstration purposes. UPC++ includes scalable reduction operations 
// upcxx::reduce_{one,all}() that should be preferred in real codes.

int64_t reduce_to_rank0(int64_t my_hits)
{
    // Rank 0 creates an array the size of the number of ranks 
    // to store all of the incoming values
    upcxx::global_ptr<int64_t> all_hits_ptr = nullptr;
    if (upcxx::rank_me() == 0) { 
        all_hits_ptr = upcxx::new_array<int64_t>(upcxx::rank_n()); 
    }
    // Rank 0 broadcasts the array global pointer to all ranks
    all_hits_ptr = upcxx::broadcast(all_hits_ptr, 0).wait();
    // All ranks offset the start pointer of the array by their rank to point 
    // to their own chunk of the array
    upcxx::global_ptr<int64_t> my_hits_ptr = all_hits_ptr + upcxx::rank_me();
    // every rank now puts its own hits value into the correct part of the array
    upcxx::rput(my_hits, my_hits_ptr).wait();
    upcxx::barrier();
    // Now rank 0 gets all the values stored in the array
    int64_t hits = 0;
    if (upcxx::rank_me() == 0) {
        // get a local pointer to the shared object on rank 0
        int64_t *local_hits_ptrs = all_hits_ptr.local();
        for (int i = 0; i < upcxx::rank_n(); i++) {
            hits += local_hits_ptrs[i];
        }
        upcxx::delete_array(all_hits_ptr);
    }
    return hits;
}
