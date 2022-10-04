#include <upcxx/upcxx.hpp>

#include <atomic>
#include <vector> 

#include <omp.h>

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

using namespace std;
using upcxx::experimental::os_env;

vector<upcxx::global_ptr<int>> setup_pointers(const int n) {
  vector<upcxx::global_ptr<int>> ptrs(n); 
  ptrs[upcxx::rank_me()] = upcxx::new_array<int>(n); 
  for(int i=0; i < n; i++)
    ptrs[i] = upcxx::broadcast(ptrs[i], i).wait(); 

  int *local = ptrs[upcxx::rank_me()].local(); 
  for(int i=0; i < n; i++)
    local[i] = -1;

  upcxx::barrier();
  return ptrs;
}

int main () {
  upcxx::init(); 

  int thread_count = os_env<int>("THREADS", os_env<int>("OMP_NUM_THREADS", 10));
  if(upcxx::rank_me() == 0) std::cout<<"Threads: "<<thread_count<<std::endl;
  omp_set_dynamic(0); // required to guarantee exact OMP parallel thread count
//SNIPPET
  const int n = upcxx::rank_n();
  const int me = upcxx::rank_me();
  const int tn = thread_count;  // threads per process

  vector<upcxx::global_ptr<int>> ptrs = setup_pointers(n);
  std::vector<upcxx::persona*> workers(tn);

  std::atomic<int> done_count(0);

  #pragma omp parallel num_threads(tn)
  {
    // all threads publish their default persona as worker
    int tid = omp_get_thread_num();
    workers[tid] = &upcxx::default_persona();
    #pragma omp barrier

    // launch one rput to each rank
    #pragma omp for 
    for(int i=0; i < n; i++) {
      upcxx::rput(&me, ptrs[(me + i)%n] + me, 1,
        // rput is resolved in continuation on another thread
        upcxx::operation_cx::as_lpc(*workers[(tid + i) % tn], [&,i]() {
          // fetch the value just put
          upcxx::rget(ptrs[(me + i)%n] + me).then([&](int got) {
            UPCXX_ASSERT_ALWAYS(got == me);
            done_count++;
          });
        })
      );
    }

    // each thread drains progress until all work is quiesced
    while(done_count.load(std::memory_order_relaxed) != n)
      upcxx::progress();
    } // <-- this closing brace implies an OpenMP thread barrier

  upcxx::barrier();
//SNIPPET
  int *local = ptrs[upcxx::rank_me()].local(); 
  for(int i=0; i < n; i++)
    UPCXX_ASSERT_ALWAYS(local[i] == i);

  upcxx::barrier();
  if (me == 0) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize(); 
}
