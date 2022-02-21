#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"

int main() {
  upcxx::init();
  print_test_header();

  std::size_t x = upcxx::cuda_device::default_alignment<int>();
  std::cout << "upcxx::cuda_device::default_alignment<int> = " << x << std::endl;
  assert(x > 0);

  print_test_success();
  upcxx::finalize();
  return 0;
}
