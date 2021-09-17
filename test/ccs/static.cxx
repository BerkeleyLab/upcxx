#include <upcxx/upcxx.hpp>
#include "../util.hpp"
#include <dlfcn.h>

#define STR(s) #s
#define XSTR(s) STR(s)

int test_segment_function() { return 1; }

int main()
{
  upcxx::init();
  print_test_header();
  // Static executables can dlopen things. In doing so, they will introduce their dynamically linked
  // dependencies, but this doesn't mean the main executable was dynamically linked by mistake. Without
  // dlopen, the segment table would contain just the executable and vDSO.
  void* handle = dlopen(XSTR(CCS_DLOPEN_LIB), RTLD_NOW);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    exit(EXIT_FAILURE);
  }
  int (*dlopen_function)() = reinterpret_cast<int(*)()>(dlsym(handle, "dlopen_function"));
  int (*dlopen_cpp_function)() = reinterpret_cast<int(*)()>(dlsym(handle, "_Z19dlopen_cpp_functionv"));
  upcxx::experimental::relocation::verify_segment(dlopen_function);
  auto fut1 = upcxx::rpc(0,test_segment_function);
  auto fut3 = upcxx::rpc(0,dlopen_function);
  auto fut4 = upcxx::rpc(0,dlopen_cpp_function);
  upcxx::when_all(fut1,fut3,fut4).wait();
  UPCXX_ASSERT_ALWAYS(fut1.result() == 1);
  UPCXX_ASSERT_ALWAYS(fut3.result() == 3);
  UPCXX_ASSERT_ALWAYS(fut4.result() == 4);
  upcxx::experimental::relo::debug_write_segment_table();
  print_test_success();
  upcxx::finalize();
  return 0;
}
