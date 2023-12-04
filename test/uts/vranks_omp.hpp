#ifndef _bcf4d64e_19bd_4080_86c4_57aaf780e98c
#define _bcf4d64e_19bd_4080_86c4_57aaf780e98c

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

// This version of this test deliberately avoids initializing the UPC++/GASNet backend, 
// but still uses some of the UPC++ internals to test them in isolation.
// This is NOT in any way supported for user code!!
#include <upcxx/upcxx.hpp>
#include "../util.hpp"

#if !UPCXX_THREADMODE
#error thread-safe libupcxx is required
#endif

#include <atomic>
#include <vector>

#include <omp.h>
#include <sched.h>

#define VRANKS_IMPL "omp"
#define VRANK_LOCAL thread_local

namespace vranks {
  std::vector<upcxx::persona*> vranks;
  
  template<typename Fn>
  void send(int vrank, Fn msg) {
    // use the internal version to avoid the init check:
    vranks[vrank]->lpc_ff(upcxx::detail::the_persona_tls, std::move(msg));
  }
  
  inline void progress() {
    bool worked = upcxx::detail::the_persona_tls.persona_only_progress();
    
    static thread_local int nothings = 0;
    
    if(worked)
      nothings = 0;
    else if(nothings++ == 10) {
      sched_yield();
      nothings = 0;
    }
  }
  
  template<typename Fn>
  void spawn(Fn fn) {
    int vrank_n = os_env<int>("THREADS", os_env<int>("OMP_NUM_THREADS", 10));
    std::cout<<"Threads: "<<vrank_n<<std::endl;
    vranks.resize(vrank_n);

    omp_set_num_threads(vrank_n);
    
    #pragma omp parallel num_threads(vrank_n)
    {
      int vrank_me = omp_get_thread_num();
      vranks[vrank_me] = &upcxx::default_persona();
      
      #pragma omp barrier
      
      fn(vrank_me, vrank_n);
    }
  }
}
#endif
