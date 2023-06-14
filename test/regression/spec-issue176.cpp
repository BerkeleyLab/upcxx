#include <upcxx/upcxx.hpp>
#include <unistd.h>
#include "../util.hpp"

using namespace std;
using namespace upcxx;

size_t freemem() {
  return shared_segment_size() - shared_segment_used();
}

#if defined(__PGIC__) && __PGIC__ < 20
// PGI 18.10/19.3 on PPC whines about large types that are never even instantiated..
#define BROKEN_COMPILER 1
#endif

#if !BROKEN_COMPILER
struct huge {
  char dummy[1LLU<<33];
};
#endif

struct byte_bag { // serialization ubound is deliberately unbounded
  static constexpr size_t sz = 4096;
  char data[sz];
  struct upcxx_serialization {
     template<typename Writer>
     static void serialize(Writer& writer, byte_bag const & object) {
       writer.write_sequence(object.data, object.data+sz);
     }
     template<typename Reader>
     static byte_bag* deserialize(Reader& reader, void* storage) {
       byte_bag *r = ::new(storage) byte_bag;
       reader.template read_sequence_into<char>(r->data, sz);
       return r;
     }
  };
};

struct naughty_serialize {
  struct upcxx_serialization {
     template<typename Writer>
     static void serialize(Writer& writer, naughty_serialize const & object) {
       throw std::runtime_error("From Hell's heart, I stab at thee");
     }
     template<typename Reader>
     static naughty_serialize* deserialize(Reader& reader, void* storage) {
       return ::new(storage) naughty_serialize;
     }
  };
};
struct naughty_deserialize {
  char dummy;
  naughty_deserialize() : dummy(0) {}
  naughty_deserialize(int x) {
    throw std::runtime_error("For hate's sake, I spit my last breath at thee.");
  }
  UPCXX_SERIALIZED_VALUES(0)
};

struct tracker {
  static int live_count;
  tracker() { live_count++; }
  tracker(const tracker&) { live_count++; }
  tracker(tracker&&) { live_count++; }
  ~tracker() { live_count--; }
  void operator()() {
    say() << "ERROR: Call to tracker::operator()";
  }
  void operator()(int) {
    say() << "ERROR: Call to tracker::operator(int)";
  }
  static void check() { 
    if (live_count) say() << "ERROR: " << live_count << " tracker destructors skipped";
    live_count = 0; // reset for run-thru
  }
};
int tracker::live_count = 0;

int main() {
    upcxx::init();
    print_test_header();

    barrier();
    size_t snapshot = freemem();
    barrier();
    size_t toobig = 2*snapshot;
    byte_bag *bb = nullptr;
    size_t bb_cnt = toobig / sizeof(byte_bag);
    toobig = bb_cnt * sizeof(byte_bag); // round down
    try {
      bb = new byte_bag[bb_cnt];
      UPCXX_ASSERT_ALWAYS(bb);
    } catch (std::exception &e) {
      say() << "ERROR: Caught exception allocating large private object: " << e.what();
      abort();
    }
    char *bigbuf = reinterpret_cast<char *>(bb);
    auto big_view = make_view(bigbuf, bigbuf+toobig);
    auto unbounded_view = make_view(bb, bb+bb_cnt);

    int peer = (rank_me() + 1) % rank_n();
    global_ptr<char> gp_local = new_<char>();
    dist_object<global_ptr<char>> dog(gp_local);
    global_ptr<char> gp = dog.fetch(peer).wait();
    barrier();

    #define CHECK(desc, stmt) do { \
      barrier(); \
      if (!rank_me()) say("") << "Checking " << desc << "..."; \
      barrier(); \
      try { \
        { stmt; } \
        UPCXX_ASSERT_ALWAYS(0, desc << " failed to throw expected exception!"); \
      } catch (bad_shared_alloc &e) { \
        UPCXX_ASSERT_ALWAYS(e.what() && strlen(e.what()) > 0); \
        barrier(); \
        if (!rank_me()) say("") << "passed."; \
        barrier(); \
      } \
    } while (0)
    #define CHECK_NULL(desc, expr) do { \
      barrier(); \
      if (!rank_me()) say("") << "Checking " << desc << "..."; \
      barrier(); \
      auto _r = expr; \
      UPCXX_ASSERT_ALWAYS(_r == nullptr, desc << " failed to produce expected nullptr!"); \
      barrier(); \
      if (!rank_me()) say("") << "passed."; \
      barrier(); \
    } while (0)

  
    CHECK("upcxx::new_array<char>(toobig)", auto g = new_array<char>(toobig));
  #if !BROKEN_COMPILER
    CHECK("upcxx::new_<huge>()", auto g = new_<huge>());
  #endif
    CHECK_NULL("upcxx::allocate(toobig)", allocate(toobig));
    CHECK_NULL("upcxx::allocate<char>(toobig)", allocate<char>(toobig));

    CHECK("rpc_ff(view(char[toobig]))", 
          rpc_ff(peer, [](view<char> const &v) {}, big_view););

    CHECK("rpc_ff(view(unbounded))", 
          rpc_ff(peer, [](view<byte_bag> const &v) {}, unbounded_view););

    CHECK("rpc(view(char[toobig]))", 
          auto f = rpc(peer, [](view<char> const &v) {}, big_view););

    CHECK("rpc(view(unbounded))", 
          auto f = rpc(peer, [](view<byte_bag> const &v) {}, unbounded_view););

    CHECK("rpc(view(char[toobig]) -> int)", 
          auto f = rpc(peer, [](view<char> const &v) { return 0; }, big_view););

#if 0
    // rput is currently noexcept
    CHECK("rput(remote_cx::as_rpc(view(char[toobig])))", 
          rput((char)0, gp, 
               remote_cx::as_rpc([](view<char> const &v) {}, big_view)));

    CHECK("rput(remote_cx::as_rpc(view(unbounded)))", 
          rput((char)0, gp, 
               remote_cx::as_rpc([](view<byte_bag> const &v) {}, unbounded_view)));
#endif
#if 0
    // copy is currently noexcept
    CHECK("copy-put(remote_cx::as_rpc(view(char[toobig])))", 
          copy(gp_local.local(), gp, 1, 
               remote_cx::as_rpc([](view<char> const &v) {}, big_view)));
    CHECK("copy-3rd(remote_cx::as_rpc(view(char[toobig])))", 
          copy(gp, gp, 1, 
               remote_cx::as_rpc([](view<char> const &v) {}, big_view)));
#endif

    tracker::check();
    { tracker t; }
    tracker::check();

    { promise<> sp;
      promise<int> sp2;
      tracker t;
      CHECK("rpc_ff(view(char[toobig])) with completions", 
          { auto f = rpc_ff(peer, 
            source_cx::as_future() | 
            source_cx::as_promise(sp) | 
            source_cx::as_promise(sp2) | 
            source_cx::as_lpc(current_persona(), tracker()) | 
            source_cx::as_lpc(current_persona(), t)
            , [](view<char> const &v) {}, big_view);
          });
      if (!sp.finalize().is_ready()) say() << "ERROR: source_cx::as_promise() completion was not cleaned up.";
      sp2.fulfill_result(42);
      if (!sp2.get_future().is_ready()) say() << "ERROR: source_cx::as_promise(promise<int>) completion was not cleaned up.";
    }
    tracker::check();

    { promise<> sp,p;
      promise<int> sp2,p2,p3;
      tracker t;
      CHECK("rpc(view(char[toobig])) with completions", 
         {  auto ftup = rpc(peer, 
            source_cx::as_future() | 
            source_cx::as_promise(sp) | 
            source_cx::as_promise(sp2) | 
            source_cx::as_lpc(current_persona(), tracker()) | 
            source_cx::as_lpc(current_persona(), t) | 
            operation_cx::as_future() | 
            operation_cx::as_promise(p) | 
            operation_cx::as_promise(p2) | 
            operation_cx::as_promise(p3) | 
            operation_cx::as_promise(p3) | 
            operation_cx::as_lpc(current_persona(), tracker()) |
            operation_cx::as_lpc(current_persona(), t) 
            , [](view<char> const &v) {}, big_view);
         });
      if (!sp.finalize().is_ready()) say() << "ERROR: source_cx::as_promise() completion was not cleaned up.";
      sp2.fulfill_result(42);
      if (!sp2.get_future().is_ready()) say() << "ERROR: source_cx::as_promise(promise<int>) completion was not cleaned up.";

      if (!p.finalize().is_ready()) say() << "ERROR: operation_cx::as_promise() completion was not cleaned up.";
      p2.fulfill_result(42);
      if (!p2.get_future().is_ready()) say() << "ERROR: operation_cx::as_promise(promise<int>) completion was not cleaned up.";
      p3.fulfill_result(42);
      if (!p3.get_future().is_ready()) say() << "ERROR: operation_cx::as_promise(promise<int>)x2 completion was not cleaned up.";
    }
    tracker::check();

    { promise<> sp;
      promise<int> sp2,p2,p3;
      tracker t;
      CHECK("rpc(view(char[toobig]) -> int) with completions", 
         {  auto ftup = rpc(peer, 
            source_cx::as_future() | 
            source_cx::as_promise(sp) | 
            source_cx::as_promise(sp2) | 
            source_cx::as_lpc(current_persona(), tracker()) | 
            source_cx::as_lpc(current_persona(), t) | 
            operation_cx::as_future() | 
            operation_cx::as_future() | 
            operation_cx::as_promise(p2) | 
            operation_cx::as_promise(p3) | 
            operation_cx::as_promise(p3) | // corner case: this is ONLY valid because of the exception
            operation_cx::as_lpc(current_persona(), tracker()) |
            operation_cx::as_lpc(current_persona(), t) 
            , [](view<char> const &v) { return 0; }, big_view);
         });
      if (!sp.finalize().is_ready()) say() << "ERROR: source_cx::as_promise() completion was not cleaned up.";
      sp2.fulfill_result(42);
      if (!sp2.get_future().is_ready()) say() << "ERROR: source_cx::as_promise(promise<int>) completion was not cleaned up.";

      p2.fulfill_result(42); // crash here indicates erroneous fulfillment
      if (!p2.get_future().is_ready()) say() << "ERROR: operation_cx::as_promise(promise<int>) completion was not cleaned up.";
      p3.fulfill_result(42); // crash here indicates erroneous fulfillment
      if (!p3.get_future().is_ready()) say() << "ERROR: operation_cx::as_promise(promise<int>)x2 completion was not cleaned up.";
    }
    tracker::check();

    // the following defines enable ERRONEOUS behavior
    // which should be diagnosed in debug codemode
    #if TEST_THROW_FROM_PROGRESS
      if (!rank_me()) say("") << "Throwing an exception into progress()..."; \
      barrier();
      try {
        current_persona().lpc([=]() {
           rpc_ff(peer,[](view<char> const &v) {}, big_view);
           say() << "ERROR: failure to throw expected exception";
          }).wait();
      } catch (std::exception &e) {
        say() << "ERROR: exception thrown out of progress\n" << e.what();
      }
      barrier();
    #endif

    #if TEST_THROW_FROM_SERIALIZE
      if (!rank_me()) say("") << "Throwing an exception from serialize()..."; \
      barrier();
      try {
        rpc_ff(peer,[](naughty_serialize const &) {
           say() << "ERROR: failure to throw expected exception 1";
        }, naughty_serialize());
        say() << "ERROR: failure to throw expected exception 2";
      } catch (std::exception &e) {
        say() << "ERROR: exception thrown out of rpc_ff\n" << e.what();
      }
      barrier();
    #endif

    #if TEST_THROW_FROM_DESERIALIZE
      if (!rank_me()) say("") << "Throwing an exception from deserialize()..."; \
      barrier();
      try {
        rpc_ff(peer,[](naughty_deserialize const &) {
           say() << "ERROR: failure to throw expected exception";
        }, naughty_deserialize());
        discharge(); sleep(1); progress(); abort();
      } catch (std::exception &e) {
        say() << "ERROR: exception thrown out of rpc_ff\n" << e.what();
      }
      barrier();
    #endif

    #if TEST_THROW_FROM_SERIAL_VALUE
      if (!rank_me()) say("") << "Throwing an exception from deserialized_value()..."; \
      barrier();
      try {
        naughty_serialize n;
        auto x = serialization_traits<naughty_serialize>::deserialized_value(n);
        say() << "ERROR: failure to throw expected exception";
      } catch (std::exception &e) {
        say() << "ERROR: exception thrown out of deserialized_value\n" << e.what();
      }
      barrier();
    #endif

    barrier();
    delete [] bb;
    delete_(gp_local);

    barrier();
    size_t snapshot2 = freemem();
    if (snapshot2 != snapshot) 
      say() << "ERROR: unexpected change in shared heap: " << snapshot << " != " << snapshot2;

    print_test_success(true);
    upcxx::finalize();
    return 0;
} 
