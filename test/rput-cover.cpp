#include <upcxx/upcxx.hpp>
#include "util.hpp"

using namespace upcxx;

using val_t = std::int8_t;
#define VAL(idx) ((val_t)(idx) & 0x7F)
#define BAD_VAL ((val_t)0xFF)

long errs = 0;

// options could be expanded later
#if OUT_SEG
constexpr bool outseg = true;
#else
constexpr bool outseg = false;
#endif
#if VERBOSE
constexpr bool verbose = true;
#else
constexpr bool verbose = false;
#endif

// these are globals to minimize RPC closure size and simplify logic
int peer;
val_t *my_src;
val_t *my_dst;
global_ptr<val_t> peer_dst;
promise<> done;
future<> sf,of;
long iters;

// these are kept single-valued
size_t sz;
const char *context;

// helper functions to keep things terse
void mark_done() { done.fulfill_anonymous(1); } // register a completion

void init_src(val_t v) { memset(my_src, v, sz); } // set src
void clobber_src() { memset(my_src, BAD_VAL, sz); } // clear src
void src_done() { clobber_src(); mark_done(); } 

void check_dst(val_t expected, val_t const *buf = my_dst) { // validate an LZ
  for (size_t i = 0; i < sz; i++) {
    val_t got = buf[i];
    if (got != expected) {
      say() << "ERROR: in " << context << (buf == my_dst ? " in my_dst" : "in view buffer")
            << " at " << i << " got=" << got << " expected=" << expected;
      errs++;
    }
  } 
}

void remote_done_small(val_t val) { // process RC and send ack
  check_dst(val);
  rpc_ff(peer, mark_done);
}
void remote_done_large(val_t val, view<val_t> buf) { // process RC and send ack
  check_dst(val);
  check_dst(val, buf.begin());
  rpc_ff(peer, mark_done);
}

template<int completions=3, typename Inject>
void run_test(const char *ctx, Inject &&inj) {
  context = ctx;

  if (verbose && !rank_me()) say("") << "  - " << context;
  barrier();

  sf = future<>(); of = future<>(); // init to never-ready
  for (long iter = 0; iter < iters; iter++) {
    val_t val = VAL(iter); 
    init_src(val);
    done = promise<>(completions);

    inj(val);

    done.get_future().wait();
  }

  barrier();
}


int main(int argc, char *argv[]) {
  upcxx::init();
  print_test_header();

  int me = upcxx::rank_me();
  int ranks = upcxx::rank_n();
  if (ranks > 1 && ranks % 2) {
    print_test_skipped("test requires unary or even rank count");
    upcxx::finalize();
    return 0;
  }
  // peer = (me ^ 1) % ranks; // nearest-neighbor
  // cross-machine pairing:
  if (me < ranks/2) peer = me + ranks/2;
  else              peer = me - ranks/2;

  iters = 0;
  if (argc > 1) iters = std::atol(argv[1]);
  if (iters <= 0) iters = 10;
  size_t maxelems;
  { size_t bufsz = 0;
    if (argc > 2) bufsz = std::atol(argv[2]);
    if (bufsz <= 0) bufsz = 1024*1024;
    if (bufsz < sizeof(val_t)) bufsz = sizeof(val_t);
    maxelems = bufsz / sizeof(val_t);
    if (!me) say("") << "Running with iters=" << iters << " bufsz=" << maxelems*sizeof(val_t) << " bytes"; 
  }

  my_src = (outseg ? new val_t[maxelems] : new_array<val_t>(maxelems).local());
  dist_object<global_ptr<val_t>> do_dst(new_array<val_t>(maxelems));
  my_dst = do_dst->local();
  peer_dst = do_dst.fetch(peer).wait();

  for (sz = 1; sz <= maxelems; sz *= 2) {
    if (!me) say("") << "* Payload size = " << sz;

    // async OC
    
    run_test("!SC && OC && !RC", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          operation_cx::as_future()
      );
      of.then(src_done);
      of.then([=]() { rpc_ff(peer, remote_done_small, val); } );
      of.then(mark_done);
    });

    run_test("!SC && OC && RC-small", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      of.then(src_done);
      of.then(mark_done);
    });

    run_test("!SC && OC && RC-large", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      of.then(src_done);
      of.then(mark_done);
    });

    run_test("SC && OC && !RC", [](val_t val) {
      std::tie(sf,of) = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_future()
      );
      sf.then(src_done);
      of.then([=]() { rpc_ff(peer, remote_done_small, val); } );
      of.then(mark_done);
    });

    run_test("SC && OC && RC-small", [](val_t val) {
      std::tie(sf,of) = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      sf.then(src_done);
      of.then(mark_done);
    });

    run_test("SC && OC && RC-large", [](val_t val) {
      std::tie(sf,of) = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      sf.then(src_done);
      of.then(mark_done);
    });

    run_test("SC-block && OC && !RC", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_future()
      );
      src_done();
      of.then([=]() { rpc_ff(peer, remote_done_small, val); } );
      of.then(mark_done);
    });

    run_test("SC-block && OC && RC-small", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      src_done();
      of.then(mark_done);
    });

    run_test("SC-block && OC && RC-large", [](val_t val) {
      of = rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_future()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      src_done();
      of.then(mark_done);
    });

  #if UPCXXI_HAS_OPERATION_CX_AS_BLOCKING
    // blocking OC
    // NOTE: operation_cx::as_blocking() is currently unspecified and should not be relied upon by users.
    // The calls below ensure white-box code coverage for the implementation.
    
    run_test<2>("!SC && OC-block && !RC", [](val_t val) {
      rput(my_src, peer_dst, sz,
          operation_cx::as_blocking()
      );
      src_done();
      rpc_ff(peer, remote_done_small, val);
    });

    run_test<2>("!SC && OC-block && RC-small", [](val_t val) {
      rput(my_src, peer_dst, sz,
          operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      src_done();
    });

    run_test<2>("!SC && OC-block && RC-large", [](val_t val) {
      rput(my_src, peer_dst, sz,
          operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      src_done();
    });

    run_test<2>("SC && OC-block && !RC", [](val_t val) {
      sf = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_blocking()
      );
      sf.then(src_done);
      rpc_ff(peer, remote_done_small, val);
    });

    run_test<2>("SC && OC-block && RC-small", [](val_t val) {
      sf = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      sf.then(src_done);
    });

    run_test<2>("SC && OC-block && RC-large", [](val_t val) {
      sf = rput(my_src, peer_dst, sz,
          source_cx::as_future() | operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      sf.then(src_done);
    });

    run_test<2>("SC-block && OC-block && !RC", [](val_t val) {
      rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_blocking()
      );
      src_done();
      rpc_ff(peer, remote_done_small, val);
    });

    run_test<2>("SC-block && OC-block && RC-small", [](val_t val) {
      rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      src_done();
    });

    run_test<2>("SC-block && OC-block && RC-large", [](val_t val) {
      rput(my_src, peer_dst, sz,
          source_cx::as_blocking() | operation_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      src_done();
    });
  #endif // UPCXXI_HAS_OPERATION_CX_AS_BLOCKING

    // no OC - commented out cases are ill-formed
    
    //run_test("!SC && !OC && !RC"

    run_test<1>("!SC && !OC && RC-small", [](val_t val) {
      rput(my_src, peer_dst, sz,
          remote_cx::as_rpc(remote_done_small, val)
      );
    });

    run_test<1>("!SC && !OC && RC-large", [](val_t val) {
      rput(my_src, peer_dst, sz,
          remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
    });

    //run_test("SC && !OC && !RC"

    run_test<2>("SC && !OC && RC-small", [](val_t val) {
      sf = rput(my_src, peer_dst, sz,
          source_cx::as_future() 
        | remote_cx::as_rpc(remote_done_small, val)
      );
      sf.then(src_done);
    });

    run_test<2>("SC && !OC && RC-large", [](val_t val) {
      sf = rput(my_src, peer_dst, sz,
          source_cx::as_future()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      sf.then(src_done);
    });

    // run_test("SC-block && !OC && !RC"

    run_test<2>("SC-block && !OC && RC-small", [](val_t val) {
      rput(my_src, peer_dst, sz,
          source_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_small, val)
      );
      src_done();
    });

    run_test<2>("SC-block && !OC && RC-large", [](val_t val) {
      rput(my_src, peer_dst, sz,
          source_cx::as_blocking()
        | remote_cx::as_rpc(remote_done_large, val, make_view(my_src, my_src+sz))
      );
      src_done();
    });

    barrier();
  } // sz

  upcxx::delete_array(*do_dst);
  if (outseg) delete [] my_src;
  else delete_array(to_global_ptr(my_src));

  print_test_success(errs == 0);
  
  upcxx::finalize();
  return 0;
}
