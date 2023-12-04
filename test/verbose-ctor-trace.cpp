#include <iomanip>
#include <upcxx/upcxx.hpp>
#include <unistd.h>
#include "util.hpp"

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

// This test invokes various UPC++ routines with a user-provided serializable object 
// that prints a detailed log of construct/deserialize/copy/move/destroy operations
// and checks for use-after-invalidate defects.
//
// The logs printed by this test reveal some unspecified library implementation
// details that are subject to change without notice. This test should not be
// interpreted as a guarantee regarding any unspecified aspect of library behavior.

using namespace upcxx;

const char *states[] = {
  "uninitialized", // 0
  "default constructed", // 1
  "move constructed", // 2
  "copy constructed", // 3
  "deserialized", // 4
  "invalid: moved out", // 5
  "invalid: destroyed" // 6
};

constexpr size_t numstates = sizeof(states)/sizeof(states[0]);
#define STATE(state) ((state) < numstates ? states[state] : "unknown state")
#define OBJSTATE(pobj) std::hex << (pobj) << ": " << STATE((pobj)->state)
#define SET_STATE(pobj, newstate) do { \
  say() << OBJSTATE(pobj) << " => " << STATE(newstate); \
  (pobj)->state = newstate; \
} while(0)

#if SKIP_PAUSE
#define PAUSE() ((void)0)
#else
#define PAUSE() sleep(1)
#endif

bool done = false;

struct T {
  static void show_stats(char const *title="");
  static void reset_counts() { ctors = copies = moves = dtors = 0; }

  T() { // default construct
    say() << "* default constructor:";
    SET_STATE(this, 1); 
    valid = true;
    ctors++; 
  }
  T(bool v, size_t s) { // deserialize
    say() << "* deserialize constructor:";
    SET_STATE(this, 4); 
    valid = true;
    ctors++; 
  }
  T(T const &that) { // copy cons
    say() << "* copy constructor:";
    UPCXX_ASSERT_ALWAYS(that.valid, "copying from an invalidated object" << OBJSTATE(&that));
    SET_STATE(this, 3);
    valid = true;
    copies++;
  }
  T(T &&that) { // move cons
    say() << "* move constructor:";
    UPCXX_ASSERT_ALWAYS(that.valid, "moving from an invalidated object" << OBJSTATE(&that));
    SET_STATE(this, 2);
    SET_STATE(&that, 5);
    valid = true;
    that.valid = false;
    moves++;
  }
  ~T() {
    say() << "* destructor:";
    SET_STATE(this, 6);
    valid = false;
    dtors++;
  }
  void operator()() {
    say() << "* operator() invoked:";
    say() << OBJSTATE(this);
    done = true;
  }

  private:
  static int ctors, dtors, copies, moves;

  bool valid = false;
  size_t state = 0;

  public:
  UPCXX_SERIALIZED_VALUES(valid, state)
};

struct Fn {
  T t;
  UPCXX_SERIALIZED_FIELDS(t)
  void operator()() { t(); }
};

int T::ctors = 0;
int T::dtors = 0;
int T::copies = 0;
int T::moves = 0;

void T::show_stats(char const *title) {
 upcxx::barrier();
 say() << "STATS: " << title  
       << "  ctors=" << ctors 
       << "  dtors=" << dtors 
       << "  copies=" << copies
       << "  moves=" << moves;
  reset_counts();
  upcxx::barrier();
}

int main() {
  upcxx::init();
  print_test_header();

  UPCXX_ASSERT_ALWAYS(rank_n() == 1 || !(rank_n() % 2), "test requires unary or even rank count");
  int peer = (rank_n() == 1 ? 0 : rank_me() ^ 1);
  bool isend = (rank_me() % 2 == 0);
  bool irecv = (rank_n() == 1) || (rank_me() % 2 == 1);

#if !SKIP_BASIC
  if (!rank_me())  say() << " === Basic tests ===";
  upcxx::barrier();
  if(isend) {
    say() << "Basic tests";
    T t1;
    T t2 = t1;
    T t3 = std::move(t2);
    T t4 = serialization_traits<T>::deserialized_value(t3);
    t4();
  }
  T::show_stats();
#endif

  upcxx::barrier();
  dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
  upcxx::global_ptr<int> gp = dobj.fetch(peer).wait();
  upcxx::global_ptr<int> gp_local = *dobj;
  int x = 0; int *lp = &x;
  upcxx::barrier();

#if !SKIP_RPUT
  if (!rank_me())  say() << " === rput: remote_cx::as_rpc(T&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::rput(lp, gp, 1, remote_cx::as_rpc(T()));
  }
  if (irecv) {
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_RPUT
  if (!rank_me())  say() << " === rput: remote_cx::as_rpc(Fn&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::rput(lp, gp, 1, remote_cx::as_rpc(Fn()));
  }
  if (irecv) {
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_RPUT
  if (!rank_me())  say() << " === rput: remote_cx::as_rpc(Fn&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    { Fn f; upcxx::rput(lp, gp, 1, remote_cx::as_rpc(f)); }
  }
  if (irecv) {
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_PUT
  if (!rank_me())  say() << " === copy-put: remote_cx::as_rpc(T&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::copy(lp, gp, 1, remote_cx::as_rpc(T()));
  }
  if (irecv) {
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_PUT
  if (!rank_me())  say() << " === copy-put: remote_cx::as_rpc(T&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    { T t; upcxx::copy(lp, gp, 1, remote_cx::as_rpc(t)); }
  }
  if (irecv) {
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_LOOP
  if (!rank_me())  say() << " === copy-loop: remote_cx::as_rpc(T&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc(T()));
    //PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif


#if !SKIP_COPY_LOOP
  if (!rank_me())  say() << " === copy-loop: remote_cx::as_rpc(T&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    { T t; upcxx::copy(gp_local, lp, 1, remote_cx::as_rpc(t)); }
    //PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif


#if !SKIP_COPY_GET
  if (!rank_me())  say() << " === copy-get: remote_cx::as_rpc(T&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::copy(gp, lp, 1, remote_cx::as_rpc(T()));
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_GET
  if (!rank_me())  say() << " === copy-get: remote_cx::as_rpc(T&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    { T t; upcxx::copy(gp, lp, 1, remote_cx::as_rpc(t)); }
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_GET
  if (!rank_me())  say() << " === copy-get: remote_cx::as_rpc(Fn&&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    upcxx::copy(gp, lp, 1, remote_cx::as_rpc(Fn()));
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif

#if !SKIP_COPY_GET
  if (!rank_me())  say() << " === copy-get: remote_cx::as_rpc(Fn&)&& ===";
  done = false;
  upcxx::barrier();
  if (isend) {
    { Fn f; upcxx::copy(gp, lp, 1, remote_cx::as_rpc(f)); }
    PAUSE();
    while (!done) { upcxx::progress(); }
  }
  T::show_stats();
#endif



  print_test_success();
  upcxx::finalize();
}
