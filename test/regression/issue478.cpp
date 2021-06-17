// None of the internal invariants evaluated by this test are
// specified, and as such they are subject to change without notice.

#include "../util.hpp"

using namespace upcxx;

// the following use unsupported internals and are UNSAFE UPC++
#define ASSERT_SAME(f1, f2) \
  UPCXX_ASSERT_ALWAYS((f1).impl_.hdr_ == (f2).impl_.hdr_)
#define ASSERT_NOT_SAME(f1, f2) \
  UPCXX_ASSERT_ALWAYS((f1).impl_.hdr_ != (f2).impl_.hdr_)

future<> global_ready_empty = make_future();
future<> global_nonready_empty;
future<int> global_ready_nonempty = make_future(3);
future<int> global_nonready_nonempty;
future<> global_ready_empty2 =
  when_all(global_ready_empty, global_ready_empty);
future<> global_nonready_empty2 =
  when_all(global_ready_empty, global_nonready_empty);
future<int> global_ready_nonempty2 =
  when_all(global_ready_empty, global_ready_nonempty);
future<int> global_nonready_nonempty2 =
  when_all(global_ready_empty, global_nonready_nonempty);
future<> global_ready_empty3 = when_all();
future<> global_ready_empty4 = to_future(global_ready_empty3);

int main() {
  upcxx::init();
  print_test_header();

  future<> ready_empty = make_future();
  future<int> ready_nonempty = make_future(3);
  future<int,int> ready_nonempty2 = make_future(3,4);
  promise<> prom1;
  future<> nonready_empty = prom1.get_future();
  promise<int> prom2;
  future<int> nonready_nonempty = prom2.get_future();
  promise<int,int> prom3;
  future<int,int> nonready_nonempty2 = prom3.get_future();

  {
    // make_future() and when_all()
    future<> f1 = make_future();
    ASSERT_SAME(f1, ready_empty);
    future<> f2 = when_all();
    ASSERT_SAME(f2, ready_empty);
  }

  {
    // singleton
    future<> f1 = when_all(ready_empty);
    ASSERT_SAME(f1, ready_empty);
    future<> f2 = when_all(nonready_empty);
    ASSERT_SAME(f2, nonready_empty);
    future<int> f3 = when_all(ready_nonempty);
    ASSERT_SAME(f3, ready_nonempty);
    future<int> f4 = when_all(nonready_nonempty);
    ASSERT_SAME(f4, nonready_nonempty);
    future<int,int> f5 = when_all(ready_nonempty2);
    ASSERT_SAME(f5, ready_nonempty2);
    future<int,int> f6 = when_all(nonready_nonempty2);
    ASSERT_SAME(f6, nonready_nonempty2);
  }

  {
    // ready empty + other
    future<> f1 = when_all(ready_empty, ready_empty);
    ASSERT_SAME(f1, ready_empty);
    future<> f2 = when_all(ready_empty, nonready_empty);
    ASSERT_SAME(f2, nonready_empty);
    future<int> f3 = when_all(ready_empty, ready_nonempty);
    ASSERT_SAME(f3, ready_nonempty);
    future<int> f4 = when_all(ready_empty, nonready_nonempty);
    ASSERT_SAME(f4, nonready_nonempty);
    future<int,int> f5 = when_all(ready_empty, ready_nonempty2);
    ASSERT_SAME(f5, ready_nonempty2);
    future<int,int> f6 = when_all(ready_empty, nonready_nonempty2);
    ASSERT_SAME(f6, nonready_nonempty2);
  }

  {
    // other + ready empty
    future<> f2 = when_all(nonready_empty, ready_empty);
    ASSERT_SAME(f2, nonready_empty);
    future<int> f3 = when_all(ready_nonempty, ready_empty);
    ASSERT_SAME(f3, ready_nonempty);
    future<int> f4 = when_all(nonready_nonempty, ready_empty);
    ASSERT_SAME(f4, nonready_nonempty);
    future<int,int> f5 = when_all(ready_nonempty2, ready_empty);
    ASSERT_SAME(f5, ready_nonempty2);
    future<int,int> f6 = when_all(nonready_nonempty2, ready_empty);
    ASSERT_SAME(f6, nonready_nonempty2);
  }

  {
    // ready nonempty + ready nonempty
    future<int,int> f1 = when_all(ready_nonempty, ready_nonempty);
    ASSERT_NOT_SAME(f1, ready_nonempty);
    future<int,int,int> f2 = when_all(ready_nonempty, ready_nonempty2);
    ASSERT_NOT_SAME(f2, ready_nonempty);
    ASSERT_NOT_SAME(f2, ready_nonempty2);
    future<int,int,int> f3 = when_all(ready_nonempty2, ready_nonempty);
    ASSERT_NOT_SAME(f3, ready_nonempty);
    ASSERT_NOT_SAME(f3, ready_nonempty2);
  }

  {
    // nonready + nonready
    future<> f1 = when_all(nonready_empty, nonready_empty);
    ASSERT_NOT_SAME(f1, nonready_empty);
    future<int> f2 = when_all(nonready_empty, nonready_nonempty);
    ASSERT_NOT_SAME(f2, nonready_empty);
    ASSERT_NOT_SAME(f2, nonready_nonempty);
    future<int> f3 = when_all(nonready_nonempty, nonready_empty);
    ASSERT_NOT_SAME(f3, nonready_empty);
    ASSERT_NOT_SAME(f3, nonready_nonempty);
    future<int,int> f4 = when_all(nonready_nonempty, nonready_nonempty);
    ASSERT_NOT_SAME(f4, nonready_nonempty);
    future<int,int,int> f5 = when_all(nonready_nonempty, nonready_nonempty2);
    ASSERT_NOT_SAME(f5, nonready_nonempty);
    ASSERT_NOT_SAME(f5, nonready_nonempty2);
  }

  {
    // two ready, one nonready
    future<> f1 = when_all(nonready_empty, ready_empty, ready_empty);
    ASSERT_SAME(f1, nonready_empty);
    future<> f2 = when_all(ready_empty, nonready_empty, ready_empty);
    ASSERT_SAME(f2, nonready_empty);
    future<> f3 = when_all(ready_empty, ready_empty, nonready_empty);
    ASSERT_SAME(f3, nonready_empty);
    future<int> f4 = when_all(nonready_nonempty, ready_empty, ready_empty);
    ASSERT_SAME(f4, nonready_nonempty);
    future<int> f5 = when_all(ready_empty, nonready_nonempty, ready_empty);
    ASSERT_SAME(f5, nonready_nonempty);
    future<int> f6 = when_all(ready_empty, ready_empty, nonready_nonempty);
    ASSERT_SAME(f6, nonready_nonempty);
    future<int> f7 = when_all(nonready_empty, ready_empty, ready_nonempty);
    ASSERT_NOT_SAME(f7, nonready_empty);
    ASSERT_NOT_SAME(f7, ready_empty);
    ASSERT_NOT_SAME(f7, ready_nonempty);
    future<int> f8 = when_all(ready_empty, ready_nonempty, nonready_empty);
    ASSERT_NOT_SAME(f8, nonready_empty);
    ASSERT_NOT_SAME(f8, ready_empty);
    ASSERT_NOT_SAME(f8, ready_nonempty);
    future<int> f9 = when_all(ready_nonempty, nonready_empty, ready_empty);
    ASSERT_NOT_SAME(f9, nonready_empty);
    ASSERT_NOT_SAME(f9, ready_empty);
    ASSERT_NOT_SAME(f9, ready_nonempty);
    future<int,int> f10 = when_all(nonready_nonempty2, ready_empty, ready_empty);
    ASSERT_SAME(f10, nonready_nonempty2);
    future<int,int> f11 = when_all(ready_empty, nonready_nonempty2, ready_empty);
    ASSERT_SAME(f11, nonready_nonempty2);
    future<int,int> f12 = when_all(ready_empty, ready_empty, nonready_nonempty2);
    ASSERT_SAME(f12, nonready_nonempty2);
  }

  {
    // dynamic initialization
    ASSERT_SAME(global_ready_empty, ready_empty);
    ASSERT_NOT_SAME(global_nonready_empty, ready_empty);
    ASSERT_NOT_SAME(global_nonready_empty, nonready_empty);
    ASSERT_SAME(global_ready_empty2, ready_empty);
    ASSERT_SAME(global_nonready_empty2, global_nonready_empty);
    ASSERT_SAME(global_ready_nonempty2, global_ready_nonempty);
    ASSERT_SAME(global_nonready_nonempty2, global_nonready_nonempty);
    ASSERT_SAME(global_ready_empty3, ready_empty);
    ASSERT_SAME(global_ready_empty4, ready_empty);
  }

  print_test_success();
  upcxx::finalize();
}
