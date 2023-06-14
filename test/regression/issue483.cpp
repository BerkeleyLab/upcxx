#include "../util.hpp"

using namespace upcxx;

struct A {
  static int live_count;
  A() {
    ++live_count;
  }
  A(const A&) {
    ++live_count;
  }
  ~A() {
    --live_count;
  }
};

int A::live_count = 0;

static int do_assignment();

future<> f0;
future<> f1 = make_future();
future<int> f2 = make_future(3);
future<double, char> f3 = make_future(3.5, 'x');
future<int> f4 = to_future(f2);
future<int> f5 = to_future(-2);
future<> f6 = when_all();
future<int> f7 = when_all(f2);
future<int, double, char, int, int, int> f8 =
  when_all(f1, f2, f3, f4, f5, f6, f7);
future<int, double, char, int, int, int> f9 = f8; // copy ctor
future<int, double, char, int, int, int> f10 = std::move(f8); // move ctor
int ignored = do_assignment();

static int do_assignment() {
  f8 = std::move(f10); // move assign
  f10 = f8; // copy assign
  // ctors/dtors
  auto f11 = f8;
  future<> f12 = make_future();
  future<int> f13 = make_future(8);
  future<> f15;
  future<int> f16;
  future<A> f17;
  future<A> f18 = make_future(A{});
  return 0;
}

future<A> f19;
future<A> f20 = make_future(A{});

static void check(const future<int, double, char, int, int, int> &fut) {
  UPCXX_ASSERT_ALWAYS(fut.is_ready());
  int i, j, k, m;
  double d;
  char c;
  std::tie(i, d, c, j, k, m) = fut.wait();
  UPCXX_ASSERT_ALWAYS(i == 3);
  UPCXX_ASSERT_ALWAYS(j == 3);
  UPCXX_ASSERT_ALWAYS(k == -2);
  UPCXX_ASSERT_ALWAYS(m == 3);
  UPCXX_ASSERT_ALWAYS(d == 3.5);
  UPCXX_ASSERT_ALWAYS(c == 'x');
}

int main() {
  UPCXX_ASSERT_ALWAYS(A::live_count == 1);

  init();
  print_test_header();

  UPCXX_ASSERT_ALWAYS(!f0.is_ready());
  check(f8);
  check(f9);
  check(f10);

  print_test_success();
  finalize();
}
