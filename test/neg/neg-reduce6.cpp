#include <string>
#include <upcxx/upcxx.hpp>

int main() {
  upcxx::init();

  std::string s1, s2;
  upcxx::reduce_all(&s1, &s2, 1, upcxx::op_fast_add).wait();
  
  upcxx::finalize();
}
