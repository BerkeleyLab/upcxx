#include <upcxx/upcxx.hpp>
upcxx::device_allocator<upcxx::cuda_device> gpu_allocator;  // global variable is prereq
int main(void) {
  upcxx::init();
  gpu_allocator = upcxx::make_gpu_allocator<upcxx::cuda_device>(32*1024*1024);
  upcxx::finalize();
  return 0;
}
