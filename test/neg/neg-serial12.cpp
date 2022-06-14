#include <upcxx/upcxx.hpp>

struct A { // non-Serializable
  ~A() {}
};

struct B {
  A a;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const B &x) {
    }
    template<typename Reader>
    static B* deserialize(Reader &r, void *spot) {
      B* result = new(spot) B;
      r.template read_overwrite<A>(result->a);
      return result;
    }
  };
};

int main() {
  upcxx::init();

  B b;
  upcxx::rpc(0, [](B b) {}, b).wait();

  upcxx::finalize();
}
