#include <upcxx/upcxx.hpp>

struct A {
  upcxx::optional<const int> t;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const A &o) {
      w.write(*o.t);
    }
    template<typename Reader, typename Storage>
    static A* deserialize(Reader &r, Storage storage) {
      A *a = storage.construct();
      r.template read_into<int>(a->t);
      return a;
    }
  };
};

int main() {
  upcxx::init();

  static A a;
  a = upcxx::serialization_traits<A>::deserialized_value(a);

  upcxx::finalize();
}
