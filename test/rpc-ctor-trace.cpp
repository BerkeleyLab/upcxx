#include <iomanip>
#include <upcxx/upcxx.hpp>
#include "util.hpp"

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

// This test measures the number of copies/moves invoked on objects passed to
// various UPC++ routines. The results asserted by this test are only indicative
// of the current implementation and should NOT be construed as a guarantee of
// copy/move behavior for past or subsequent revisions of the implementation. 
// Consult the UPC++ Specification for guaranteed copy/move behaviors.

using std::uint64_t;

struct T {
  static void show_stats(int line, char const *title, 
                         int expected_ctors, int expected_copies, int expected_moves) UTIL_ATTRIB_NOINLINE;
  static void reset_counts() { ctors = copies = moves = dtors = 0; }

  private:
  static constexpr uint64_t VALID   = 0x5555555555555555llu;
  static constexpr uint64_t INVALID = 0xAAAAAAAAAAAAAAAAllu;
  uint64_t valid = VALID;

  public:
  void check_corruption(const char *context) const {
    UPCXX_ASSERT_ALWAYS(valid == VALID || valid == INVALID,
                        context << " a corrupted object: " << std::hex << valid);
  }
  void check_op(const char *context) const {
    check_corruption(context);
    UPCXX_ASSERT_ALWAYS(valid == VALID,
                        context << " an invalidated object: " << std::hex << valid);
  }

  T() { 
    check_op("default constructing");
    ctors++; 
  }
  T(T const &that) {
    check_op("copying");
    that.check_op("copying from");
    copies++;
  }
  T(T &&that) {
    check_op("move constructing");
    that.check_op("moving from");
    that.valid = INVALID;
    moves++;
  }
  ~T() {
    check_corruption("destroying");
    valid = INVALID;
    dtors++;
  }

  private:
  uint64_t serialize() const {
    check_op("serializing");
    return valid;
  }
  T(uint64_t v) : valid(v) { 
    check_op("deserializing");
    ctors++; 
  }

  static int ctors, dtors, copies, moves;

  public:
  UPCXX_SERIALIZED_VALUES( serialize() )
};

int T::ctors = 0;
int T::dtors = 0;
int T::copies = 0;
int T::moves = 0;

bool success = true;

// expected_{ctors,copies,moves}:
//  positive values request an exact match on # of respective default construct, copy, move of T
//  negative values enforce an upper bound on the given metric
void T::show_stats(int line, const char *title, 
                   int expected_ctors, int expected_copies, int expected_moves) {
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
      say() << "ERROR: failed check: " << #prop << "\n" \
            << title << ": " << __VA_ARGS__ \
            << " \t(line " << line << ")" << "\n"; \
    } \
  } while (0)

  if (expected_ctors < 0) 
    CHECK(ctors <= -expected_ctors, "ctors="<<ctors<<" expected<="<<-expected_ctors);
  else                    
    CHECK(ctors == expected_ctors, "ctors="<<ctors<<" expected="<<expected_ctors);

  if (expected_copies < 0) 
    CHECK(copies <= -expected_copies, "copies="<<copies<<" expected<="<<-expected_copies);
  else 
    CHECK(copies == expected_copies, "copies="<<copies<<" expected="<<expected_copies);

  if (expected_moves < 0)
    CHECK(moves <= -expected_moves, "moves="<<moves<<" expected<="<<-expected_moves);
  else
    CHECK(moves == expected_moves, "moves="<<moves<<" expected="<<expected_moves);

  CHECK(ctors+copies+moves == dtors, "ctors - dtors != 0");
  
  T::reset_counts();

  upcxx::barrier();
}
#define SHOW(...) T::show_stats(__LINE__, __VA_ARGS__)

T global;

bool done = false;
#define set_done() do { \
  UPCXX_ASSERT_ALWAYS(done == false, "Duplicate call to set_done()"); \
  done = true; \
} while(0)

struct Fn { // movable and copyable function object
  T t;
  void operator()() { set_done(); }
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
    template<typename Reader, typename Storage>
    static Fn* deserialize(Reader &reader, Storage storage) {
      return storage.construct(reader.template read<T>());
    }
  };
};

NmNcFn NmNcFn::global;

using upcxx::dist_object;
using upcxx::remote_cx;
using upcxx::operation_cx;

int target;
dist_object<int> *ddob;

void UTIL_ATTRIB_NOINLINE test_rpc1() {
  // About the expected num of copies.
  // Now that backend::send_awaken_lpc can take a std::tuple
  // containing a reference to T and serialize from that place, it no
  // longer involves an extra copy in the future<T> returning cases.

#if !SKIP_RPC1 && !SKIP_RPC
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
#endif
}
void UTIL_ATTRIB_NOINLINE test_rpc2() {
  // now with dist_object
  dist_object<int> const &dob = *ddob;
#if !SKIP_RPC2 && !SKIP_RPC
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
#endif
}
void UTIL_ATTRIB_NOINLINE test_rpc3() {
  // returning references
#if !SKIP_RPC3 && !SKIP_RPC
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
#endif
}
void UTIL_ATTRIB_NOINLINE test_rpc4() {
  // function object
#if !SKIP_RPC4 && !SKIP_RPC
  {
    NmNcFn fn;
    upcxx::rpc(target, fn).wait_reference();
  }
  SHOW("NmNcFn& ->", 2, 0, 2);

  {
    NmNcFn fn;
    upcxx::rpc(target, [](Fn const &) {}, fn).wait_reference();
  }
  SHOW("(arg) NmNcFn& ->", 2, 0, 2);

  {
    NmNcFn fn;
    upcxx::rpc(target,
      [](Fn const &) -> NmNcFn& {
        return NmNcFn::global;
      }, fn).wait_reference();
  }
  SHOW("(arg) NmNcFn& -> NmNcFn&", 3, 0, 5);
#endif
}
void UTIL_ATTRIB_NOINLINE test_rpc5() {
  // rpc_ff
#if !SKIP_RPC5 && !SKIP_RPC
  upcxx::barrier();
  done = false;
  upcxx::barrier();

  upcxx::rpc_ff(target,
    [](T &&x) { 
      set_done(); 
    }, T()
  );
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) T&& ->", 2, 0, 0);

  upcxx::rpc_ff(target,
    [](T const &x) { 
      set_done(); 
    }, global
  );
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff) T& ->", 1, 0, 0);

  upcxx::rpc_ff(target,
    [](T const &x) { 
      set_done(); 
    }, static_cast<T const&>(global)
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
  SHOW("(rpc_ff) NmNcFn& ->", 2, 0, 2);

  {
    NmNcFn fn;
    upcxx::rpc_ff(target,
      [](Fn &&dfn) {
        dfn();
      }, fn);
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("(rpc_ff arg) NmNcFn& ->", 2, 0, 2);
#endif
}
upcxx::global_ptr<int> gp;
upcxx::global_ptr<int> gp_local;
int x = 0;
int *lp = &x;
void UTIL_ATTRIB_NOINLINE test_rput_rpc1() {
  // as_rpc

  { dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
    gp = dobj.fetch(target).wait();
    gp_local = *dobj;
    upcxx::barrier();
  }
#if !SKIP_RPUT1 && !SKIP_RPUT
    // rput: as_rpc
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
    SHOW("as_rpc(NmNcFn&)&& ->", 2, 0, 2);

    {
      NmNcFn fn;
      upcxx::rput(42, gp, remote_cx::as_rpc(
                            [](Fn &&dfn) {
                              dfn();
                            }, fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc(lambda, NmNcFn&)&& ->", 2, 0, 2);
#endif
}
void UTIL_ATTRIB_NOINLINE test_rput_rpc2() {
#if !SKIP_RPUT2 && !SKIP_RPUT
    {
      T t;
      upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T& -> const T&", 2, 0, 0);

    {
      upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T&& -> const T&", 2, 0, 2);

    {
      upcxx::rput(42, gp, remote_cx::as_rpc([](T&& t){ set_done(); return std::move(t); }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc() T&& -> T&&", 2, 0, 3);

    {
      T t;
      (void)upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ set_done(); }, t) | operation_cx::as_future());
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|... T& -> const T&", 2, 0, 0);

    {
      (void)upcxx::rput(42, gp, remote_cx::as_rpc([](const T&){ set_done(); }, T()) | operation_cx::as_future());
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|... T&& -> const T&", 2, 0, 3);
#endif
}
void UTIL_ATTRIB_NOINLINE test_rput_rpc3() {
#if !SKIP_RPUT3 && !SKIP_RPUT
    {
      T t;
      auto cx = remote_cx::as_rpc([](const T&){ set_done(); }, t) | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|...& T& -> const T&", 2, 0, 0);

    {
      auto cx = remote_cx::as_rpc([](const T&){ set_done(); }, T()) | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()|...& T&& -> const T&", 2, 0, 3);

    {
      auto cx = remote_cx::as_rpc([](const T&){ set_done(); }, T());
      auto cx2 = cx | operation_cx::as_future();
      (void)upcxx::rput(42, gp, cx2);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("as_rpc()&|...& T&& -> const T&", 2, 1, 2);

    {
      T t;
      (void)upcxx::rput(42, gp, operation_cx::as_future() | remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc() T& -> const T&", 2, 0, 0);

    {
      (void)upcxx::rput(42, gp, operation_cx::as_future() | remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc() T&& -> const T&", 2, 0, 3);

    {
      T t;
      auto cx = operation_cx::as_future() | remote_cx::as_rpc([](const T&){ set_done(); }, t);
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc()& T& -> const T&", 2, 0, 0);

    {
      auto cx = operation_cx::as_future() | remote_cx::as_rpc([](const T&){ set_done(); }, T());
      (void)upcxx::rput(42, gp, cx);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...|as_rpc()& T&& -> const T&", 2, 0, 3);

    {
      auto cx = operation_cx::as_future();
      auto cx2 = cx | remote_cx::as_rpc([](const T&){ set_done(); }, T());
      (void)upcxx::rput(42, gp, cx2);
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("...&|as_rpc()& T&& -> const T&", 2, 0, 3);
#endif
}
void UTIL_ATTRIB_NOINLINE test_vis_rpc() {
    // VIS rput: as_rpc
#if !SKIP_VIS
    std::size_t sz = 1;
    std::pair<int *,size_t> lpp(lp,sz);
    std::pair<upcxx::global_ptr<int>,size_t> gpp(gp,sz);
    std::array<std::ptrdiff_t,1> a_stride = {{4}};
    std::array<std::size_t,1> a_ext = {{1}};

    {
      T t;
      upcxx::rput_irregular(&lpp,&lpp+1,&gpp,&gpp+1, 
                            remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_irregular: as_rpc() T& -> const T&", 2, 0, 0);

    upcxx::rput_irregular(&lpp,&lpp+1,&gpp,&gpp+1,  
                          remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_irregular: as_rpc() T&& -> const T&", 2, 0, 3);

    {
      T t;
      upcxx::rput_regular(&lp,&lp+1,sz,&gp,&gp+1,sz,
                            remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_regular: as_rpc() T& -> const T&", 2, 0, 0);

    upcxx::rput_regular(&lp,&lp+1,sz,&gp,&gp+1,sz,
                          remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_regular: as_rpc() T&& -> const T&", 2, 0, 3);

    {
      T t;
      upcxx::rput_strided(lp, a_stride, gp, a_stride, a_ext,
                            remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_strided: as_rpc() T& -> const T&", 2, 0, 0);

    upcxx::rput_strided(lp, a_stride, gp, a_stride, a_ext,
                          remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("rput_strided: as_rpc() T&& -> const T&", 2, 0, 3);
#endif
}
void UTIL_ATTRIB_NOINLINE test_copy_rpc() {
    // copy: as_rpc
#if !SKIP_COPY_HOST && !SKIP_COPY
    {
      Fn fn;
      upcxx::copy(lp, gp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put: as_rpc(Fn&)&& ->", 3, 0, 0);

    upcxx::copy(lp, gp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put: as_rpc(Fn&&)&& ->", 3, 0, 2);

    {
      T t;
      upcxx::copy(lp, gp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put: as_rpc() T& -> const T&", 2, 0, 0);

    upcxx::copy(lp, gp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put: as_rpc() T&& -> const T&", 2, 0, 2);

    {
      upcxx::future<> f;
      { T t;
        f = upcxx::copy(lp, gp, 1, operation_cx::as_future() | remote_cx::as_rpc([](const T&){ set_done(); }, t));
      }
      f.wait();
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put: as_rpc()|as_future() T& -> const T&", 2, 0, 0);

    {
      Fn fn;
      upcxx::copy(gp, lp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gp, lp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      T t;
      upcxx::copy(gp, lp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get: as_rpc() T& -> const T&", 2, 0, -2);

    upcxx::copy(gp, lp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get: as_rpc() T&& -> const T&", 2, 0, -4);

    {
      Fn fn;
      upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loopback: as_rpc(Fn&)&& ->", 3, 0, 2);

    upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loopback: as_rpc(Fn&&)&& ->", 3, 0, 4);

    {
      T t;
      upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, t));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loopback: as_rpc() T& -> const T&", 2, 0, 2);

    upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc([](const T&){ set_done(); }, T()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loopback: as_rpc() T&& -> const T&", 2, 0, 4);
#endif
}
void UTIL_ATTRIB_NOINLINE test_copy_rpc_cuda() {

  #if defined(DEVICE) && !SKIP_COPY_DEVICE && !SKIP_COPY
   #if SPREAD_DEVICE
    // Note: since 2022.3.0 this can be accomplished via upcxx::make_gpu_allocator()
    Device dev(upcxx::local_team().rank_me()%Device::device_n());
   #else
    Device dev(0);
   #endif
    upcxx::device_allocator<Device> dev_alloc(dev, 1024*1024);
    using gpdev_t = upcxx::global_ptr<int, upcxx::memory_kind::any>;
    gpdev_t gpdev_local = dev_alloc.allocate<int>(2);
    dist_object<gpdev_t> devdobj(gpdev_local+1);
    gpdev_t gpdev = devdobj.fetch(target).wait();

    {
      Fn fn;
      upcxx::copy(gpdev_local, lp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-d2h: as_rpc(Fn&)&& ->", 3, 0, 1);

    upcxx::copy(gpdev_local, lp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-d2h: as_rpc(Fn&&)&& ->", 3, 0, 3);

    {
      Fn fn;
      upcxx::copy(lp, gpdev_local, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-h2d: as_rpc(Fn&)&& ->", 3, 0, 1);

    upcxx::copy(lp, gpdev_local, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-h2d: as_rpc(Fn&&)&& ->", 3, 0, 3);

    {
      Fn fn;
      upcxx::copy(gpdev_local, gpdev_local+1, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-d2d: as_rpc(Fn&)&& ->", 3, 0, 1);

    upcxx::copy(gpdev_local, gpdev_local+1, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-loop-d2d: as_rpc(Fn&&)&& ->", 3, 0, 3);

    // the non-determinism in the move counts below arises from device copy
    // asynchrony in reference kinds and is described in issue 494
    {
      Fn fn;
      upcxx::copy(lp, gpdev, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-h2d: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(lp, gpdev, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-h2d: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      Fn fn;
      upcxx::copy(gpdev_local, gp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-d2h: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gpdev_local, gp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-d2h: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      Fn fn;
      upcxx::copy(gpdev_local, gpdev, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-d2d: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gpdev_local, gpdev, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-put-d2d: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      Fn fn;
      upcxx::copy(gpdev, lp, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-d2h: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gpdev, lp, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-d2h: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      Fn fn;
      upcxx::copy(gp, gpdev_local, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-h2d: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gp, gpdev_local, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-h2d: as_rpc(Fn&&)&& ->", 3, 0, -4);

    {
      Fn fn;
      upcxx::copy(gpdev, gpdev_local, 1, remote_cx::as_rpc(fn));
    }
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-d2d: as_rpc(Fn&)&& ->", 3, 0, -2);

    upcxx::copy(gpdev, gpdev_local, 1, remote_cx::as_rpc(Fn()));
    while (!done) { upcxx::progress(); }
    done = false;
    SHOW("copy-get-d2d: as_rpc(Fn&&)&& ->", 3, 0, -4);


    dev.destroy();
  #endif // DEVICE
}

int main() {
  upcxx::init();
  print_test_header();

  T::reset_counts(); // discount construction of global

  target = (upcxx::rank_me() + 1) % upcxx::rank_n();

  ddob = new dist_object<int>(3);

  test_rpc1();
  test_rpc2();
  test_rpc3();
  test_rpc4();
  test_rpc5();
  test_rput_rpc1();
  test_rput_rpc2();
  test_rput_rpc3();
  test_vis_rpc();
  test_copy_rpc();
  test_copy_rpc_cuda();

  upcxx::barrier();
  upcxx::delete_(gp_local);
  delete ddob;

  print_test_success(success);
  upcxx::finalize();
}

