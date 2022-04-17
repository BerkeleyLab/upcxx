#include<iostream>
#include<omp.h>
#include<upcxx/upcxx.hpp>
#include<atomic>

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

using upcxx::experimental::os_env;

int main() {
  upcxx::init();
  int thread_count = os_env<int>("THREADS", os_env<int>("OMP_NUM_THREADS", 4));
  if(upcxx::rank_me() == 0) std::cout<<"Threads: "<<thread_count<<std::endl;
  omp_set_dynamic(0); // required to guarantee exact OMP parallel thread count
//SNIPPET
  const int me = upcxx::rank_me();
  const int n = upcxx::rank_n();
  const int buddy = (me^1)%n;
  const int tn = thread_count; // threads per process
  std::atomic<int> done(1); // master thread doesn't do worker loop

#pragma omp parallel num_threads(tn)
  {
    UPCXX_ASSERT(tn == omp_get_num_threads());
    // OpenMP guarantees master thread has rank 0
    if (omp_get_thread_num() == 0) {
      UPCXX_ASSERT(upcxx::master_persona().active_with_caller());
      do {
        upcxx::progress();
      } while(done.load(std::memory_order_relaxed) != tn);
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
