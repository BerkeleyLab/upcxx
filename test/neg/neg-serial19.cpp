#include <upcxx/upcxx.hpp>

struct A { // Serializable
  ~A() {} // nontrivial dtor
  UPCXX_SERIALIZED_VALUES()
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
      r.template read_into<A>(&result->a); // no cast to void*
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
