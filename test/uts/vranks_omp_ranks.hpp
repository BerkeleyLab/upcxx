#ifndef _6dd7e289_751e_45bc_90dc_006795a19ea7
#define _6dd7e289_751e_45bc_90dc_006795a19ea7

#include <upcxx/upcxx.hpp>
#include "../util.hpp"

#include <atomic>
#include <vector>

#include <omp.h>
#include <sched.h>

#if !UPCXX_THREADMODE
  #error "UPCXX_THREADMODE must be par"
#endif

#define VRANKS_IMPL "ranks+omp"
#define VRANK_LOCAL thread_local

namespace vranks {
  std::vector<upcxx::persona*> thread_agents;
  int thread_per_rank;
  
  template<typename Fn>
  void send(int vrank, Fn msg) {
    int rank = vrank/thread_per_rank;
    int thd = vrank%thread_per_rank;
    
    upcxx::rpc_ff(rank,
      [=]() { thread_agents[thd]->lpc_ff(msg); }
    );
  }
  
  inline void progress() {
    upcxx::progress();
  }
  
  template<typename Fn>
  void spawn(Fn fn) {
    upcxx::init();
    
    thread_per_rank = os_env<int>("THREADS", os_env<int>("OMP_NUM_THREADS", 4));
    if (upcxx::rank_me() == 0) say("") << "Threads per process: " << thread_per_rank;
    thread_agents.resize(thread_per_rank);
    
    std::atomic<int> bar1{0};

    omp_set_num_threads(thread_per_rank);
    
    #pragma omp parallel num_threads(thread_per_rank)
    {
      int thread_me = omp_get_thread_num();
      thread_agents[thread_me] = &upcxx::default_persona();
      
      #pragma omp barrier
      
      fn(
        /*agent_me*/thread_me + thread_per_rank*upcxx::rank_me(),
        /*agent_n*/thread_per_rank*upcxx::rank_n()
      );
      
      // cant omp barrier because we have to service progress
      bar1.fetch_add(1);
      while(bar1.load(std::memory_order_acquire) != thread_per_rank)
        upcxx::progress();
    }
    
    upcxx::finalize();
  }
}
#endif
