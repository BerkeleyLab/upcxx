#include "../util.hpp"
#include <type_traits>
#include <vector>
#include <iostream>

#if SYMMETRIC
#define U T // force symmetric deserialization
#else
struct U { // asymmetric deserialization
  int w,x;
};
#endif

struct T {
  int x,y,z;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize (Writer& writer, T const & t) {
        writer.write(t.x);
    }
    template<typename Reader, typename Storage>
    static U* deserialize(Reader& reader, Storage storage) {
        int x = reader.template read<int>();
        U *up = storage.construct();
        up->x = x;
        return up;
    }
  };
};

int main() {
  upcxx::init();
  print_test_header();

  T t;
  t.x = 42;
  upcxx::rpc(0, [](const U &u) { 
                  assert(u.x == 42); 
                }, t).wait();

  static_assert(std::is_same<U, upcxx::deserialized_type_t<T>>::value, "oops");
  static_assert(std::is_same<upcxx::view<T>, 
                upcxx::deserialized_type_t<upcxx::view<T,std::vector<T>::iterator>>>::value, "oops");

  std::vector<T> vt;
  for (int i=0; i < 10; i++) vt.push_back(T{42,43,44});
  for (int i=0; i < 10; i++) assert(vt[i].x == 42);
  upcxx::rpc(0, [](upcxx::view<T> myview) { 
                  static_assert(std::is_same<upcxx::deserializing_iterator<T>,
                                             decltype(myview)::iterator>::value, "oops");
                  int cnt = 0;
                  auto it = myview.begin();
                  static_assert(std::is_same<U,  decltype(it)::value_type>::value, "oops");
                  static_assert(std::is_same<U*, decltype(it)::pointer>::value, "oops");
                  static_assert(std::is_same<U,  decltype(it)::reference>::value, "oops");
                  for (; it < myview.end(); it++, cnt++) {
                    U u = *it;
                    assert(u.x == 42); 
                  }
                  assert(cnt == 10);
                }, upcxx::make_view(vt)).wait();

  print_test_success();
  upcxx::finalize();
}
