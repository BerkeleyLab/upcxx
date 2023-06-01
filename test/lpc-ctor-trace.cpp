#include <iomanip>
#include <thread>
#include <upcxx/upcxx.hpp>
#include <unistd.h>
#include "util.hpp"

// NOTE: This test is carefully written to be safe in either SEQ or PAR THREADMODE
// See the rules documented in docs/implementation-defined.md

// This test measures the number of copies/moves invoked on objects passed to
// various UPC++ routines. The results asserted by this test are only indicative
// of the current implementation and should NOT be construed as a guarantee of
// copy/move behavior for past or subsequent revisions of the implementation. 
// Consult the UPC++ Specification for guaranteed copy/move behaviors.

using std::uint64_t;

struct T {
  static std::atomic<int> ctors, dtors, copies, moves;
  static void show_stats(int line, char const *title, int expected_ctors, int expected_copies,
                         int expected_moves=-1);
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

  // Deliberately NOT Serializable
  //UPCXX_SERIALIZED_FIELDS(valid)
};
static_assert(!upcxx::is_serializable<T>::value, "oops");

std::atomic<int> T::ctors{0}, T::dtors{0}, T::copies{0}, T::moves{0};

bool success = true;

void T::show_stats(int line, const char *title, int expected_ctors, int expected_copies,
                   int expected_moves) {
  upcxx::barrier();
  
  #if !SKIP_OUTPUT
  if(upcxx::rank_me() == 0) {
    say("\n")<<std::left<<std::setw(50)<<title<< " \t(line " << line << ")\n" 
           <<"  T::ctors  = "<<ctors<<"\n"
           <<"  T::copies = "<<copies<<"\n"
           <<"  T::moves  = "<<moves<<"\n"
           <<"  T::dtors  = "<<dtors;
  }
  #endif

  #define CHECK(prop, ...) do { \
    if (!(prop)) { \
      success = false; \
      say() << "ERROR: failed check: " << #prop << "\n" \
            << "    " << title << ": " << __VA_ARGS__ \
            << " \t(line " << line << ")" << "\n"; \
    } \
  } while (0)
  int retry = 0;
  while (dtors < ctors+copies+moves && ++retry < 5) {
    // handle potential race where passive target persona hasn't finished destruction
    say() << "Detected incomplete destruction, waiting...";
    sleep(1);
  }
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

std::atomic<bool> done{false};
#define set_done() do { \
  UPCXX_ASSERT_ALWAYS(done == false, "Duplicate call to set_done()"); \
  done = true; \
} while(0)

struct Fn {
  T t;
  Fn() : t(T()) { UPCXX_ASSERT_ALWAYS(done == false, "Constructed a Fn with done set"); }
  Fn(const Fn &other) : t(other.t) { UPCXX_ASSERT_ALWAYS(done == false, "Copied an Fn with done set"); }
  Fn(Fn &&other) : t(std::move(other.t)) { UPCXX_ASSERT_ALWAYS(done == false, "Moved an Fn with done set"); }
  void operator()() { set_done(); }
  // Deliberately NOT Serializable
  //UPCXX_SERIALIZED_FIELDS(t)
};
static_assert(!upcxx::is_serializable<Fn>::value, "oops");

void test_lpc(upcxx::persona &target, std::string context) {
  upcxx::barrier();
  if(upcxx::rank_me() == 0) say("") << "\n*** Testing LPC to " << context << " ***\n";
  upcxx::barrier();

  auto &initiator = upcxx::current_persona();
  int peer = (upcxx::rank_me() + 1) % upcxx::rank_n();

  // lpc
  { 
    Fn fn;
    auto f = target.lpc(fn);
    f.wait_reference();
  }
  done = false;
  SHOW("lpc(Fn&) ->", 1, 1, 1);

  { 
    auto f = target.lpc(Fn());
    f.wait_reference();
  }
  done = false;
  SHOW("lpc(Fn&&) ->", 1, 0, 2);

  // lpc_ff
  { 
    Fn fn;
    target.lpc_ff(fn);
    while (!done) { upcxx::progress(); }
  }
  done = false;
  SHOW("lpc_ff(Fn&) ->", 1, 1, 0);

  { 
    target.lpc_ff(Fn());
    while (!done) { upcxx::progress(); }
  }
  done = false;
  SHOW("lpc_ff(Fn&&) ->", 1, 0, 1);

  // exercise lpc return path
  { 
    auto f = target.lpc([]() -> T { return global; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> T", 0, 1, 1);

  { 
    auto f = target.lpc([]() -> T const & { return global; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> T const &", 0, 0, 0);

  { 
    T t;
    auto f = target.lpc([&t]() -> T&& { return std::move(t); });
    f.wait_reference();
  }
  SHOW("lpc([]&&) T& -> T&&", 1, 0, 1);

  { 
    T t;
    auto f = target.lpc([&t]() -> T { return t; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) T& -> T", 1, 1, 1);

  { 
    T t;
    auto f = target.lpc([t]() -> T { return t; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) T -> T", 1, 2, 3);

  { 
    T t;
    auto f = target.lpc([&t]() -> T const & { return t; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) T& -> T const &", 1, 0, 0);

  { 
    T t;
    auto f = target.lpc([t]() -> T const & { return t; });
    f.wait_reference();
  }
  SHOW("lpc([]&&) T -> T const &", 1, 1, 2);

  // futures along lpc return path
  { 
    auto f = target.lpc([]() { return upcxx::make_future<T>(global); });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> future<T>", 0, 2, 2);

  { 
    auto f = target.lpc([]() { return upcxx::make_future<T&>(global); });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> future<T&>", 0, 0, 0);

  { 
    auto f = target.lpc([]() { return upcxx::make_future<T>(T()); });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> future<T>", 1, 1, 2);

  { 
    auto f = target.lpc([&]() { 
       return initiator.lpc([]() {
         return upcxx::make_future<T>(T()); 
       });
    });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> future<T> chain", 1, 2, 2);

  { 
    auto f = target.lpc([&]() { 
       return initiator.lpc([]() {
         return upcxx::make_future<T>(T()); 
       }).then([](const T& t) { return t; });
    });
    f.wait_reference();
  }
  SHOW("lpc([]&&) -> future<T> chain w/then", 1, 3, 5);

  // put: as_lpc

  using upcxx::operation_cx;
  using upcxx::source_cx;

  upcxx::dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
  upcxx::global_ptr<int> gp = dobj.fetch(peer).wait();
  upcxx::global_ptr<int> gp_local = *dobj;
  upcxx::barrier();
  int x = 0; int *lp = &x;

  { 
    Fn fn;
    upcxx::rput(42, gp, operation_cx::as_lpc(target, fn));
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("operation_cx::as_lpc(Fn&) ->", 1, 1, 4);

  upcxx::rput(42, gp, operation_cx::as_lpc(target, Fn()));
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("operation_cx::as_lpc(Fn&&) ->", 1, 0, 5);

  { 
    Fn fn;
    upcxx::rput(lp, gp, 1, source_cx::as_lpc(target, fn)|operation_cx::as_future()).wait();
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("source_cx::as_lpc(Fn&) ->", 1, 1, 5);

  upcxx::rput(lp, gp, 1, source_cx::as_lpc(target, Fn())|operation_cx::as_future()).wait();
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("source_cx::as_lpc(Fn&&) ->", 1, 0, 6);

  // copy: as_lpc

  { 
    Fn fn;
    upcxx::copy(lp, gp, 1, operation_cx::as_lpc(target, fn));
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-put: operation_cx::as_lpc(Fn&) ->", 1, 1, 4);

  upcxx::copy(lp, gp, 1, operation_cx::as_lpc(target, Fn()));
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-put: operation_cx::as_lpc(Fn&&) ->", 1, 0, 5);

  { 
    Fn fn;
    upcxx::copy(lp, gp, 1, source_cx::as_lpc(target, fn)|operation_cx::as_future()).wait();
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-put: source_cx::as_lpc(Fn&) ->", 1, 1, 5);

  upcxx::copy(lp, gp, 1, source_cx::as_lpc(target, Fn())|operation_cx::as_future()).wait();
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-put source_cx::as_lpc(Fn&&) ->", 1, 0, 6);

  { 
    Fn fn;
    upcxx::copy(gp, lp, 1, operation_cx::as_lpc(target, fn));
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-get: operation_cx::as_lpc(Fn&) ->", 1, 1, 4);

  upcxx::copy(gp, lp, 1, operation_cx::as_lpc(target, Fn()));
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-get: operation_cx::as_lpc(Fn&&) ->", 1, 0, 5);

  { 
    Fn fn;
    upcxx::copy(gp, lp, 1, source_cx::as_lpc(target, fn)|operation_cx::as_future()).wait();
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-get: source_cx::as_lpc(Fn&) ->", 1, 1, 5);

  upcxx::copy(gp, lp, 1, source_cx::as_lpc(target, Fn())|operation_cx::as_future()).wait();
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-get source_cx::as_lpc(Fn&&) ->", 1, 0, 6);

  { 
    Fn fn;
    upcxx::copy(lp, gp_local, 1, operation_cx::as_lpc(target, fn));
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-loopback: operation_cx::as_lpc(Fn&) ->", 1, 1, 4);

  upcxx::copy(lp, gp_local, 1, operation_cx::as_lpc(target, Fn()));
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-loopback: operation_cx::as_lpc(Fn&&) ->", 1, 0, 5);

  { 
    Fn fn;
    upcxx::copy(lp, gp_local, 1, source_cx::as_lpc(target, fn)|operation_cx::as_future()).wait();
  }
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-loopback: source_cx::as_lpc(Fn&) ->", 1, 1, 5);

  upcxx::copy(lp, gp_local, 1, source_cx::as_lpc(target, Fn())|operation_cx::as_future()).wait();
  while (!done) { upcxx::progress(); }
  done = false;
  SHOW("copy-loopback source_cx::as_lpc(Fn&&) ->", 1, 0, 6);
 
  upcxx::delete_(gp_local);
}

int main() {
  upcxx::init();
  print_test_header();

  T::reset_counts(); // discount construction of global

  // first test LPC to personas on the primordial thread
  UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() != &upcxx::default_persona());
  test_lpc(upcxx::current_persona(), "current_persona (self)"); 
  test_lpc(upcxx::default_persona(), "default_persona (held)");

  // now test LPC to a persona held by a different thread
  say() << "Starting worker thread...";
  std::atomic<upcxx::persona *> worker_persona{nullptr};
  std::thread worker_thread([&]() {
     worker_persona = &upcxx::current_persona();
     while (worker_persona) {
       upcxx::progress();
       sched_yield();
     }
     upcxx::discharge();
     say() << "Exiting worker thread...";
  });
  upcxx::persona *target;
  do { target = worker_persona; sched_yield(); } while (!target);
  test_lpc(*target, "worker persona (cross-thread)");
  worker_persona = nullptr; // kill switch
  worker_thread.join();

  upcxx::barrier();
  if(upcxx::rank_me() == 0) say("") << "\n*** Testing future::then ***\n";
  upcxx::barrier();

  // then
  using upcxx::future;
  using upcxx::promise;
  using upcxx::make_future;

  { future<T> tf = make_future<T>(global);
  }
  SHOW("make_future<T>", 0, 1, 2);
  future<T> tf = make_future<T>(global);
  T::reset_counts(); // discount construction of tf

  { future<T&> tfr = make_future<T&>(global);
  }
  SHOW("make_future<T&>", 0, 0, 0);
  future<T&> tfr = make_future<T&>(global);

  { promise<T> p;
    future<T> tf = p.get_future();
    p.fulfill_result(global);
    tf.wait_reference();
  }
  SHOW("promise<T>::fulfill_result(T)", 0, 1, 0);

  { promise<T> p;
    future<T> tf = p.get_future();
    p.fulfill_result(T());
    tf.wait_reference();
  }
  SHOW("promise<T>::fulfill_result(T&&)", 1, 0, 1);

  { future<T> f = upcxx::to_future(tf); 
    f.wait_reference();
  }
  SHOW("to_future(future<T>)", 0, 0, 0);

  { future<T> wa = upcxx::when_all(tf); 
    wa.wait_reference();
  }
  SHOW("when_all(future<T>)", 0, 0, 0);

  { future<T,int> wa = upcxx::when_all(tf,4); 
    wa.wait_reference();
  }
  SHOW("when_all(future<T>,int)", 0, 1, 0);

  { future<T&,int> wa = upcxx::when_all(tfr,4); 
    wa.wait_reference();
  }
  SHOW("when_all(future<T&>,int)", 0, 0, 0);

  tf.then([](T t) {}).wait_reference();
  SHOW("future<T>::then T ->", 0, 1, 0);

  tf.then([](T const &t) {}).wait_reference();
  SHOW("future<T>::then T const & ->", 0, 0, 0);

  tf.then([](T t) { return t; }).wait_reference();
  SHOW("future<T>::then T -> T", 0, 1, 4);

  tf.then([](T const &t) { return t; }).wait_reference();
  SHOW("future<T>::then T const & -> T", 0, 1, 3);

  tf.then([](T const &t) -> T const & { return t; }).wait_reference();
  SHOW("future<T>::then T const & -> T const &", 0, 0, 0);

  tfr.then([](T t) {}).wait_reference();
  SHOW("future<T&>::then T ->", 0, 1, 0);

  tfr.then([](T const &t) {}).wait_reference();
  SHOW("future<T&>::then T const & ->", 0, 0, 0);

  tfr.then([](T const &t) -> T const & { return t; }).wait_reference();
  SHOW("future<T&>::then T const & -> T const &", 0, 0, 0);

  future<> vf = make_future();

  { future<T> f = vf.then([]() { return global; }); f.wait_reference(); }
  SHOW("future<>::then -> T", 0, 1, 3);

  { future<T> f = vf.then([]() { return T(); }); f.wait_reference(); }
  SHOW("future<>::then -> T", 1, 0, 3);

  { future<T&> f = vf.then([]() -> T & { return global; }); f.wait_reference(); }
  SHOW("future<>::then -> T &", 0, 0, 0);

  { future<const T&> f = vf.then([]() -> T const & { return global; }); f.wait_reference(); }
  SHOW("future<>::then -> T const &", 0, 0, 0);

  { future<T> f = vf.then([&tf]() { return tf; }); 
    f.wait_reference(); 
  }
  SHOW("future<>::then -> ready future<T>", 0, 0, 0);

  { future<T&> f = vf.then([&tfr]() { return tfr; }); 
    f.wait_reference(); 
  }
  SHOW("future<>::then -> ready future<T&>", 0, 0, 0);

  { future<T> f = vf.then([]() { return make_future(global); }); 
    f.wait_reference(); 
  }
  SHOW("future<>::then -> make_future<T>", 0, 1, 2);

  { future<T&> f = vf.then([]() { return make_future<T&>(global); }); 
    f.wait_reference(); 
  }
  SHOW("future<>::then -> make_future<T&>", 0, 0, 0);

  { promise<T> p;
    future<T> f = vf.then([p]() { return p.get_future(); }); 
    p.fulfill_result(T());
    f.wait_reference(); 
  }
  SHOW("future<>::then -> non-ready future<T>", 1, 0, 1);

  { promise<T&> p;
    future<T&> f = vf.then([p]() { return p.get_future(); }); 
    p.fulfill_result(global);
    f.wait_reference(); 
  }
  SHOW("future<>::then -> non-ready future<T&>", 0, 0, 0);

  print_test_success(success);
  upcxx::finalize();
}
