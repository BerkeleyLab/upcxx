#include "../util.hpp"
#include <iostream>
#include <memory>

struct A {
  int x;
  A(int v) : x(v) {}
  A(A &&other) : x(other.x){} // move cons
  A(const A &) = delete; // non-copyable
  UPCXX_SERIALIZED_VALUES(x)
};

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  // demonstrates that future can contain a non-copyable type (std::unique_ptr)
  future<std::unique_ptr<int>> boof;
  {
    std::unique_ptr<int> up(new int(17));
    boof = make_future(std::move(up)); // trivially ready future
  }
  assert(boof.is_ready());
  assert(*boof.result_reference() == 17);
  {
    promise<std::unique_ptr<int>> p;
    boof = p.get_future(); // real non-ready future
    assert(!boof.is_ready());
    std::unique_ptr<int> up(new int(42));
    p.fulfill_result(std::move(up));
  }
  assert(boof.is_ready());
  assert(*boof.result_reference() == 42);

  // demonstrate with a custom non-copyable type
  future<A> fa;
  { 
    fa = make_future(A(17)); // trivially ready future
  }
  assert(fa.result_reference().x == 17);
  {
    promise<A> p;
    fa = p.get_future(); // real non-ready future
    assert(!fa.is_ready());
    p.fulfill_result(A(16));
  }
  assert(fa.result_reference().x == 16);

  // demonstrate with a custom non-copyable type and multiple values
  future<A,A> fb;
  { 
    fb = make_future(A(1),A(2)); // trivially ready future
  }
  assert(fb.result_reference<0>().x == 1);
  assert(fb.result_reference<1>().x == 2);
  {
    promise<A,A> p;
    fb = p.get_future(); // real non-ready future
    assert(!fb.is_ready());
    p.fulfill_result(A(3),A(4));
  }
  assert(fb.result_reference<0>().x == 3);
  assert(fb.result_reference<1>().x == 4);

  { 
    fa = when_all(A(6)); // trivially ready future
  }
  assert(fa.result_reference().x == 6);
  fa = when_all(fa);
  assert(fa.result_reference().x == 6);

  { 
    fb = when_all(A(21),A(22)); // trivially ready future
  }
  assert(fb.result_reference<0>().x == 21);
  assert(fb.result_reference<1>().x == 22);
  fb = when_all(fb);
  assert(fb.result_reference<0>().x == 21);
  assert(fb.result_reference<1>().x == 22);


#if 0 // currently broken on GCC 9 with -O (Issue #400)
  // now try rpc
  rpc(0,[](A const &a) { 
              assert(a.x == 10); 
        }, A(10)).wait(); // works in develop
  A tmp(11);
  rpc(0,[](A const &a, A const &b) { 
              assert(a.x == 10); 
              assert(b.x == 11); 
        }, A(10), std::move(tmp)).wait(); // works in develop
  future<A> fx = rpc((rank_me()+1)%rank_n(),[]() -> A&& {
                   static A a(13);
                   return std::move(a); 
                 });
  assert(fx.wait_reference().x == 13);

  future<A> fy = rpc(0,[](A &&a) -> A&& { 
                    assert(a.x == 22);
                    return std::move(a); 
                }, A(22));
  assert(fy.wait_reference().x == 22);

  future<A,A,A> fz = rpc((rank_me()+1)%rank_n(),
                      [](A &&a, A &&b) { 
                    static A c(6);
                    return make_future(std::move(a),std::move(b),std::move(c));
                }, A(4), A(5));
  assert(fz.wait_reference<0>().x == 4);
  assert(fz.wait_reference<1>().x == 5);
  assert(fz.wait_reference<2>().x == 6);
#endif

  print_test_success();
  upcxx::finalize();
  return 0;
}
