#include <upcxx/upcxx.hpp> 

#include "util.hpp"

#include <atomic>
#include <thread>
#include <vector> 

#include <sched.h>

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

using namespace std; 

template<typename Fn>
void run_threads(int tn, Fn &&fn) {
	std::vector<std::thread*> ts;
	ts.resize(tn);
	
	for(int ti=1; ti < tn; ti++)
		ts[ti] = new std::thread(fn, ti);
	fn(0);
	
	for(int ti=1; ti < tn; ti++) {
		ts[ti]->join();
		delete ts[ti];
	}
}

int main () {
	upcxx::init(); 
	print_test_header();
	
	const int n = upcxx::rank_n();
	const int me = upcxx::rank_me();
	
	int tn = os_env<int>("THREADS", 10);
	
	if(me == 0)
		std::cout<<"Threads: "<<tn<<'\n';
	
	vector<upcxx::global_ptr<int>> ptrs(n); 
	ptrs[me] = upcxx::new_array<int>(n); 
	for(int i=0; i < n; i++)
		ptrs[i] = upcxx::broadcast(ptrs[i], i).wait(); 
	
	int *local = ptrs[me].local(); 
	for(int i=0; i < n; i++)
		local[i] = -1;
	
	upcxx::barrier(); 
	
	std::vector<upcxx::persona*> workers;
	workers.resize(tn);
	
	std::atomic<int> tbarrier1(0);
	std::atomic<int> work_q(0);
	std::atomic<int> done_count(0);
	
	run_threads(tn, [&](int tid) {
		// all threads publish their default persona as worker
		workers[tid] = &upcxx::default_persona();
		
		tbarrier1 += 1;
		while(tbarrier1.load(std::memory_order_acquire) != tn)
			sched_yield();
		
		// launch one rput to each rank
		// threads race to acquire jobs indexed i=0..n-1
		while(true) {
			int i = work_q++;
			if(i >= n) break;
			
			upcxx::rput(
				&me, ptrs[(me + i)%n] + i, 1,
				// rput is resolved in continuation on another thread
				upcxx::operation_cx::as_lpc(
					*workers[(tid + i) % tn],
					[&,i]() {
						// fetch the value just put
						upcxx::rget(ptrs[(me + i)%n] + i).then(
							[&](int got) {
								UPCXX_ASSERT_ALWAYS(got == me);
								done_count += 1;
							}
						);
					}
				)
			);
		}
		
		// each thread drains progress until all work quiesced
		while(done_count.load(std::memory_order_relaxed) != n)
			upcxx::progress();
	});

	upcxx::barrier();

	for(int i=0; i < n; i++)
		UPCXX_ASSERT_ALWAYS(local[i] == (me+n-i)%n);
	
	print_test_success();
	upcxx::finalize(); 
}
