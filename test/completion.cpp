#include <upcxx/upcxx.hpp>
#include "util.hpp"

using namespace upcxx;

struct T {
  T() {}
  T(int v) : value(v) {}
  ~T() {
    valid = false;
  }
  int value;
  bool valid = true;
  UPCXX_SERIALIZED_FIELDS(value, valid)
};

bool done = false;

struct Fn {
  int expected;
  bool value;
  void operator()(const T &t) {
    UPCXX_ASSERT_ALWAYS(t.valid && t.value == expected);
    done = value;
  }
};

struct LocalFn {
  T *t;
  int expected;
  bool value;
  void operator()() {
    UPCXX_ASSERT_ALWAYS(t->valid && t->value == expected);
    done = value;
  }
};

int main() {
  upcxx::init();
  print_test_header();

  int target = (upcxx::rank_me() + 1) % upcxx::rank_n();
  dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
  upcxx::global_ptr<int> gp = dobj.fetch(target).wait();

  {
    promise<> p1;
    promise<> p2;
    auto cxs = operation_cx::as_future() | operation_cx::as_promise(p1);
    {
      auto cxs2 = operation_cx::as_promise(p2);
      cxs = operation_cx::as_future() | cxs2; // overwrite permitted because completion objects are CopyAssignable
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    f.wait();
    p2.finalize().wait();
    assert(!p1.get_future().is_ready());
  }

  {
    promise<> p1;
    promise<> p2;
    auto cxs = operation_cx::as_promise(p1) | operation_cx::as_future();
    {
      auto cxs2 = operation_cx::as_promise(p2);
      cxs = cxs2 | operation_cx::as_future(); // this better not std::move(cxs2.head())
      cxs = cxs2 | operation_cx::as_future(); // because we can call it again..
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    f.wait();
    p2.finalize().wait();
    assert(!p1.get_future().is_ready());
  }

  {
    auto cxs = operation_cx::as_future()
      | remote_cx::as_rpc(Fn{0, false}, T(5));
    {
      auto cxs2 = remote_cx::as_rpc(Fn{-3, true}, T(-3));
      cxs = operation_cx::as_future() | cxs2; // overwrite permitted because completion objects are CopyAssignable
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    while (!done) { progress(); }
    f.wait();
    done = false;
    barrier();
  }

  {
    auto cxs = remote_cx::as_rpc(Fn{0, false}, T(5))
      | operation_cx::as_future();
    {
      auto cxs2 = remote_cx::as_rpc(Fn{-3, true}, T(-3));
      cxs = cxs2 | operation_cx::as_future(); // this better not std::move(cxs2.head())
      cxs = cxs2 | operation_cx::as_future(); // because we can call it again..
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    while (!done) { progress(); }
    f.wait();
    done = false;
    barrier();
  }

  {
    T *t1 = new T{5};
    auto cxs = operation_cx::as_future()
      | operation_cx::as_lpc(current_persona(), LocalFn{t1, 0, false});
    delete t1;
    T *t2 = nullptr;
    {
      t2 = new T{-3};
      auto cxs2 = operation_cx::as_lpc(current_persona(),
                                       LocalFn{t2, -3, true});
      cxs = operation_cx::as_future() | cxs2; // overwrite permitted because completion objects are CopyAssignable
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    while (!done) { progress(); }
    f.wait();
    delete t2;
    done = false;
    barrier();
  }


  {
    T *t1 = new T{5};
    auto cxs = operation_cx::as_lpc(current_persona(),
                                    LocalFn{t1, 0, false})
      | operation_cx::as_future();
    delete t1;
    T *t2 = nullptr;
    {
      t2 = new T{-3};
      auto cxs2 = operation_cx::as_lpc(current_persona(),
                                       LocalFn{t2, -3, true});
      cxs = cxs2 | operation_cx::as_future(); // this better not std::move(cxs2.head())
      cxs = cxs2 | operation_cx::as_future(); // because we can call it again..
    } // cxs2 dies here
    auto f = rput(42, gp, cxs);
    while (!done) { progress(); }
    f.wait();
    delete t2;
    done = false;
    barrier();
  }

  print_test_success();
  upcxx::finalize();
}
