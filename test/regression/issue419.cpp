#include "../util.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unistd.h>

struct pidsay {
  std::stringstream ss;
  pidsay() {
    *this << "pid:" << (int)getpid() << ": ";
  }
  template<typename T>
  pidsay& operator<<(T const &that) {
    ss << that;
    return *this;
  }
  ~pidsay() {
    *this << "\n";
    std::cout << ss.str() << std::flush;
  }
};

struct A { 
  A() {
     pidsay() << "constructor("<<std::setw(18)<<this<<"): init=" << upcxx::initialized();
  }
  ~A(){ 
     pidsay() << "destructor ("<<std::setw(18)<<this<<"): init=" << upcxx::initialized();
     assert(!upcxx::initialized()); 
  }
};

A a1; // static data

int main() {
  pidsay() << "main()";
  A a2; // stack

  upcxx::init();
  print_test_header();

  pidsay() << "UPC++ process " << upcxx::rank_me() << "/" << upcxx::rank_n();

  print_test_success();
  upcxx::finalize();
  
  pidsay() << "post-finalize";
  return 0;
}

