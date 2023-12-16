#include <string>
#include <tuple>
#include <type_traits>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

template<typename T, bool TS, bool S, typename DT = void>
struct check {
  static_assert(upcxx::is_trivially_serializable<T>::value == TS, "ERROR");
  static_assert(upcxx::is_serializable<T>::value == S, "ERROR");
  static_assert(std::is_same<upcxx::deserialized_type_t<T>, DT>::value, "ERROR");
  check(){} // this helps avoid unused-value warnings on use (issue #468)
};
template<typename T>
struct check<T, false, false, void> {
  static_assert(!upcxx::is_trivially_serializable<T>::value, "ERROR");
  static_assert(!upcxx::is_serializable<T>::value, "ERROR");
  check(){} // this helps avoid unused-value warnings on use (issue #468)
};

struct D {
  int w,x;
  ~D() {} // break trivialness
};
struct S { // asymmetric serialization
  int x,y,z;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize (Writer& writer, S const & t) {
        writer.write(t.x);
    }
    template<typename Reader, typename Storage>
    static D* deserialize(Reader& reader, Storage storage) {
        int x = reader.template read<int>();
        D *up = storage.construct();
        up->x = x;
        return up;
    }
  };
};
struct U { // no serialization
  ~U() {} // break trivialness
};

namespace upcxx {
  template<>
  struct is_trivially_serializable<::D> : ::std::true_type {};
}

int z = 5;
std::string s = "test";
S t{-1, -2, -3};
D d{-4, -5};

int main() {
  upcxx::init();

  print_test_header();

  int x = 3;
  double y = 5.3;

  check<const int, true, true, const int>{};
  check<const int&, false, true, int>{};
  check<int&&, false, true, int>{};
  check<std::reference_wrapper<int>, false, true, int>{};
  check<std::reference_wrapper<const int>, false, true, int>{};

  check<const std::string, false, true, const std::string>{};
  check<const std::string&, false, true, std::string>{};
  check<std::string&&, false, true, std::string>{};
  check<std::reference_wrapper<std::string>, false, true, std::string>{};
  check<std::reference_wrapper<const std::string>, false, true, std::string>{};

  check<volatile std::string, false, false>{};
  check<const volatile std::string, false, false>{};
  check<volatile std::string&, false, false>{};
  check<const volatile std::string&, false, false>{};
  check<std::reference_wrapper<volatile std::string>, false, false>{};
  check<std::reference_wrapper<const volatile std::string>, false, false>{};

  check<S, false, true, D>{};
  check<const S, false, true, const D>{};
  check<const S&, false, true, D>{};
  check<S&&, false, true, D>{};
  check<std::reference_wrapper<S>, false, true, D>{};
  check<std::reference_wrapper<const S>, false, true, D>{};

  check<D, true, true, D>{};
  check<const D, true, true, const D>{};
  check<const D&, false, true, D>{};
  check<D&&, false, true, D>{};
  check<std::reference_wrapper<D>, false, true, D>{};
  check<std::reference_wrapper<const D>, false, true, D>{};

  check<U, false, false>{};
  check<const U, false, false>{};
  check<const U&, false, false>{};
  check<U&&, false, false>{};
  check<std::reference_wrapper<U>, false, false>{};
  check<std::reference_wrapper<const U>, false, false>{};

  check<std::pair<const int, const D>, true, true, std::pair<const int, const D>>{};
  check<std::tuple<const int, const S&, const D&>, false, true, std::tuple<const int, D, D>>{};
  check<std::tuple<const int&, const S, const D>, false, true, std::tuple<int, const D, const D>>{};
  check<std::tuple<const int, std::reference_wrapper<const S>, const D&>, false, true, std::tuple<const int, D, D>>{};
  check<std::tuple<std::reference_wrapper<const int>, const S, const D>, false, true, std::tuple<int, const D, const D>>{};
  check<std::pair<U, int>, false, false>{};
  check<std::tuple<U, int>, false, false>{};

  check<upcxx::global_ptr<const int>, true, true, upcxx::global_ptr<const int>>{};
  check<upcxx::global_ptr<const int>&, false, true, upcxx::global_ptr<const int>>{};

  check<int[4], false, false>{};
  check<const int[4], false, false>{};
  check<int[4][3], false, false>{};
  check<const int[4][3], false, false>{};
  check<int(&)[4], false, false>{};
  check<const int(&)[4], false, false>{};
  check<std::reference_wrapper<int[4]>, false, false>{};
  check<std::reference_wrapper<const int[4]>, false, false>{};
  check<std::pair<int[4], int>, false ,false>{};
  check<std::pair<int(&)[4], int>, false ,false>{};
  check<std::pair<std::reference_wrapper<int[4]>, int>, false ,false>{};
  check<std::tuple<int[4], int>, false ,false>{};
  check<std::tuple<int(&)[4], int>, false ,false>{};
  check<std::tuple<std::reference_wrapper<int[4]>, int>, false ,false>{};

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=](std::pair<int, double> q) -> std::string&& {
        UPCXX_ASSERT_ALWAYS(q.first == x && q.second == -y);
        return std::move(s);
      },
      std::pair<const int&, double&&>(x, -y));
    assert_same<decltype(res), upcxx::future<std::string>>{};
    UPCXX_ASSERT_ALWAYS(res.wait() == "test");
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=]() -> const int& {
        return z;
      });
    assert_same<decltype(res), upcxx::future<int>>{};
    UPCXX_ASSERT_ALWAYS(res.wait() == z);
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=]() -> int& {
        return z;
      });
    assert_same<decltype(res), upcxx::future<int>>{};
    UPCXX_ASSERT_ALWAYS(res.wait() == z);
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=](std::pair<const D, const D> p) -> const S& {
        UPCXX_ASSERT_ALWAYS(p.first.x == 1);
        UPCXX_ASSERT_ALWAYS(p.second.w == 4);
        UPCXX_ASSERT_ALWAYS(p.second.x == 5);
        return t;
      },
      std::pair<const S, const D>{S{1, 2, 3}, D{4, 5}});
    assert_same<decltype(res), upcxx::future<D>>{};
    UPCXX_ASSERT_ALWAYS(res.wait().x == -1);
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=](std::pair<D, D> p) -> D&& {
        UPCXX_ASSERT_ALWAYS(p.first.x == 1);
        UPCXX_ASSERT_ALWAYS(p.second.w == 4);
        UPCXX_ASSERT_ALWAYS(p.second.x == 5);
        return std::move(d);
      },
      std::pair<const S&, D&&>{S{1, 2, 3}, D{4, 5}});
    assert_same<decltype(res), upcxx::future<D>>{};
    UPCXX_ASSERT_ALWAYS(res.wait().w == -4);
    UPCXX_ASSERT_ALWAYS(res.wait().x == -5);
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=]() -> std::reference_wrapper<const int> {
        return std::cref(z);
      });
    assert_same<decltype(res), upcxx::future<int>>{};
    UPCXX_ASSERT_ALWAYS(res.wait() == z);
  }

  {
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=]() -> std::reference_wrapper<int> {
        return std::ref(z);
      });
    assert_same<decltype(res), upcxx::future<int>>{};
    UPCXX_ASSERT_ALWAYS(res.wait() == z);
  }

  {
    S s{1, 2, 3};
    D d{4, 5};
    auto res = upcxx::rpc((upcxx::rank_me() + 1) % upcxx::rank_n(),
      [=](std::pair<D, D> p) -> std::reference_wrapper<const S> {
        UPCXX_ASSERT_ALWAYS(p.first.x == 1);
        UPCXX_ASSERT_ALWAYS(p.second.w == 4);
        UPCXX_ASSERT_ALWAYS(p.second.x == 5);
        return t;
      },
      std::pair<std::reference_wrapper<const S>,
                std::reference_wrapper<const D>>{
        std::cref(s), std::cref(d)
      });
    assert_same<decltype(res), upcxx::future<D>>{};
    UPCXX_ASSERT_ALWAYS(res.wait().x == -1);
  }

  print_test_success();

  upcxx::finalize();
}
