#include <memory>
#include <upcxx/upcxx.hpp>

int main() {
  upcxx::init();

  upcxx::global_ptr<int[5]> p = upcxx::new_<int[5]>(std::nothrow);
  
  upcxx::finalize();
}
