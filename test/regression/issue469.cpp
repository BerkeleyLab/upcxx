#include "../util.hpp"

using namespace upcxx;

static void check(const future<int, double, char, int> &fut) {
  UPCXX_ASSERT_ALWAYS(fut.is_ready());
  auto [i, d, c, j] = fut.wait();
  UPCXX_ASSERT_ALWAYS(i == 3);
  UPCXX_ASSERT_ALWAYS(d == 3.5);
  UPCXX_ASSERT_ALWAYS(c == 'x');
  UPCXX_ASSERT_ALWAYS(j == -2);
}

static void ref_check(const future<int, double, char, int> &fut) {
  UPCXX_ASSERT_ALWAYS(fut.is_ready());
  auto [i, d, c, j] = fut.wait_reference();
  UPCXX_ASSERT_ALWAYS(i == 3);
  UPCXX_ASSERT_ALWAYS(d == 3.5);
  UPCXX_ASSERT_ALWAYS(c == 'x');
  UPCXX_ASSERT_ALWAYS(j == -2);
}

int main() {
  init();
  print_test_header();

  future<int,double,char,int> fut = make_future(3,3.5,'x',-2);
  check(fut);
  ref_check(fut);

  print_test_success();
  finalize();
}
