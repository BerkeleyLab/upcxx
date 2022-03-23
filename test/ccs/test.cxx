#include <upcxx/upcxx.hpp>
#include "../util.hpp"
#include <dlfcn.h>

#define STR(s) #s
#define XSTR(s) STR(s)

int test_segment_function() { return 1; }
int dynamic_linked_function();

void upcxx_test2()
{
  upcxx::experimental::relocation::enforce_verification(true);
  upcxx::experimental::relocation::verify_all();
  bool printrank = (upcxx::rank_me() == 0) || (upcxx::rank_me() == upcxx::rank_n() - 1);
  if (printrank)
    upcxx::experimental::relocation::debug_write_segment_table();
  print_test_header();
  void* handle = dlopen(XSTR(CCS_DLOPEN_LIB), RTLD_NOW);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    exit(EXIT_FAILURE);
  }
  int (*dlopen_function)() = reinterpret_cast<int(*)()>(dlsym(handle, "dlopen_function"));
  int (*dlopen_cpp_function)() = reinterpret_cast<int(*)()>(dlsym(handle, "_Z19dlopen_cpp_functionv"));
  upcxx::experimental::relocation::verify_segment(dlopen_function);
  if (printrank)
    upcxx::experimental::relocation::debug_write_ptr(dlopen_function);
  auto fut1 = upcxx::rpc(0,test_segment_function);
  auto fut2 = upcxx::rpc(0,dynamic_linked_function);
  auto fut3 = upcxx::rpc(0,dlopen_function);
  auto fut4 = upcxx::rpc(0,dlopen_cpp_function);
  upcxx::when_all(fut1,fut2,fut3,fut4).wait();
  UPCXX_ASSERT_ALWAYS(fut1.result() == 1);
  UPCXX_ASSERT_ALWAYS(fut2.result() == 2);
  UPCXX_ASSERT_ALWAYS(fut3.result() == 3);
  UPCXX_ASSERT_ALWAYS(fut4.result() == 4);
  print_test_success();
}

void upcxx_test()
{
  upcxx::init();
  upcxx_test2();
  upcxx::finalize();
}
