#include <upcxx/upcxx.hpp>
#include <iostream>
#include "util.hpp"

int main() {
  upcxx::init();
  print_test_header();
 
  if (!upcxx::rank_me()) 
    std::cout << "UPCXX_VERSION=" << UPCXX_VERSION << "\n"
              << "UPCXX_SPEC_VERSION=" << UPCXX_SPEC_VERSION << std::endl;

  constexpr long release_version = UPCXX_VERSION;
  constexpr long spec_version = UPCXX_SPEC_VERSION;

  assert(release_version == upcxx::release_version());
  assert(spec_version == upcxx::spec_version());

  print_test_success(); 
  upcxx::finalize();
  return 0;
}
