#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#include <sched.h>

#include "util.hpp"

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

using namespace upcxx;
using namespace std;

// Barrier state bitmasks.
thread_local uint64_t state_bits[2] = {0, 0};

const int thread_n = 10;
thread_local int thread_me = -100;

persona *peer_persona[thread_n];

void thread_progress() {
  bool worked = 0 != upcxx::detail::the_persona_tls.persona_only_progress();
  
  static thread_local int nothings = 0;
  
  if(worked)
    nothings = 0;
  else if(nothings++ == 10) {
    sched_yield();
    nothings = 0;
  }
}

void lpc_barrier() {
  static thread_local unsigned epoch_bump = 0;
  int epoch = epoch_bump++;
  
  int round = 0;
  
  while(1<<round < thread_n) {
    uint64_t bit = uint64_t(1)<<round;
    
    int peer = thread_me + bit;
    if(peer >= thread_n) peer -= thread_n;
    
    peer_persona[peer]->lpc_ff([=]() {
      state_bits[epoch & 1] |= bit;
    });
    
    while(0 == (state_bits[epoch & 1] & bit))
      thread_progress();
    
    round += 1;
  }
  
  state_bits[epoch & 1] = 0;
}


struct tcout {
  static std::mutex lock_;
  
  tcout() { lock_.lock(); }
  ~tcout() {
    std::cout << '\n';
    std::cout.flush();
    lock_.unlock();
  }
  
  template<typename T>
  tcout& operator<<(T x) {
    std::cout << x;
    return *this;
  }
};

std::mutex tcout::lock_;

void thread_main() {
  for(int i=0; i < 10; i++) {
    lpc_barrier();
    
    if(i % thread_n == thread_me) {
      tcout() << "Barrier "<<i;
    }
  }

  int right = (thread_me + 1) % thread_n;
  int left = (thread_me + 1 + thread_n) % thread_n;

  static thread_local bool got_right = false;
  static thread_local bool got_left = false;
  
  {
    future<int> fut = peer_persona[right]->lpc([]() {
      tcout() << thread_me << ": from left";
      got_left = true;
      return 0xbeef;
    });
    
    fut.wait();
    UPCXX_ASSERT_ALWAYS(fut.result() == 0xbeef, "lpc returned wrong value = "<<fut.result());
  }

  lpc_barrier();

  UPCXX_ASSERT_ALWAYS(got_left, "no left found before barrier");
  UPCXX_ASSERT_ALWAYS(!got_right, "right found before barrier");

  if(thread_me == 0) {
    tcout() << "Eyeball me! No 'rights' before this message, no 'lefts' after.";
  }

  got_left = false;

  lpc_barrier();

  {
    int me = thread_me;
    future<int> fut = peer_persona[left]->lpc([=]() {
      tcout() << thread_me << ": from right";
      cout.flush();
      got_right = true;
      return me;
    });
    
    fut.wait();
    UPCXX_ASSERT_ALWAYS(fut.result() == thread_me, "lpc returned wrong value");
  }

  lpc_barrier();

  UPCXX_ASSERT_ALWAYS(got_right, "no right found after barrier");
  UPCXX_ASSERT_ALWAYS(!got_left, "left found after barrier");
}

int main() {
  upcxx::init();
  print_test_header();
  
  std::atomic<int> setup_bar{0};
  
  auto thread_fn = [&](int me) {
    thread_me = me;
    peer_persona[me] = &upcxx::default_persona();
    
    setup_bar.fetch_add(1);
    while(setup_bar.load(std::memory_order_relaxed) != thread_n)
      sched_yield();
    
    thread_main();
    lpc_barrier();
  };
  
  std::thread* threads[thread_n];
  
  for(int t=1; t < thread_n; t++)
    threads[t] = new std::thread{thread_fn, t};
  thread_fn(0);
  
  for(int t=1; t < thread_n; t++) {
    threads[t]->join();
    delete threads[t];
  }
  
  print_test_success();
  
  upcxx::finalize();
  return 0;
}
