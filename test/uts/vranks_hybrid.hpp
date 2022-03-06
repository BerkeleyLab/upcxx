#ifndef _f387e40c_d7ab_4dbf_a130_3bcd835cc3b9
#define _f387e40c_d7ab_4dbf_a130_3bcd835cc3b9

#include <upcxx/upcxx.hpp>
#include "../util.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <sched.h>

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

#define VRANKS_IMPL "ranks+threads"
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
    
    thread_per_rank = os_env<int>("THREADS", 4);
    if (upcxx::rank_me() == 0) say("") << "Threads per process: " << thread_per_rank;
    thread_agents.resize(thread_per_rank);
    
    std::vector<std::thread*> threads{(unsigned)thread_per_rank};
    std::atomic<int> bar0{0}, bar1{0};
    
    auto thread_main = [&](int thread_me) {
      thread_agents[thread_me] = &upcxx::default_persona();
      
      bar0.fetch_add(1);
      while(bar0.load(std::memory_order_acquire) != thread_per_rank)
        sched_yield();
      
      fn(
        /*agent_me*/thread_me + thread_per_rank*upcxx::rank_me(),
        /*agent_n*/thread_per_rank*upcxx::rank_n()
      );
      
      bar1.fetch_add(1);
      while(bar1.load(std::memory_order_acquire) != thread_per_rank)
        upcxx::progress();
    };
    
    for(int t=1; t < thread_per_rank; t++)
      threads[t] = new std::thread{thread_main, t};
    
    thread_main(0);
    
    for(int t=1; t < thread_per_rank; t++) {
      threads[t]->join();
      delete threads[t];
    }
    
    upcxx::finalize();
  }
}
#endif
