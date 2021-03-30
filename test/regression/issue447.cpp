#include <upcxx/upcxx.hpp>
#include "../util.hpp"

int main() {
  upcxx::init();
  print_test_header();
  int src = 0xbeef;
  upcxx::global_ptr<int> dst = upcxx::new_<int>();
  auto cx = upcxx::operation_cx::as_future();
  #if BYVAL
    auto f = upcxx::rput(src, dst, cx);
  #else
    auto f = upcxx::rput(&src, dst, 1, cx);
  #endif
  f.wait();
  UPCXX_ASSERT_ALWAYS(*dst.local() == src);
  upcxx::delete_(dst);
  print_test_success();
  upcxx::finalize();
  return 0;
}
