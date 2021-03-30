#include <string>
#include <upcxx/upcxx.hpp>

int main() {
  upcxx::init();

  std::string s;
  upcxx::global_ptr<std::string> gptr;
  upcxx::copy(gptr, gptr, 1).wait();
  
  upcxx::finalize();
}
