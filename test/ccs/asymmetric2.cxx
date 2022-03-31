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
  int (*dlopen_function)() = nullptr;
  if (upcxx::rank_me() == 0) {
    void* handle = dlopen(XSTR(CCS_DLOPEN_LIB), RTLD_NOW);
    if (!handle)
      throw std::runtime_error(dlerror());
    dlopen_function = reinterpret_cast<int(*)()>(dlsym(handle, "dlopen_function"));
    if (!dlopen_function)
      throw std::runtime_error(dlerror());
  }
  bool result = true;
  try {
    upcxx::experimental::relo::verify_segment(dlopen_function);
    result = false;
  } catch (const upcxx::segment_verification_error& e)
  {
  } catch(const std::exception& e)
  {
    std::cout << e.what() << std::endl;
    result = false;
  }
  print_test_success(result);
  if (!result)
    upcxx::experimental::relo::debug_write_ptr(dlopen_function);
  upcxx::finalize();
  return !result;
}
