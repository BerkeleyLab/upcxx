#include "../util.hpp"
#include <iostream>

struct A {
  int x;
  A(int v) : x(v) {}
  A(A &&other) : x(other.x){} // move cons
  #if COPYABLE
    A(const A &) = default;
  #else
    A(const A &) = delete; // non-copyable
  #endif
  UPCXX_SERIALIZED_VALUES(x)
};

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  rpc(0,[](A const &a) { 
        int val = a.x;
        std::cout << val << std::endl;
        assert(val == 10); 
        }, A(10)).wait();

  print_test_success();
  upcxx::finalize();
  return 0;
}
