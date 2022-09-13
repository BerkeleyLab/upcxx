#include <upcxx/upcxx.hpp>

struct trivial {
  int x;
  trivial() = delete; // not DefaultConstructible
};

struct A {
  upcxx::optional<trivial> t;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const A &o) {
      w.write(*o.t);
    }
    template<typename Reader, typename Storage>
    static A* deserialize(Reader &r, Storage storage) {
      A *a = storage.construct();
      r.template read_into<trivial>(a->t);
      return a;
    }
  };
};

int main() {
  upcxx::init();

  static A a;
  a = upcxx::serialization_traits<A>::deserialized_value(a);

  static trivial b = {3};
  b = upcxx::serialization_traits<trivial>::deserialized_value(b);

  upcxx::finalize();
}
