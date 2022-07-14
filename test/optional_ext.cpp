#include "util.hpp"

struct foo {
  foo() : id(next_id++) {
    ++default_ctor_count;
  }
  foo(const foo &other) : id(next_id++) {
    ++copy_ctor_count;
  }
  foo& operator=(const foo &other) {
    ++copy_assign_count;
    return *this;
  }
  ~foo() {
    ++dtor_count;
  }
  int id;
  static int next_id;
  static int default_ctor_count;
  static int copy_ctor_count;
  static int copy_assign_count;
  static int dtor_count;
};

int foo::next_id = 5;
int foo::default_ctor_count = 0;
int foo::copy_ctor_count = 0;
int foo::copy_assign_count = 0;
int foo::dtor_count = 0;

#define CHECK_COUNT(name)                               \
  if (name >= 0) UPCXX_ASSERT_ALWAYS(name == foo::name)

inline void check_counts(int default_ctor_count, int copy_ctor_count,
                         int copy_assign_count, int dtor_count) {
  CHECK_COUNT(default_ctor_count);
  CHECK_COUNT(copy_ctor_count);
  CHECK_COUNT(copy_assign_count);
  CHECK_COUNT(dtor_count);
}

struct bar : foo {
  bar() {
    throw 3;
  }
};

int main() {
  upcxx::init();
  check_counts(0, 0, 0, 0);
  upcxx::optional<foo> s2;
  check_counts(0, 0, 0, 0);
  upcxx::optional<foo> *p3 = new upcxx::optional<foo>;
  check_counts(0, 0, 0, 0);
  {
    upcxx::optional<foo> s4;
    check_counts(0, 0, 0, 0);
    s2.emplace();
    check_counts(1, 0, 0, 0);
    p3->emplace();
    check_counts(2, 0, 0, 0);
    s4.emplace(*s2);
    check_counts(2, 1, 0, 0);
    s4 = s2;
    check_counts(2, 1, 1, 0);
  }
  check_counts(2, 1, 1, 1);
  delete p3;
  check_counts(2, 1, 1, 2);
  p3 = new upcxx::optional<foo>;
  check_counts(2, 1, 1, 2);
  new (*p3) foo;
  UPCXX_ASSERT_ALWAYS(*p3);  // check that *p3 was activated
  check_counts(3, 1, 1, 2);
  delete p3;
  check_counts(3, 1, 1, 3);
  upcxx::optional<bar> *p5 = new upcxx::optional<bar>;
  check_counts(3, 1, 1, 3);
  try {
    new (*p5) bar;  // throws int
    UPCXX_ASSERT_ALWAYS(false);
  } catch (...) {
  }
  check_counts(4, 1, 1, 4);
  // check that placement new deactivated *p5 after the exception was thrown
  UPCXX_ASSERT_ALWAYS(!*p5);
  delete p5;
  check_counts(4, 1, 1, 4);
  p3 = new upcxx::optional<foo>;
  check_counts(4, 1, 1, 4);
  delete p3;
  check_counts(4, 1, 1, 4);
  upcxx::finalize();
}
