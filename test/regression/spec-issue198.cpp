#include <upcxx/upcxx.hpp>
#include "../util.hpp"

using namespace std;
using namespace upcxx;

// non-copyable, non-movable but Serializable type
struct nonmovable {
  int value;
  nonmovable(int v) : value(v) {}
  nonmovable(const nonmovable&) = delete;
  nonmovable& operator=(const nonmovable&) = delete;
  UPCXX_SERIALIZED_VALUES(value)
};

// test read_[sequence_]into/overwrite with non-movable type
struct container {
  nonmovable x;
  upcxx::optional<nonmovable> y;
  nonmovable z[1];
  container(int v) : x(v), y(upcxx::in_place, v+1), z{{v+2}} {}
  container(const container &rhs) : container(rhs.x.value) {}
  container& operator=(const container &rhs) {
    x.value = rhs.x.value;
    y.emplace(rhs.y->value);
    z->value = rhs.z->value;
    return *this;
  }
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const container &c) {
      w.write(c.x);
      w.write(*c.y);
      w.write_sequence(c.z, c.z + 1);
    }
    template<typename Reader, typename Storage>
    static container* deserialize(Reader &r, Storage storage) {
      auto result = storage.construct(-11);
      r.template read_overwrite<nonmovable>(result->x);
      r.template read_into<nonmovable>(result->y);
      r.template read_sequence_overwrite<nonmovable>(result->z, 1);
      return result;
    }
  };
  void check(int v) const {
    UPCXX_ASSERT_ALWAYS(x.value == v);
    UPCXX_ASSERT_ALWAYS(y->value == v + 1);
    UPCXX_ASSERT_ALWAYS(z->value == v + 2);
  }
};

// asymmetric non-movable type whose deserialized type is movable
struct asym_nonmovable {
  int value;
  asym_nonmovable(int v) : value(v) {}
  asym_nonmovable(const asym_nonmovable&) = delete;
  asym_nonmovable& operator=(const asym_nonmovable&) = delete;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const asym_nonmovable &a) {
      w.write(a.value);
    }
    template<typename Reader, typename Storage>
    static int* deserialize(Reader &r, Storage storage) {
      return storage.construct(r.template read<int>());
    }
  };
};

int main() {
    init();
    int right = (rank_me() + 1) % rank_n();

    // test read_[sequence_]into/overwrite with non-movable type
    container c1(right);
    auto fut = rpc(right,
                   [](const container &obj) {
                     obj.check(rank_me());
                     return obj;
                   },
                   c1);
    container c2 = fut.wait();
    UPCXX_ASSERT_ALWAYS(c2.x.value == right);
    UPCXX_ASSERT_ALWAYS(c2.y->value == right + 1);

    // test view over non-movable type
    nonmovable n1(right);
    rpc(right,
        [](const view<nonmovable> &v) {
          nonmovable r1(-11);
          upcxx::optional<nonmovable> r2;
          v.begin().deserialize_overwrite(r1);
          v.begin().deserialize_into(r2);
          UPCXX_ASSERT_ALWAYS(r1.value == rank_me());
          UPCXX_ASSERT_ALWAYS(r2->value == rank_me());
        },
        make_view(&n1, &n1 + 1)).wait();

    // test dist_object<T>::fetch() with non-movable T but movable
    // deserialized_type_t<T>
    dist_object<asym_nonmovable> dan1(world(), rank_me());
    auto fut2 = dan1.fetch(right);
    UPCXX_ASSERT_ALWAYS(fut2.wait() == right);

    print_test_success(true);
    finalize();
}
