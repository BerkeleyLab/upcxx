#include <upcxx/upcxx.hpp>
#include "../util.hpp"
#include <unistd.h>

// global variable is prereq for issue 633
upcxx::device_allocator<upcxx::cuda_device> cuda_allocator;
upcxx::device_allocator<upcxx::hip_device>  hip_allocator;
upcxx::device_allocator<upcxx::ze_device>   ze_allocator;

int main(void) {
  upcxx::init();
  print_test_header();

  say() << "PID: " << getpid();

  std::size_t sz = 32*1024*1024;

  cuda_allocator = upcxx::make_gpu_allocator<upcxx::cuda_device>(sz);
  hip_allocator  = upcxx::make_gpu_allocator<upcxx::hip_device>(sz);
  ze_allocator   = upcxx::make_gpu_allocator<upcxx::ze_device>(sz);

  print_test_success();
  upcxx::finalize();
  return 0;
}
