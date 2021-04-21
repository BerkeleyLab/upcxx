#include <upcxx/upcxx.hpp>
#include <iostream>

// demonstrate std::bad_alloc exception behavior on shared heap exhaustion
int main() {
  upcxx::init();

  if (!upcxx::rank_me()) {
    try {
      std::cout << "Making an absurd shared heap request..." << std::endl;
      upcxx::global_ptr<double> p = upcxx::new_array<double>((size_t)-4);
      std::cout << "ERROR: upcxx::new_array failed to throw exception!" << p << std::endl;
    } catch (std::bad_alloc const &e) {
      std::cout << "\nCaught exception: \n" << e.what() << std::endl;
    }
    try {
      std::cout << "\nStarting shared heap grab..." << std::endl;
      size_t tot = 0;
      for (size_t sz = 1; ; sz *= 2) {
        tot += sz;
        std::cout << tot << " bytes.." << std::endl;
        upcxx::global_ptr<char> leak = upcxx::new_array<char>(sz);
        if (!leak)
          std::cout << "ERROR: upcxx::new_array returned a nullptr!" << std::endl;
      }
    } catch (std::bad_alloc const &e) {
      std::cout << "\nCaught exception: \n" << e.what() << std::endl;
      #if RETHROW
        throw;
      #endif
    }
    std::cout << "\nSUCCESS" << std::endl;
  }

  upcxx::finalize();
}
