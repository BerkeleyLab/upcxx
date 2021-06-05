#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  { persona p; persona_scope ps(p);
    auto g1 = upcxx::new_<int>(42);
    upcxx::delete_(g1);
    auto g2 = upcxx::new_<int>(std::nothrow, 42);
    upcxx::delete_(g2);
    auto g3 = upcxx::new_array<int>(10);
    upcxx::delete_array(g3);
    auto g4 = upcxx::new_array<int>(10, std::nothrow);
    upcxx::delete_array(g4);
    auto g5 = upcxx::allocate<int>(10);
    upcxx::deallocate(g5);
    auto l1 = upcxx::allocate(128);
    upcxx::deallocate(l1);
  } 
  print_test_success();
  
  upcxx::finalize();
  return 0;
}
