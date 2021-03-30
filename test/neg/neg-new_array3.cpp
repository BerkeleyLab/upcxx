#include <memory>
#include <upcxx/upcxx.hpp>

int main() {
  upcxx::init();

  upcxx::global_ptr<int[5]> p = upcxx::new_array<int[5]>(7, std::nothrow);
  
  upcxx::finalize();
}
