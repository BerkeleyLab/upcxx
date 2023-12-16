#include <iostream>
#include <upcxx/upcxx.hpp>

#include "../util.hpp"

struct A { // movable but not copyable type
  int x;
  A(int x_) : x(x_) {}
  A(A&&) = default;
  A(const A &a) = delete;
public:
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const A &x) {
      w.write(x.x);
    }
    template<typename Reader, typename Storage>
    static A* deserialize(Reader &r, Storage storage) {
      return storage.construct(r.template read<int>());
    }
  };
};

bool done = false;

int main() {
  upcxx::init();
  print_test_header();

  A a{upcxx::rank_me()};
  upcxx::global_ptr<int> ptr = upcxx::allocate<int>();
  bool send = upcxx::rank_me() == 0;
  if (send) {
    auto fut =  upcxx::rpc((upcxx::rank_me()+1)%upcxx::rank_n(),
                           [ptr](const A &b) {
                             UPCXX_ASSERT_ALWAYS(
                               b.x == (upcxx::rank_me()+upcxx::rank_n()-1)%upcxx::rank_n()
                             );
                             return upcxx::make_future<const A&, upcxx::global_ptr<int>>(b, ptr);
                           },
                           a);
    UPCXX_ASSERT_ALWAYS(fut.wait_reference<0>().x == a.x);
    upcxx::global_ptr<int> dst = fut.wait<1>();
    upcxx::rput(a.x, dst,
                upcxx::remote_cx::as_rpc(
                  [=](const A &b) {
                    UPCXX_ASSERT_ALWAYS(*ptr.local() == b.x);
                    done = true;
                  },
                  a)
                | upcxx::operation_cx::as_future()).wait();
    while (!done) {
      upcxx::progress();
    }
  }
  upcxx::barrier();
  upcxx::deallocate(ptr);

  print_test_success();
  upcxx::finalize();
}
