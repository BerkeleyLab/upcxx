#include <iostream>
#include <string>
#include "../util.hpp"

struct foo {
  int data[2][3];
  void fill(int value) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 3; ++j) {
        data[i][j] = value;
      }
    }
  }
  bool operator==(const foo &other) const {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 3; ++j) {
        if (data[i][j] != other.data[i][j]) {
          return false;
        }
      }
    }
    return true;
  }
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const foo &x) {
      w.write(x.data);
    }
    template<typename Reader, typename Storage>
    static foo* deserialize(Reader &r, Storage storage) {
      auto result = storage.construct();
      r.template read_into<int[2][3]>(result->data);
      return result;
    }
  };
};

struct bar {
  std::string data[2][3];
  void fill(const std::string &value) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 3; ++j) {
        data[i][j] = value;
      }
    }
  }
  bool operator==(const bar &other) const {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 3; ++j) {
        if (data[i][j] != other.data[i][j]) {
          return false;
        }
      }
    }
    return true;
  }
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &w, const bar &x) {
      w.write(x.data);
    }
    template<typename Reader, typename Storage>
    static bar* deserialize(Reader &r, Storage storage) {
      auto result = storage.construct();
      r.template read_overwrite<std::string[2][3]>(result->data);
      return result;
    }
  };
};

int main() {
  upcxx::init();
  print_test_header();

  int target = (upcxx::rank_me()+1) % upcxx::rank_n();

  foo f;
  f.fill(100 + target);

  upcxx::rpc(
    target,
    [](foo x) {
      foo expected;
      expected.fill(100 + upcxx::rank_me());
      UPCXX_ASSERT_ALWAYS(x == expected);
    },
    f).wait();

  bar b;
  b.fill(std::to_string(100 + target));

  upcxx::rpc(
    target,
    [](bar x) {
      bar expected;
      expected.fill(std::to_string(100 + upcxx::rank_me()));
      UPCXX_ASSERT_ALWAYS(x == expected);
    },
    b).wait();

  // quiesce the world
  upcxx::barrier();

  print_test_success();
  upcxx::finalize();
}
