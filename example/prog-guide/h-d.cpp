#include <upcxx/upcxx.hpp> // to get UPCXX_VERSION
#if UPCXX_VERSION < 20210905
#error This test requires UPC++ 2021.9.5 or newer
#endif
//SNIPPET
#include <upcxx/upcxx.hpp>
#include <iostream>

#if !UPCXX_KIND_CUDA
#error "This example requires UPC++ to be built with CUDA support."
#endif

using namespace std;
using namespace upcxx;

int main() {
  upcxx::init();

  std::size_t segsize = 4*1024*1024; // 4 MiB
  auto gpu_alloc = upcxx::make_gpu_allocator<cuda_device>(segsize); // alloc GPU segment 
  UPCXX_ASSERT_ALWAYS(gpu_alloc.is_active(),
                      "Failed to open GPU:\n" << cuda_device::kind_info());

  // alloc some arrays of 1024 doubles on GPU and host
  global_ptr<double,memory_kind::cuda_device> gpu_array = gpu_alloc.allocate<double>(1024);
  global_ptr<double> host_array1 = new_array<double>(1024);
  global_ptr<double> host_array2 = new_array<double>(1024);

  double *h1 = host_array1.local();
  double *h2 = host_array2.local();
  for (int i=0; i< 1024; i++) h1[i] = i; //initialize h1

  // copy data from host memory to GPU
  upcxx::copy(host_array1, gpu_array, 1024).wait();
  // copy data back from GPU to host memory
  upcxx::copy(gpu_array, host_array2, 1024).wait();

  int nerrs = 0;
  for (int i=0; i<1024; i++){
    if (h1[i] != h2[i]){
      if (nerrs < 10) cout << "Error at element " << i << endl;
      nerrs++;
    }
  }
  if (nerrs) cout << "ERROR: " << nerrs << " errors detected" << endl;
  else if (!upcxx::rank_me()) cout << "SUCCESS" << endl;

  gpu_alloc.deallocate(gpu_array);
  delete_array(host_array2);
  delete_array(host_array1);

  gpu_alloc.destroy();
  upcxx::finalize();
}
//SNIPPET
