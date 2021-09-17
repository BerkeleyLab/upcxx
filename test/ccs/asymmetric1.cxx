#include <upcxx/upcxx.hpp>
#include "../util.hpp"
#include <iostream>
#include <dlfcn.h>

#define STR(s) #s
#define XSTR(s) STR(s)

int main()
{
  upcxx::init();
  upcxx::experimental::relo::enforce_verification(true);
  if (upcxx::rank_n() < 2) {
    print_test_skipped("Requires >=2 ranks");
    return 0;
  }
  void (*dlopen_function)() = nullptr;
  if (upcxx::rank_me() == 0) {
    void* handle = dlopen(XSTR(CCS_DLOPEN_LIB), RTLD_NOW);
    if (!handle)
      throw std::runtime_error(dlerror());
    dlopen_function = reinterpret_cast<void(*)()>(dlsym(handle, "dlopen_function2"));
    if (!dlopen_function)
      throw std::runtime_error(dlerror());
  }
  upcxx::experimental::relo::verify_all();
  bool result = true;
  if (dlopen_function)
  {
    try {
      upcxx::rpc(1, dlopen_function).wait();
      result = false;
    }
    catch (const upcxx::segment_verification_error& e) {}
  }
  print_test_success(result);
  upcxx::finalize();
  return !result;
}
