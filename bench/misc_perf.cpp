// This micro-benchmark measures the cost of selected library operations, 
// focusing on CPU overheads.
//
// Usage: a.out (iterations)

#include <upcxx/upcxx.hpp>
#include <gasnetex.h>
#include <gasnet_tools.h>
#include <memory>
#include <iostream>
#include <sstream>
#include <vector>

#ifndef ATTRIB_NOINLINE
#define ATTRIB_NOINLINE __attribute__((__noinline__))
#endif

gasnett_tick_t ticktime(void) { return gasnett_ticks_now(); }
uint64_t tickcvt(gasnett_tick_t ticks) { return gasnett_ticks_to_ns(ticks); }
static int accuracy = 6;
void report(const char *desc, int64_t totaltime, int iters) ATTRIB_NOINLINE;
void report(const char *desc, int64_t totaltime, int iters) {
  if (!upcxx::rank_me()) {
      char format[80];
      snprintf(format, sizeof(format), "%i: %%-60s: %%%i.%if s  %%%i.%if us\n",
              upcxx::rank_me(), (4+accuracy), accuracy, (4+accuracy), accuracy);
      printf(format, desc, totaltime/1.0E9, (totaltime/1000.0)/iters);
      fflush(stdout);
  }
}

extern int volatile ctr;
int volatile ctr = 0;
void direct_inc(void) ATTRIB_NOINLINE;
void direct_inc(void) {
  ctr = 1 + ctr; // C++20 deprecates ++ and += on volatile
}

void noop0(void) {
}

void noop8(int d1, int d2, int d3, int d4, int d5, int d6, int d7, int d8) {
}

void doit() ATTRIB_NOINLINE;
void doit1() ATTRIB_NOINLINE;
void doit2() ATTRIB_NOINLINE;
void doit3() ATTRIB_NOINLINE;

#define TIME_OPERATION_FULL(desc, preop, op, postop, fullduplex) do {  \
  if (self > peer && !fullduplex) for (int i=0;i<3;i++) upcxx::barrier(); \
  else {                                                   \
    int i, _iters = iters, _warmupiters = MAX(1,iters/10); \
    gasnett_tick_t start,end;  /* use ticks interface */   \
    upcxx::barrier();          /* for best accuracy */     \
    preop;       /* warm-up */                             \
    for (i=0; i < _warmupiters; i++) { op; }               \
    postop;                                                \
    upcxx::barrier();                                      \
    start = ticktime();                                    \
    preop;                                                 \
    for (i=0; i < _iters; i++) { op; }                     \
    postop;                                                \
    end = ticktime();                                      \
    upcxx::barrier();                                      \
    if (((const char *)(desc)) && ((char*)(desc))[0])      \
      report((desc), tickcvt(end - start), iters);         \
    else report(#op, tickcvt(end - start), iters);         \
  }                                                        \
} while (0)
#define TIME_OPERATION(desc, op) TIME_OPERATION_FULL(desc, {}, op, {}, 0)

int nranks, self, peer;
uint64_t iters = 10000;

int main(int argc, char **argv) {
  if (argc > 1) iters = atol(argv[1]);
  upcxx::init();

  nranks = upcxx::rank_n();
  self = upcxx::rank_me();
  // cross-machine symmetric pairing
  if (nranks % 2 == 1 && self == nranks - 1) peer = self;
  else if (self < nranks/2) peer = self + nranks/2;
  else peer = self - nranks/2;
  std::stringstream ss;

  ss << self << "/" << nranks << " : " << gasnett_gethostname() << " : peer=" << peer << "\n";
  std::cout << ss.str() << std::flush;

  upcxx::barrier();

  if (!upcxx::rank_me()) {
      printf("Running misc performance test with %llu iterations...\n",(unsigned long long)iters);
      printf("%-60s    Total time    Avg. time\n"
             "%-60s    ----------    ---------\n", "", "");
      fflush(stdout);
  }
    if (!upcxx::rank_me()) std::cout << "\n Serial overhead tests:" << std::endl;

  doit();

  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout << "SUCCESS" << std::endl;

  upcxx::finalize();

  return 0;
}
// artificially split up into separate functions to get better default behavior from the optimizer
void doit() {
    TIME_OPERATION("measurement overhead",{ ctr = 1 + ctr; }); // C++20 deprecates ++ and += on volatile
    TIME_OPERATION("direct function call",direct_inc());

    upcxx::global_ptr<char> gpb1 = upcxx::new_array<char>(4096);
    upcxx::global_ptr<char> gpb2 = upcxx::new_array<char>(4096);
    char *buf1 = gpb1.local();
    char *buf2 = gpb2.local();
    TIME_OPERATION("memcpy(4KB)",std::memcpy(buf1, buf2, 4096));
    upcxx::delete_array(gpb1);
    upcxx::delete_array(gpb2);

    doit1();
}
struct dummy64_t { char data[64]; }; // 64 bytes of TriviallySerializable data
struct dummy256_t { char data[256]; }; // 256 bytes of TriviallySerializable data

template<int sz>
struct myarr { // a silly, but minimal container whose serialization ubound is deliberately unbounded
  char data[sz];
  struct upcxx_serialization {
     template<typename Writer>
     static void serialize (Writer& writer, myarr const & object) {
       writer.write_sequence(object.data, object.data+sz);
     }
     template<typename Reader, typename Storage>
     static myarr* deserialize(Reader& reader, Storage storage) {
       myarr *r = storage.construct();
       reader.template read_sequence_into<char>(r->data, sz);
       return r;
     }
  };
};
void doit1() {
    if (!upcxx::rank_me()) std::cout << "\n Local UPC++ tests:" << std::endl;
    TIME_OPERATION("upcxx::progress (do-nothing)",upcxx::progress());

    upcxx::persona &selfp = upcxx::current_persona();
    TIME_OPERATION("self.lpc(noop0) latency",selfp.lpc(&noop0).wait());
    TIME_OPERATION("self.lpc(lamb0) latency",selfp.lpc([](){}).wait());
    {
      static std::int64_t sent=0,recv=0;
      { std::vector<upcxx::future<>> fs; fs.reserve(iters);
        TIME_OPERATION_FULL("self.lpc(lamb0) inv. throughput", {},
                       fs.push_back(selfp.lpc([](){}));
                    , { for (auto f: fs) f.wait(); 
                        fs.clear(); /* releases futures */ }, 1);
      }
    #if 0
      // Alternate version that under-estimates inv throughput:
      TIME_OPERATION_FULL("self.lpc(lamb0) inv. throughput (lower-bound)", {},
                       sent++;
                       (void)selfp.lpc([](){recv++;}); /* deliberately drops ack future */
                    , { while (recv<sent) { upcxx::progress(); } }, 1);
      // following is correct but incurs alot more overhead in practice:
      TIME_OPERATION_FULL("self.lpc(lamb0) inv. throughput (upper-bound, future chain)", {},
                       sent++;
                       selfp.lpc([](){}).then([](){recv++;});
                    , { while (recv<sent) { upcxx::progress(); } }, 1);
    #endif

      // lpc_ff
      TIME_OPERATION_FULL("self.lpc_ff(lamb0) latency", {},
                       sent++;
                       selfp.lpc_ff([](){recv++;});
                       while (recv<sent) { upcxx::progress(); } 
                    , {}, 1);
      TIME_OPERATION_FULL("self.lpc_ff(lamb0) inv. throughput", {},
                       sent++;
                       selfp.lpc_ff([](){recv++;});
                    , { while (recv<sent) { upcxx::progress(); } }, 1);
    }

    TIME_OPERATION("upcxx::rpc(self,noop0)",upcxx::rpc(self,&noop0).wait());
    TIME_OPERATION("upcxx::rpc(self,noop8)",upcxx::rpc(self,&noop8,0,0,0,0,0,0,0,0).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb0)",upcxx::rpc(self,[](){}).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb8)",upcxx::rpc(self,
        [](int d1, int d2, int d3, int d4, int d5, int d6, int d7, int d8){},
        0,0,0,0,0,0,0,0).wait());

    static dummy64_t dummy64;
    static dummy256_t dummy256;
    myarr<64> a64;
    myarr<256> a256;
    myarr<512> a512;
    TIME_OPERATION("upcxx::rpc(self,lamb 64b static)",upcxx::rpc(self,
        [](const dummy64_t &){}, dummy64).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 64b view)",upcxx::rpc(self,
        [](const upcxx::view<char> &){}, upcxx::make_view(dummy64.data, dummy64.data+64)).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 64b unbounded)",upcxx::rpc(self,
        [](const myarr<64> &){}, a64).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 256b static)",upcxx::rpc(self,
        [](const dummy256_t &){}, dummy256).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 256b view)",upcxx::rpc(self,
        [](const upcxx::view<char> &){}, upcxx::make_view(dummy256.data, dummy256.data+256)).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 256b unbounded)",upcxx::rpc(self,
        [](const myarr<256> &){}, a256).wait());
    TIME_OPERATION("upcxx::rpc(self,lamb 512b unbounded)",upcxx::rpc(self,
        [](const myarr<512> &){}, a512).wait());

    doit2();
}
upcxx::global_ptr<double> gp;
upcxx::global_ptr<double> gp_peer;
upcxx::global_ptr<std::int64_t> gpi64;
upcxx::global_ptr<std::int64_t> gpi64_peer;
void doit2() {
    upcxx::dist_object<upcxx::global_ptr<double>> dod(upcxx::new_<double>(0));
    gp = *dod;
    gp_peer = dod.fetch(peer).wait();
    TIME_OPERATION("upcxx::rput<double>(self, as_defer_future)",upcxx::rput(0.,gp, upcxx::operation_cx::as_defer_future()).wait());
    TIME_OPERATION("upcxx::rput<double>(self, as_eager_future)",upcxx::rput(0.,gp, upcxx::operation_cx::as_eager_future()).wait());
    TIME_OPERATION("upcxx::rget<double>(self, as_defer_future)",upcxx::rget(gp, upcxx::operation_cx::as_defer_future()).wait());
    TIME_OPERATION("upcxx::rget<double>(self, as_eager_future)",upcxx::rget(gp, upcxx::operation_cx::as_eager_future()).wait());
    {
      double dst;
      TIME_OPERATION("upcxx::rget<double>(self, T*, 1, as_defer_future)",upcxx::rget(gp, &dst, 1, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("upcxx::rget<double>(self, T*, 1, as_eager_future)",upcxx::rget(gp, &dst, 1, upcxx::operation_cx::as_eager_future()).wait());
    }
    {
      static int flag;
      TIME_OPERATION("upcxx::rput<double>(self, RC)", 
                       flag = 0;
                       upcxx::rput(0.,gp, upcxx::remote_cx::as_rpc([](){flag=1;}));
                       do { upcxx::progress(); } while (!flag)
                    );
    }

    {
      static int flag;
      static dummy64_t dummy;
      TIME_OPERATION("upcxx::rput<double>(self, RC-64b)", 
                       flag = 0;
                       upcxx::rput(0.,gp, upcxx::remote_cx::as_rpc([](const dummy64_t &){flag=1;},dummy));
                       do { upcxx::progress(); } while (!flag)
                    );
    }

    upcxx::dist_object<upcxx::global_ptr<std::int64_t>> doi(upcxx::new_<std::int64_t>(0));
    gpi64 = *doi;
    gpi64_peer = doi.fetch(peer).wait();

    using upcxx::atomic_op;
    { upcxx::promise<> p;
      upcxx::atomic_domain<std::int64_t> ad_fa({atomic_op::fetch_add, atomic_op::add}, 
                                               upcxx::local_team());
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, defer_promise) overhead", // sync is outside loop
                     ad_fa.add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_defer_promise(p)));
      p.finalize().wait();
      p = upcxx::promise<>{};
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, eager_promise) overhead", // sync is outside loop
                     ad_fa.add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_eager_promise(p)));
      p.finalize().wait();
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, defer_future)",
                     ad_fa.add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, eager_future)",
                     ad_fa.add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_eager_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::fetch_add(relaxed, defer_future)",
                     ad_fa.fetch_add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::fetch_add(relaxed, eager_future)",
                     ad_fa.fetch_add(gpi64, 1, std::memory_order_relaxed, upcxx::operation_cx::as_eager_future()).wait());
      std::int64_t dst;
      TIME_OPERATION("atomic_domain<int64>::fetch_add(T*, relaxed, defer_future)",
                     ad_fa.fetch_add(gpi64, 1, &dst, std::memory_order_relaxed, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::fetch_add(T*, relaxed, eager_future)",
                     ad_fa.fetch_add(gpi64, 1, &dst, std::memory_order_relaxed, upcxx::operation_cx::as_eager_future()).wait());
      ad_fa.destroy();
    }

    doit3();
}
void doit3() {
  if (upcxx::rank_n() > 1) {
    bool all_local = upcxx::reduce_all(upcxx::local_team_contains(peer), upcxx::op_fast_bit_and).wait();
    if (!upcxx::rank_me()) std::cout << "\n Remote UPC++ tests: " 
                                     << (all_local ? "(local_team())" : "(world())") << std::endl;
    TIME_OPERATION("upcxx::rpc(peer,noop0)",upcxx::rpc(peer,&noop0).wait());
    TIME_OPERATION("upcxx::rpc(peer,noop8)",upcxx::rpc(peer,&noop8,0,0,0,0,0,0,0,0).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb0)",upcxx::rpc(peer,[](){}).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb8)",upcxx::rpc(peer,
        [](int d1, int d2, int d3, int d4, int d5, int d6, int d7, int d8){},
        0,0,0,0,0,0,0,0).wait());

    static dummy64_t dummy64;
    static dummy256_t dummy256;
    myarr<64> a64;
    myarr<256> a256;
    myarr<512> a512;
    TIME_OPERATION("upcxx::rpc(peer,lamb 64b static)",upcxx::rpc(peer,
        [](const dummy64_t &){}, dummy64).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 64b view)",upcxx::rpc(peer,
        [](const upcxx::view<char> &){}, upcxx::make_view(dummy64.data, dummy64.data+64)).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 64b unbounded)",upcxx::rpc(peer,
        [](const myarr<64> &){}, a64).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 256b static)",upcxx::rpc(peer,
        [](const dummy256_t &){}, dummy256).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 256b view)",upcxx::rpc(peer,
        [](const upcxx::view<char> &){}, upcxx::make_view(dummy256.data, dummy256.data+256)).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 256b unbounded)",upcxx::rpc(peer,
        [](const myarr<256> &){}, a256).wait());
    TIME_OPERATION("upcxx::rpc(peer,lamb 512b unbounded)",upcxx::rpc(peer,
        [](const myarr<512> &){}, a512).wait());

    TIME_OPERATION("upcxx::rput<double>(peer, as_defer_future)",upcxx::rput(0.,gp_peer, upcxx::operation_cx::as_defer_future()).wait());
    TIME_OPERATION("upcxx::rput<double>(peer, as_eager_future)",upcxx::rput(0.,gp_peer, upcxx::operation_cx::as_eager_future()).wait());
    TIME_OPERATION("upcxx::rget<double>(peer, as_defer_future)",upcxx::rget(gp_peer, upcxx::operation_cx::as_defer_future()).wait());
    TIME_OPERATION("upcxx::rget<double>(peer, as_eager_future)",upcxx::rget(gp_peer, upcxx::operation_cx::as_eager_future()).wait());
    {
      double dst;
      TIME_OPERATION("upcxx::rget<double>(peer, T*, 1, as_defer_future)",upcxx::rget(gp_peer, &dst, 1, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("upcxx::rget<double>(peer, T*, 1, as_eager_future)",upcxx::rget(gp_peer, &dst, 1, upcxx::operation_cx::as_eager_future()).wait());
    }
    {
      static std::int64_t sent=0,recv=0;
      TIME_OPERATION_FULL("upcxx::rput<double>(peer, RC)", {},
                       sent++;
                       upcxx::rput(0.,gp_peer, upcxx::remote_cx::as_rpc([](){recv++;}));
                       do { upcxx::progress(); } while (recv<sent)
                    , {}, 1);
    }
    {
      static std::int64_t sent=0,recv=0;
      static dummy64_t dummy;
      TIME_OPERATION_FULL("upcxx::rput<double>(peer, RC-64b)", {},
                       sent++;
                       upcxx::rput(0.,gp_peer, upcxx::remote_cx::as_rpc([](const dummy64_t &){recv++;},dummy));
                       do { upcxx::progress(); } while (recv<sent)
                    , {}, 1);
    }

    using upcxx::atomic_op;
    { upcxx::promise<> p;
      upcxx::team &ad_team = (all_local ? upcxx::local_team() : upcxx::world());
      upcxx::atomic_domain<std::int64_t> ad_fa({atomic_op::fetch_add, atomic_op::add}, ad_team);
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, defer_promise) overhead", // sync is outside loop
                     ad_fa.add(gpi64_peer, 1, std::memory_order_relaxed, upcxx::operation_cx::as_defer_promise(p)));
      TIME_OPERATION("atomic_domain<int64>::add(relaxed, eager_promise) overhead", // sync is outside loop
                     ad_fa.add(gpi64_peer, 1, std::memory_order_relaxed, upcxx::operation_cx::as_eager_promise(p)));
      p.finalize().wait();
      TIME_OPERATION("atomic_domain<int64>::fetch_add(relaxed, defer_future)",
                     ad_fa.fetch_add(gpi64_peer, 1, std::memory_order_relaxed, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::fetch_add(relaxed, eager_future)",
                     ad_fa.fetch_add(gpi64_peer, 1, std::memory_order_relaxed, upcxx::operation_cx::as_eager_future()).wait());
      std::int64_t dst;
      TIME_OPERATION("atomic_domain<int64>::fetch_add(T*, relaxed, defer_future)",
                     ad_fa.fetch_add(gpi64_peer, 1, &dst, std::memory_order_relaxed, upcxx::operation_cx::as_defer_future()).wait());
      TIME_OPERATION("atomic_domain<int64>::fetch_add(T*, relaxed, eager_future)",
                     ad_fa.fetch_add(gpi64_peer, 1, &dst, std::memory_order_relaxed, upcxx::operation_cx::as_eager_future()).wait());
      ad_fa.destroy();
    }
  }
}
