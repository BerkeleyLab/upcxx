#include <iomanip>
#include <upcxx/upcxx.hpp>
#include "util.hpp"

// This test measures the number of copies/moves invoked on objects passed to
// various UPC++ routines. The results asserted by this test are only indicative
// of the current implementation and should NOT be construed as a guarantee of
// future copy/move behavior. 
// Consult the UPC++ Specification for guaranteed copy/move behaviors.

struct T {
  static void show_stats(int line, char const *title, int expected_ctors, int expected_copies,
                         int expected_moves=-1);
  static void reset_counts() { ctors = copies = moves = dtors = 0; }

  T() { ctors++; }
  T(T const &that) {
    UPCXX_ASSERT_ALWAYS(that.valid, "copying from an invalidated object");
    copies++;
  }
  T(T &&that) {
    UPCXX_ASSERT_ALWAYS(that.valid, "moving from an invalidated object");
    that.valid = false;
    moves++;
  }
  ~T() {
    valid = false;
    dtors++;
  }

  private:
  static int ctors, dtors, copies, moves;
  bool valid = true;

  public:
  UPCXX_SERIALIZED_FIELDS(valid)
};

int T::ctors = 0;
int T::dtors = 0;
int T::copies = 0;
int T::moves = 0;

bool success = true;

void T::show_stats(int line, const char *title, int expected_ctors, int expected_copies,
                   int expected_moves) {
  upcxx::barrier();
  
  #if !SKIP_OUTPUT
  if(upcxx::rank_me() == 0) {
    std::cout<<std::left<<std::setw(50)<<title<< " \t(line " << line << ")" << std::endl;
    std::cout<<"  T::ctors = "<<ctors<<std::endl;
    std::cout<<"  T::copies = "<<copies<<std::endl;
    std::cout<<"  T::moves = "<<moves<<std::endl;
    std::cout<<"  T::dtors = "<<dtors<<std::endl;
    std::cout<<std::endl;
  }
  #endif

  #define CHECK(prop, ...) do { \
    if (!(prop)) { \
      success = false; \
      if (!upcxx::rank_me()) \
        std::cerr << "ERROR: failed check: " << #prop << "\n" \
                  << title << ": " << __VA_ARGS__ \
                  << " \t(line " << line << ")" << "\n" << std::endl; \
    } \
  } while (0)

  CHECK(ctors == expected_ctors, "ctors="<<ctors<<" expected="<<expected_ctors);
  CHECK(copies == expected_copies, "copies="<<copies<<" expected="<<expected_copies);
  CHECK(expected_moves == -1 || moves == expected_moves,
                      "moves="<<moves<<" expected="<<expected_moves);
  CHECK(ctors+copies+moves == dtors, "ctors - dtors != 0");
  
  T::reset_counts();

  upcxx::barrier();
}
#define SHOW(...) T::show_stats(__LINE__, __VA_ARGS__)

T global;

bool done = false;

struct Fn { // movable and copyable function object
  T t;
  void operator()() { done = true; }
  UPCXX_SERIALIZED_FIELDS(t)
};

struct NmNcFn { // non-movable/non-copyable object that deserializes as Fn
  static NmNcFn global;
  T t;
  NmNcFn() {}
  NmNcFn(const NmNcFn&) = delete;
  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &writer, const NmNcFn &obj) {
      writer.write(obj.t);
    }
    template<typename Reader>
    static Fn* deserialize(Reader &reader, void *spot) {
      return new (spot) Fn{reader.template read<T>()};
    }
  };
};

NmNcFn NmNcFn::global;

int main() {
  upcxx::init();
  print_test_header();

  T::reset_counts(); // discount construction of global

  using upcxx::dist_object;
  dist_object<int> dob(3);

  int target = (upcxx::rank_me() + 1) % upcxx::rank_n();

  // About the expected num of copies.
  // Now that backend::send_awaken_lpc can take a std::tuple
  // containing a reference to T and serialize from that place, it no
  // longer involves an extra copy in the future<T> returning cases.
  
  upcxx::rpc(target,
    [](T &&x) {
    },
    T()
  ).wait_reference();
  SHOW("T&& ->", 2, 0, 0);

  upcxx::rpc(target,
    [](T const &x) {
    },
    global
  ).wait_reference();
  SHOW("T& ->", 1, 0, 0);

  upcxx::rpc(target,
    [](T const &x) {
    },
    static_cast<T const&>(global)
  ).wait_reference();
  SHOW("T const& ->", 1, 0, 0);

  upcxx::rpc(target,
    []() -> T {
      return T();
    }
  ).wait_reference();
  SHOW("-> T", 2, 0, 1);

  upcxx::rpc(target,
    [](T &&x) -> T {
      return std::move(x);
    },
    T()
  ).wait_reference();
  SHOW("T&& -> T", 3, 0, 2);

  upcxx::rpc(target,
    [](T const &x) -> T {
      return x;
    },
    static_cast<T const&>(global)
  ).wait_reference();
  SHOW("T const& -> T", 2, 1, 1);

  upcxx::rpc(target,
    [](T &&x) -> upcxx::future<T> {
      return upcxx::make_future(std::move(x));
    },
    T()
  ).wait_reference();
  SHOW("T&& -> future<T>", 3, 0, 4);

  upcxx::rpc(target,
    [](T const &x) -> upcxx::future<T> {
      return upcxx::make_future(x);
    },
    static_cast<T const&>(global)
  ).wait_reference();
  SHOW("T const& -> future<T>", 2, 1, 3);

  // now with dist_object

  {
    dist_object<T> dobT(upcxx::world());
    dobT.fetch(target).wait_reference();
    upcxx::barrier();
  }
  SHOW("dist_object<T>::fetch()", 2, 0, 1);

  upcxx::rpc(target,
    [](dist_object<int>&, T &&x) -> T {
      return std::move(x);
    },
    dob, T()
  ).wait_reference();
  SHOW("dist_object + T&& -> T", 3, 0, 4);

  upcxx::rpc(target,
    [](dist_object<int>&, T const &x) -> T {
      return x;
    },
    dob, static_cast<T const&>(global)
  ).wait_reference();
  SHOW("dist_object + T const& -> T", 2, 1, 3);

  upcxx::rpc(target,
    [](dist_object<int>&, T &&x) -> upcxx::future<T> {
      return upcxx::make_future(std::move(x));
    },
    dob, T()
  ).wait_reference();
  SHOW("dist_object + T&& -> future<T>", 3, 0, 4);

  upcxx::rpc(target,
    [](dist_object<int>&, T const &x) -> upcxx::future<T> {
      return upcxx::make_future(x);
    },
    dob, static_cast<T const&>(global)
  ).wait_reference();
  SHOW("dist_object + T const& -> future<T>", 2, 1, 3);

  // returning references

  upcxx::rpc(target,
    [](T &&x) -> T&& {
      return std::move(x);
    },
    T()
  ).wait_reference();
  SHOW("T&& -> T&&", 3, 0, 1);

  upcxx::rpc(target,
    [](T const &x) -> T& {
      return global;
    },
    global
  ).wait_reference();
  SHOW("T& -> T&", 2, 0, 1);

  upcxx::rpc(target,
    [](T const &x) -> T const& {
      return x;
    },
    static_cast<T const&>(global)
  ).wait_reference();
  SHOW("T const& -> T const&", 2, 0, 1);

  upcxx::rpc(target,
    [](T const &x) -> upcxx::future<T const&> {
      return upcxx::make_future<T const&>(x);
    },
    static_cast<T const&>(global)
  ).wait_reference();
  SHOW("T const& -> future<T const&>", 2, 0, 1);

  upcxx::rpc(target,
    [](upcxx::view<T> v) -> T const& {
      auto storage =
        new typename std::aligned_storage <sizeof(T),
                                           alignof(T)>::type;
      T *p = v.begin().deserialize_into(storage);
      delete p;
      return global;
    },
    upcxx::make_view(&global, &global+1)
  ).wait_reference();
  SHOW("view<T> -> T const&", 2, 0, 1);

  upcxx::rpc(target,
    []() -> T& {
      return global;
    }
  ).wait_reference();
  SHOW("-> T&", 1, 0, 1);

  upcxx::rpc(target,
    []() -> T const& {
      return global;
    }
  ).wait_reference();
  SHOW("-> T const&", 1, 0, 1);

  // function object

  {
    NmNcFn fn;
    upcxx::rpc(target, fn).wait_reference();
  }
  SHOW("NmNcFn& ->", 2, 0, 1);

  {
    NmNcFn fn;
    upcxx::rpc(target, [](Fn const &) {}, fn).wait_reference();
  }
  SHOW("(arg) NmNcFn& ->", 2, 0, 1);

  {
    NmNcFn fn;
    upcxx::rpc(target,
      [](Fn const &) -> NmNcFn& {
        return NmNcFn::global;
      }, fn).wait_reference();
  }
  SHOW("(arg) NmNcFn& -> NmNcFn&", 3, 0, 3);

  // rpc_ff

  upcxx::barrier();
  done = false;
  upcxx::barrier();

  upcxx::rpc_ff(target,
    [](T &&x) {
      done = true;
    },
    T()
  );
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) T&& ->", 2, 0, 0);

  upcxx::rpc_ff(target,
    [](T const &x) {
      done = true;
    },
    global
  );
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) T& ->", 1, 0, 0);

  upcxx::rpc_ff(target,
    [](T const &x) {
      done = true;
    },
    static_cast<T const&>(global)
  );
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) T const& ->", 1, 0, 0);

  {
    Fn fn;
    upcxx::rpc_ff(target, fn);
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) Fn& ->", 3, 0, 0);

  upcxx::rpc_ff(target, Fn());
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) Fn&& ->", 3, 0, 0);

  {
    NmNcFn fn;
    upcxx::rpc_ff(target, fn);
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) NmNcFn& ->", 2, 0, 1);

  {
    NmNcFn fn;
    upcxx::rpc_ff(target,
      [](Fn &&dfn) {
        dfn();
      }, fn);
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff arg) NmNcFn& ->", 2, 0, 1);

  // as_rpc

  { dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
    upcxx::global_ptr<int> gp = dobj.fetch(target).wait();

    using upcxx::remote_cx;
    using upcxx::operation_cx;

    {
      Fn fn;
      upcxx::rput(42, gp, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&)&& ->", 3, 0, 0);

    { Fn fn;
      auto cx = remote_cx::as_rpc(fn);
      upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&)& ->", 3, 0, 0);

    { Fn fn;
      auto const cx = remote_cx::as_rpc(fn);
      upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&) const & ->", 3, 0, 0);

    upcxx::rput(42, gp, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&&)&& ->", 3, 0, 2);

    { auto cx = remote_cx::as_rpc(Fn());
      upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&&)& ->", 3, 0, 2);

    { auto const cx = remote_cx::as_rpc(Fn());
      upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(Fn&&) const & ->", 3, 0, 2);

    {
      NmNcFn fn;
      upcxx::rput(42, gp, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(NmNcFn&)&& ->", 2, 0, 1);

    {
      NmNcFn fn;
      upcxx::rput(42, gp, remote_cx::as_rpc(
                            [](Fn &&dfn) {
                              dfn();
                            }, fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(lambda, NmNcFn&)&& ->", 2, 0, 1);

    {
      T t;
      upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ done=true; }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T& -> const T&", 2, 0, 0);

    {
      upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ done=true; }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T&& -> const T&", 2, 0, 2);

    {
      upcxx::rput(42, gp, remote_cx::as_rpc([](T&& t){ done=true; return std::move(t); }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T&& -> T&&", 2, 0, 3);

    {
      T t;
      (void)upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ done=true; }, t) | operation_cx::as_future());
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|... T& -> const T&", 2, 0, 0);

    {
      (void)upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ done=true; }, T()) | operation_cx::as_future());
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|... T&& -> const T&", 2, 0, 3);

    {
      T t;
      auto cx = remote_cx::as_rpc([](const T&){ done=true; }, t) | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|...& T& -> const T&", 2, 0, 0);

    {
      auto cx = remote_cx::as_rpc([](const T&){ done=true; }, T()) | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|...& T&& -> const T&", 2, 0, 3);

    {
      auto cx = remote_cx::as_rpc([](const T&){ done=true; }, T());
      auto cx2 = cx | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx2);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()&|...& T&& -> const T&", 2, 1, 2);

    {
      T t;
      (void)upcxx::rput(42, gp, operation_cx::as_future() | remote_cx::as_rpc([](const T&){ done=true; }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc() T& -> const T&", 2, 0, 0);

    {
      (void)upcxx::rput(42, gp, operation_cx::as_future() | remote_cx::as_rpc([](const T&){ done=true; }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc() T&& -> const T&", 2, 0, 3);

    {
      T t;
      auto cx = operation_cx::as_future() | remote_cx::as_rpc([](const T&){ done=true; }, t);
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc()& T& -> const T&", 2, 0, 0);

    {
      auto cx = operation_cx::as_future() | remote_cx::as_rpc([](const T&){ done=true; }, T());
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc()& T&& -> const T&", 2, 0, 3);

    {
      auto cx = operation_cx::as_future();
      auto cx2 = cx | remote_cx::as_rpc([](const T&){ done=true; }, T());
      (void)upcxx::rput(42, gp, cx2);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...&|as_rpc()& T&& -> const T&", 2, 0, 3);
  }
 
  print_test_success(success);
  upcxx::finalize();
}
