#include <upcxx/upcxx.hpp>
#include "../util.hpp"

struct A {
  int x = 3;
  UPCXX_SERIALIZED_FIELDS()
};

struct B {
  int y = 4;
  UPCXX_SERIALIZED_VALUES()
};

struct C {
  A as[7];
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const C &c) {
      w.write_sequence(c.as, c.as + 7, 7);
    }
    template<typename Reader, typename Storage>
    static C* deserialize(Reader &r, Storage storage) {
      C *c = storage.construct();
      r.template read_sequence_into<A>(c->as, 7);
      return c;
    }
  };
};

int main() {
  upcxx::init();
  print_test_header();

  A a;
  upcxx::rpc(0, [](const A &x) { return x; }, a).wait();

  B b;
  upcxx::rpc(0, [](const B &x) { return x; }, b).wait();

  std::vector<A> as{5};
  upcxx::rpc(0, [](const std::vector<A> &v) { return v; }, as).wait();

  std::array<B, 5> bs;
  upcxx::rpc(0, [](const std::array<B, 5> &v) { return v; }, bs).wait();

  C c;
  c = upcxx::serialization_traits<C>::deserialized_value(c);

  print_test_success();
  upcxx::finalize();
}
