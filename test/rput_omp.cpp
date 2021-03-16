#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <atomic>
#include <vector> 

#include <omp.h>

#if !UPCXX_BACKEND_GASNET_PAR
  #error "UPCXX_BACKEND=gasnet_par required."
#endif

using namespace std; 

int main () {
	upcxx::init(); 
	print_test_header();
	
	const int n = upcxx::rank_n();
	const int me = upcxx::rank_me();
	
	int tn = os_env<int>("THREADS", 0);
	if(tn <= 0) tn = os_env<int>("OMP_NUM_THREADS", 10);
	
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
	
	std::atomic<int> done_count(0);
	
	omp_set_num_threads(tn);
	#pragma omp parallel num_threads(tn)
	{
		// all threads publish their default persona as worker
		int tid = omp_get_thread_num();
		workers[tid] = &upcxx::default_persona();
		#pragma omp barrier
		
		// launch one rput to each rank
		#pragma omp for 
		for(int i=0; i < n; i++) {
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
	}

	upcxx::barrier();

	for(int i=0; i < n; i++)
		UPCXX_ASSERT_ALWAYS(local[i] == (me+n-i)%n);
	
	print_test_success();
	upcxx::finalize(); 
}
