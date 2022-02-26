#include<iostream>
#include<omp.h>
#include<upcxx/upcxx.hpp>
#include<atomic>

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

int main() {
  upcxx::init();
//SNIPPET
  upcxx::intrank_t me = upcxx::rank_me();
  upcxx::intrank_t n = upcxx::rank_n();
  upcxx::intrank_t buddy = (me^1)%n;
  std::atomic<int> done(1); // master thread doesn't do worker loop

#pragma omp parallel num_threads(4)
  {
    int threads = omp_get_num_threads();
    UPCXX_ASSERT(threads>1);
    // OpenMP guarantees master thread has rank 0
    if (omp_get_thread_num() == 0) {
      UPCXX_ASSERT(upcxx::master_persona().active_with_caller());
      do {
        upcxx::progress();
      } while(done.load(std::memory_order_relaxed) != threads);
    } else { // worker threads send RPCs
      upcxx::future<> fut_all = upcxx::make_future();
      for (int i=0; i<10; i++) { // RPC with buddy rank
        upcxx::future<> fut = upcxx::rpc(buddy,[](int tid, int rank){
          std::cout << "RPC from thread " << tid << " of rank " 
          << rank << std::endl; 
        },omp_get_thread_num(),me);
        fut_all = upcxx::when_all(fut_all,fut);
      }
      fut_all.wait(); // wait for thread quiescence
      done++; // worker threads can sleep at OpenMP barrier
    }
  } // <-- this closing brace implies an OpenMP thread barrier

  upcxx::barrier(); // wait for other ranks to finish incoming work
//SNIPPET
  if (me == 0) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
  return 0;
}
