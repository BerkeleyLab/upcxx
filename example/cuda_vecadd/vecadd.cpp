#include <upcxx/upcxx.hpp>
#include <iostream>

#include "kernels.hpp"

/*
 * A simple (inefficient) distributed vector add example.
 *
 * Initializes a host array on PE 0.
 *
 * Then distributes chunks of that array to GPUs on each PE (pull-based).
 *
 * Computes a vector add on each PEs chunk, and then sends the results back to
 * PE 0 for validation.
 */

using namespace std;
using namespace upcxx; 

int main() {
   upcxx::init();

   int N = 1024;
   std::size_t segsize = 3*N*sizeof(double);

   if (!rank_me()) 
       std::cout<<"Running vecadd with "<<rank_n()<<" processes, N="<<N<<" segsize="<<segsize<<std::endl;

   upcxx::barrier();

   auto gpu_device = upcxx::cuda_device( 0 ); // open device 0 (or other args TBD)
   {
       // alloc GPU segment
       auto gpu_alloc = upcxx::device_allocator<upcxx::cuda_device>(gpu_device,
               segsize);

       global_ptr<double,memory_kind::cuda_device> dA =
           gpu_alloc.allocate<double>(N);
       UPCXX_ASSERT(dA);
       global_ptr<double,memory_kind::cuda_device> dB =
           gpu_alloc.allocate<double>(N);
       UPCXX_ASSERT(dB);
       global_ptr<double,memory_kind::cuda_device> dC =
           gpu_alloc.allocate<double>(N);
       UPCXX_ASSERT(dC);

       if (rank_me() == 0) {
           initialize_device_arrays(gpu_alloc.local(dA),
                   gpu_alloc.local(dB), N);
       }

       upcxx::dist_object<upcxx::global_ptr<double, memory_kind::cuda_device>> dobjA(dA);
       upcxx::dist_object<upcxx::global_ptr<double, memory_kind::cuda_device>> dobjB(dB);
       upcxx::dist_object<upcxx::global_ptr<double, memory_kind::cuda_device>> dobjC(dC);

       global_ptr<double,memory_kind::cuda_device> root_dA = dobjA.fetch(0).wait();
       global_ptr<double,memory_kind::cuda_device> root_dB = dobjB.fetch(0).wait();
       global_ptr<double,memory_kind::cuda_device> root_dC = dobjC.fetch(0).wait();

       upcxx::barrier();

       // transfer values from device on process 0 to device on this process
       if (rank_me() != 0) {
           upcxx::when_all(
               upcxx::copy(root_dA, dA, N),
               upcxx::copy(root_dB, dB, N),
               upcxx::copy(root_dC, dC, N)
           ).wait();
       }

       double *hA = (double *)malloc(N * sizeof(*hA));
       UPCXX_ASSERT(hA);
       double *hB = (double *)malloc(N * sizeof(*hB));
       UPCXX_ASSERT(hB);
       double *hC = (double *)malloc(N * sizeof(*hC));
       UPCXX_ASSERT(hC);

       // Validate that the incoming data transferred successfully
       upcxx::when_all(
           upcxx::copy(dA, hA, N),
           upcxx::copy(dB, hB, N)
       ).wait();

       for (int i = 0; i < N; i++) {
           UPCXX_ASSERT(hA[i] == i);
           UPCXX_ASSERT(hB[i] == 2 * i);
       }

       int chunk_size = (N + rank_n() - 1) / rank_n();
       int my_chunk_start = rank_me() * chunk_size;
       int my_chunk_end = (rank_me() + 1) * chunk_size;
       if (my_chunk_end > N) my_chunk_end = N;

       gpu_vector_sum(gpu_alloc.local(dA), gpu_alloc.local(dB),
               gpu_alloc.local(dC), my_chunk_start, my_chunk_end);

       // Validate that my part of the vec add completed
       upcxx::when_all(
           upcxx::copy(dA, hA, N),
           upcxx::copy(dB, hB, N),
           upcxx::copy(dC, hC, N)
       ).wait();

       for (int i = my_chunk_start; i < my_chunk_end; i++) {
           UPCXX_ASSERT(hA[i] == i);
           UPCXX_ASSERT(hB[i] == 2 * i);
           UPCXX_ASSERT(hC[i] == hA[i] + hB[i]);
       }

       // Push back to the root GPU
       if (rank_me() != 0) {
           upcxx::copy(dC + my_chunk_start, root_dC + my_chunk_start,
                       my_chunk_end - my_chunk_start).wait();
       }

       upcxx::barrier();

       // Validate on PE 0
       if (rank_me() == 0) {
           upcxx::copy(dC, hC, N).wait();

           int count_errs = 0;
           for (int i = 0; i < N; i++) {
               if (hC[i] != i + 2 * i) {
                   fprintf(stderr, "Error @ %d. expected %f, got %f\n", i,
                           (double)(i + 2 * i), hC[i]);
                   count_errs++;
               }
           }

           if (count_errs == 0) {
               std::cout << "SUCCESS" << std::endl;
           }
       }

       free(hA);
       free(hB);
       free(hC);

       gpu_alloc.deallocate(dA);
       gpu_alloc.deallocate(dB);
       gpu_alloc.deallocate(dC);
   }

   gpu_device.destroy();
   upcxx::finalize();
}
