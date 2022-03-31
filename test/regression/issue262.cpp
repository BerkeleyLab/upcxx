#include <iostream>

#include "../util.hpp"

using namespace upcxx;

int done = 0;

struct A {
  int x;
  ~A() {
    x = 1;
    if (upcxx::initialized()) {
      say() << "destroyed " << this;
    }
  }
  operator int() const {
    return x;
  }
  UPCXX_SERIALIZED_FIELDS(x)
};

int main() {
  say() << "Hello before init!";
  upcxx::init();
  print_test_header();

  int target = (upcxx::rank_me()+1)%upcxx::rank_n();
  int data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  A v{-1};
  global_ptr<int> ptr = new_<int>(0);
  dist_object<global_ptr<int>> dptr(ptr);
  global_ptr<int> dst = dptr.fetch(target).wait();
  auto cx_fn =
    [](view<int> items, const A &a, global_ptr<int> src) {
      return rget(src).then(
        [items,&a](int) {
          say() << "processing items, &a = " << &a;
          UPCXX_ASSERT_ALWAYS(a.x == -1);
          auto p = items.begin();
          for (int i = 0; p < items.end(); ++i, ++p) {
            UPCXX_ASSERT_ALWAYS(*p == i);
          }
          ++done;
        });
    };
  auto void_fn = [](){};
  auto non_fut_fn = []() { return 1; };
  rput(1, dst,
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr));
  rput(1, dst,
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr) |
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr));
  rput(1, dst,
       remote_cx::as_rpc(void_fn) |
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr));
  rput(1, dst,
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr) |
       remote_cx::as_rpc(void_fn));
  rput(1, dst,
       remote_cx::as_rpc(non_fut_fn) |
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr));
  rput(1, dst,
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr) |
       remote_cx::as_rpc(non_fut_fn));
  (void)rput(1, dst,
       operation_cx::as_future() |
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr));
  (void)rput(1, dst,
       remote_cx::as_rpc(cx_fn, make_view(data, data+10), v, ptr) |
       operation_cx::as_future());
  while (done != 9) {
    progress();
  }
  barrier();
  delete_(ptr);

  print_test_success();
  upcxx::finalize();
  say() << "Goodbye after finalize";
}
