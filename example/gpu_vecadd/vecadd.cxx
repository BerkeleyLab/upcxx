#include <upcxx/upcxx.hpp>
#include <iostream>

#if UPCXX_VERSION < 20210905
#error This test requires UPC++ 2021.9.5 or newer
#endif

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
using gp_device = upcxx::global_ptr<double, gpu_default_device::kind>;
extern ze_device::device_handle_t handle_init();

#if UPCXX_GPU_DEFAULT_DEVICE_ZE
extern ze_device::device_handle_t handle_init();
#endif

int main() {
   upcxx::init();

   int N = 1024;
   std::size_t segsize = 3*N*sizeof(double);

   if (!rank_me()) 
       std::cout<<"Running vecadd with "<<rank_n()<<" processes, N="<<N<<" segsize="<<segsize<<std::endl;

   upcxx::barrier();

   // alloc GPU segment
#if UPCXX_GPU_DEFAULT_DEVICE_ZE
   ze_device::device_handle_t handle = handle_init();
   ze_device::id_type dev_id = ze_device::device_handle_to_device_id(handle);
   auto gpu_alloc = upcxx::make_gpu_allocator(segsize, dev_id);
#else
   auto gpu_alloc = upcxx::make_gpu_allocator(segsize);
#endif
   int gpu_dev = gpu_alloc.device_id();
   UPCXX_ASSERT_ALWAYS(gpu_alloc.is_active(),
                       "Failed to open GPU:\n" << gpu_default_device::kind_info());

   gp_device dA = gpu_alloc.allocate<double>(N);
   UPCXX_ASSERT_ALWAYS(dA);
   gp_device dB = gpu_alloc.allocate<double>(N);
   UPCXX_ASSERT_ALWAYS(dB);
   gp_device dC = gpu_alloc.allocate<double>(N);
   UPCXX_ASSERT_ALWAYS(dC);

   if (rank_me() == 0) {
       initialize_device_arrays(gpu_alloc.local(dA),
               gpu_alloc.local(dB), N, gpu_dev);
   }

   upcxx::dist_object<gp_device> dobjA(dA);
   upcxx::dist_object<gp_device> dobjB(dB);
   upcxx::dist_object<gp_device> dobjC(dC);

   gp_device root_dA = dobjA.fetch(0).wait();
   gp_device root_dB = dobjB.fetch(0).wait();
   gp_device root_dC = dobjC.fetch(0).wait();

   upcxx::barrier();

   // transfer values from device on process 0 to device on this process
   if (rank_me() != 0) {
       upcxx::when_all(
           upcxx::copy(root_dA, dA, N),
           upcxx::copy(root_dB, dB, N)
       ).wait();
   }

   double *hA = new double[N];
   double *hB = new double[N];
   double *hC = new double[N];

   // Validate that the incoming data transferred successfully
   upcxx::when_all(
       upcxx::copy(dA, hA, N),
       upcxx::copy(dB, hB, N)
   ).wait();

   for (int i = 0; i < N; i++) {
       UPCXX_ASSERT_ALWAYS(hA[i] == i);
       UPCXX_ASSERT_ALWAYS(hB[i] == 2 * i);
   }

   int chunk_size = (N + rank_n() - 1) / rank_n();
   int my_chunk_start = rank_me() * chunk_size;
   int my_chunk_end = (rank_me() + 1) * chunk_size;
   if (my_chunk_end > N) my_chunk_end = N;

   gpu_vector_sum(gpu_alloc.local(dA), gpu_alloc.local(dB),
           gpu_alloc.local(dC), my_chunk_start, my_chunk_end,
           gpu_dev);

   // Validate that my part of the vec add completed
   upcxx::when_all(
       upcxx::copy(dA, hA, N),
       upcxx::copy(dB, hB, N),
       upcxx::copy(dC, hC, N)
   ).wait();

   for (int i = my_chunk_start; i < my_chunk_end; i++) {
       UPCXX_ASSERT_ALWAYS(hA[i] == i);
       UPCXX_ASSERT_ALWAYS(hB[i] == 2 * i);
       UPCXX_ASSERT_ALWAYS(hC[i] == hA[i] + hB[i]);
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

   delete[] hA;
   delete[] hB;
   delete[] hC;

   gpu_alloc.deallocate(dA);
   gpu_alloc.deallocate(dB);
   gpu_alloc.deallocate(dC);

   gpu_alloc.destroy();
   upcxx::finalize();
}
