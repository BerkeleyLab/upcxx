/* Description: Microbenchmark comparing immediate to non-immediate injection,
 *   based on GASNet Active Messages IMMEDIATE test testimm.c
 * Copyright (c) 2018-2025, The Regents of the University of California
 * Terms of use are as specified in LICENSE.txt
 */

// TODOs:
//  + Additional schedule options:
//    - Randomized
//      May have static randomization at start of run, or dynamic per-operation
//    - Work-sharing option
//      Rather than statically partitioning the ops per-rank, just keep a total count
//    - "nbrhd-balanced" in which consecutive operations are to distinct nbrhds
//      This should reduce ingress bottlenecks, but may not "fit" if nbrhds have unequal sizes

#include <cassert>
#include <vector>

#include <upcxx/upcxx.hpp>
#include "common/report.hpp"

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

// Enable direct calls to GASNet-EX active messages: (NOT a supported feature of UPC++)
#include <upcxx/upcxx_internal.hpp> 
#include <gasnetex.h>
#include <gasnet_tools.h>

using namespace upcxx;
using upcxx::experimental::say;

#define GASNET_Safe(fncall) do {                                     \
    int _retval;                                                     \
    if ((_retval = fncall) != GASNET_OK) {                           \
      fprintf(stderr, "ERROR calling: %s\n"                          \
                   " at: %s:%i\n"                                    \
                   " error: %s (%s)\n",                              \
              #fncall, __FILE__, __LINE__,                           \
              gasnet_ErrorName(_retval), gasnet_ErrorDesc(_retval)); \
      fflush(stderr);                                                \
      exit(_retval);                                                 \
    }                                                                \
  } while(0)

static const char *_testname;
static const char *_testusage;
void test_usage(void) {
  if (!upcxx::rank_me()) {
      fprintf(stderr, "%s %s\n", _testname, GASNETT_CONFIG_STRING);
      fprintf(stderr, "Usage: %s %s\n", _testname, _testusage);
      fflush(NULL);
  }
  upcxx::barrier();
  exit(1);
}
static void test_init(const char *testname, const char *usagestr, int argc, const char * const *argv) {
  _testusage = usagestr;
  _testname = testname;
  for (int i = 0; i < argc; i++) { /* check for standard help option */
    if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"--h") ||
        !strcmp(argv[i],"-help") || !strcmp(argv[i],"--help")) test_usage();
  } 
}

static size_t param_SZ = 0;

static gex_EP_t          myep;
static gex_TM_t          myteam;

intrank_t myrank = 0;
intrank_t numrank = 0;

#define alignup_ptr(a,b) ((void *)(((((uintptr_t)(a))+(b)-1)/(b))*(b)))

gex_RankInfo_t *nbrhdinfo = NULL;
gex_Rank_t nbrhdsize; // size of the neighborhood, or 1 for PSHM-only

static int64_t param_Z = 0;
static long param_B = 0;
static long param_N = 0;

static int in_segment = 1;

static enum {
  TEST_POLL_NEXT,
  TEST_POLL_RETRY,
  TEST_POLL_LAZY,
  TEST_POLL_ALWAYS
} poll_mode;

static char *local_addr;
static std::vector<long> remain;

static gasnett_atomic_t expect = gasnett_atomic_init(0);

#define hidx_expect_dec_handler 200

void expect_dec_handler(gex_Token_t token, void *buf, size_t nbytes) {
  // decrement the counter
  gasnett_atomic_decrement(&expect, 0);
}

void doFPAM(gex_Flags_t imm_flag);
void doNPAM(gex_Flags_t imm_flag);
void doRPC(gex_Flags_t imm_flag);
void doRPC_FF(gex_Flags_t imm_flag);

int main(int argc, char **argv) {
  gex_AM_Entry_t htable[] = { 
    { hidx_expect_dec_handler, (gex_AM_Fn_t)expect_dec_handler, GEX_FLAG_AM_REQREP|GEX_FLAG_AM_MEDLONG, 
      /*nargs=*/0, /*cdata=*/0, /*name=*/"expect_dec_handler" },
  };

  upcxx::init();
  myteam = upcxx::backend::gasnet::handle_of(upcxx::world());
  myep = gex_TM_QueryEP(myteam);

  myrank = upcxx::rank_me();
  numrank = upcxx::rank_n();

  int enable_given = 0;
  int enable_fpam  = 0;
  int enable_npam  = 0;
  int enable_rpc   = 0;
  int enable_rpc_ff= 0;

  int force_mixed  = 0;

  int help = 0;
  int arg = 1;
  while (argc > arg) {
    if (!strcmp(argv[arg], "-z")) {
      ++arg;
      if (argc > arg) { param_Z = atol(argv[arg]); arg++; }
      else help = 1;
      param_Z = MAX(param_Z, 0);
    } else if (!strcmp(argv[arg], "-b")) {
      ++arg;
      if (argc > arg) { param_B = atol(argv[arg]); arg++; }
      else help = 1;
    } else if (!strcmp(argv[arg], "-m")) {
      ++arg;
      enable_fpam = enable_given = 1;
    } else if (!strcmp(argv[arg], "-n")) {
      ++arg;
      enable_npam = enable_given = 1;
    } else if (!strcmp(argv[arg], "-r")) {
      ++arg;
      enable_rpc = enable_given = 1;
    } else if (!strcmp(argv[arg], "-f")) {
      ++arg;
      enable_rpc_ff = enable_given = 1;
    } else if (!strcmp(argv[arg], "-mixed")) {
      ++arg;
      force_mixed = 1;
    } else if (!strcmp(argv[arg], "-in")) {
      ++arg;
      in_segment = 1;
    } else if (!strcmp(argv[arg], "-out")) {
      ++arg;
      in_segment = 0;
    } else if (!strcmp(argv[arg], "-poll-next")) {
      ++arg;
      poll_mode = TEST_POLL_NEXT;
    } else if (!strcmp(argv[arg], "-poll-retry")) {
      ++arg;
      poll_mode = TEST_POLL_RETRY;
    } else if (!strcmp(argv[arg], "-poll-lazy")) {
      ++arg;
      poll_mode = TEST_POLL_LAZY;
    } else if (!strcmp(argv[arg], "-poll-always")) {
      ++arg;
      poll_mode = TEST_POLL_ALWAYS;
    } else if (argv[arg][0] == '-') {
      help = 1;
      ++arg;
    } else break;
  }

  if (!enable_given) {
    enable_fpam = enable_npam = enable_rpc = enable_rpc_ff = 1;
  }

  if (argc > arg) { param_N = atol(argv[arg]); ++arg; }
  if (!param_N) param_N = 10000;

  if (argc > arg) { param_SZ = atoi(argv[arg]); ++arg; }
  if (!param_SZ) { param_SZ = 1024; }
  if (enable_fpam || enable_npam) {
    param_SZ = MIN(param_SZ, gex_AM_LUBRequestMedium());
  }

  if (!param_Z) param_Z = 500;

  if (!param_B) param_B = param_N; // what UPC would call indefinite layout

  GASNET_Safe(gex_EP_RegisterHandlers(myep, htable, sizeof(htable)/sizeof(gex_AM_Entry_t)));

  test_init("imm_dynamic", "[options] (msgcnt) (msgsz)\n"
             "  Active rank 0 injects msgcnt operations of size msgsz to each passive peer.\n"
             "  Note that msgsz will be reduced if RequestMedium is\n"
             "  to be timed and msgsz would exceed the respective LUBRequest limit.\n"
             "  Options:\n"
             "  -in / -out\n"
             "        Selects whether the initiator's buffer is in the GASNet segment\n"
             "        or not (default is 'in').\n"
             "  -m / -n / -r / -f\n"
             "        Respectively enable timing of the AM FPAM, AM NPAM, RPC and RPC_FF ops.\n"
             "        The default is to test all operations.\n"
             "  -mixed\n"
             "        By default this test excludes passive ranks that are reachable\n"
             "        via shared-memory communication, unless that is *all* ranks.\n"
             "        This option overrides this behavior, allowing the test to use\n"
             "        a mix of shared-memory and network communication.  This test\n"
             "        does not exercise \"best practices\" for this configuration.\n"
             "  -z <interval_us>\n"
             "        Sets the minimum interval (in us) between poll calls by passive\n"
             "        ranks (default is 500).  Actual inter-poll delays may be longer.\n"
             "  -b <blocksz>\n"
             "        Sets block size of nominal communication schedule, where\n"
             "        \"nominal\" means the schedule applied in the absence of\n"
             "        back-pressure indication due to GEX_FLAG_IMMEDIATE.\n"
             "           0 (default) sends all to each rank before advancing\n"
             "           1 sends only one per rank before advancing\n"
             "           N sends N per rank before advancing\n"
             "  -poll-* options select the polling policy (default is -poll-next):\n"
             "    -poll-next   poll upon back pressure, advancing to the next peer\n"
             "    -poll-retry  poll upon back pressure, retrying the same peer once\n"
             "    -poll-lazy   advance to the next peer upon back pressure,\n"
             "                 but poll only between loops over peers\n"
             "    -poll-always advance to the next peer upon back pressure,\n"
             "                 but poll before every IMMEDIATE operation\n"
           , argc, argv);
  if (help || argc > arg) test_usage();

  if (numrank < 2) {
    print_test_skipped("This test requires two or more ranks");
    exit(0); /* exit 0 to prevent false negatives in test harness */
  }

  char *space = NULL;
  char nbrhd_warning[64] = "";
  gex_System_QueryNbrhdInfo(&nbrhdinfo, &nbrhdsize, NULL);
  if (!myrank) {
    if (nbrhdsize == 1) {
      // The passive ranks are all OUTSIDE our neighborhood
    } else if ((intrank_t)nbrhdsize == numrank) {
      // The passive ranks are all INSIDE our neighborhood
      nbrhdinfo = NULL; // suppress filtering
      nbrhdsize = 1;    // and correct reported passive rank count
    #if !UPCXX_NETWORK_SMP // would be "just noise" for smp-conduit
      strcpy(nbrhd_warning, "\n  WARNING: all ranks are reachable via shared-memory");
    #endif
    } else if (force_mixed) {
      // Mixed case: but commandline ask us not to omit ranks within our neighborhood
      snprintf(nbrhd_warning, sizeof(nbrhd_warning),
               "\n  WARNING: %d shared-memory rank%s allowed by -mixed option",
               (nbrhdsize-1), (nbrhdsize>2)?"s":"");
      nbrhdinfo = NULL; // suppress filtering
      nbrhdsize = 1;    // and correct reported passive rank count
    } else {
      // Mixed case: we will omit ranks within our neighborhood
      snprintf(nbrhd_warning, sizeof(nbrhd_warning),
               "\n  WARNING: %d shared-memory rank%s omitted",
               (nbrhdsize-1), (nbrhdsize>2)?"s":"");
    }

    if (in_segment) {
      local_addr = (char *)upcxx::allocate(param_SZ, GASNET_PAGESIZE);
    } else {
      space = new char[param_SZ + GASNET_PAGESIZE];
      local_addr = (char *)alignup_ptr(space, GASNET_PAGESIZE);
    }
    remain.reserve(numrank);
  } // rank 0

  if (!myrank) {
    char msg[1024];
    snprintf(msg, sizeof(msg), "Running imm_dynamic with %d ranks\n"
       "  active rank issues %ld operations per rank, each of length %ld\n"
       "  active rank's local addresses %sside the segment\n"
       "  nominal schedule issues operations in blocks of %ld\n"
       "  polling policy is \"%s\"\n"
       "  %d passive rank%s sleep (at least) %" PRIi64 "us between polls\n"
       "  operations timed:%s%s%s%s"
       "%s",
       numrank,
       param_N, (long)param_SZ,
       (in_segment ? "in" : "out"),
       param_B,
       (poll_mode==TEST_POLL_NEXT   ? "next" :
       (poll_mode==TEST_POLL_RETRY  ? "retry" :
       (poll_mode==TEST_POLL_ALWAYS ? "always" :
                                      "lazy"))),
       (numrank-nbrhdsize), (numrank-nbrhdsize>1)?"s":"", param_Z,
       enable_fpam?" FPAM":"", 
       enable_npam?" NPAM":"", 
       enable_rpc_ff?" RPC_FF":"", 
       enable_rpc?" RPC":"", 
       nbrhd_warning
      );
    say("") << msg;
  }

  if (enable_fpam) {
    doFPAM(0);
    doFPAM(GEX_FLAG_IMMEDIATE);
  }

  if (enable_npam) {
    doNPAM(0);
    doNPAM(GEX_FLAG_IMMEDIATE);
  }

  if (enable_rpc_ff) {
    doRPC_FF(0);
    doRPC_FF(GEX_FLAG_IMMEDIATE);
  }

  if (enable_rpc) {
    doRPC(0);
    doRPC(GEX_FLAG_IMMEDIATE);
  }

  if (!myrank) {
    delete [] space;
  }

  upcxx::barrier();
  if (!upcxx::rank_me()) say("") << "SUCCESS";

  upcxx::finalize();
  return 0;
}

void sleepy_barrier(uint64_t interval_ns) {
  upcxx::future<> f = upcxx::barrier_async();
  do {
    gasnett_nsleep(interval_ns);
    upcxx::progress();
  } while (!f.is_ready());
}

void passive(void) {
  uint64_t interval_ns = 1000 * param_Z;
  sleepy_barrier(interval_ns); // start barrier (passive)
  do {
    gasnett_nsleep(interval_ns);
    upcxx::progress();
  } while (gasnett_atomic_read(&expect, 0)); // await AM arrivals
  sleepy_barrier(interval_ns); // end barrier (passive)
}

#define ACTIVE(OPERATION, POLL) do {                    \
  int done;                                             \
  do {                                                  \
    done = 1;                                           \
    for (intrank_t r = 1; r < numrank; ++r) {          \
      long count, limit = MIN(param_B, remain[r]);      \
      for (count = 0; count < limit; ++count) {         \
        int did_retry = 0;                              \
        if ((poll_mode == TEST_POLL_ALWAYS) && imm_flag)\
          { POLL; }                                     \
        retry:                                          \
        if ( OPERATION(r) ) { /* got IMM backpressure */   \
          switch (poll_mode) {                          \
            case TEST_POLL_RETRY:                       \
              if (!did_retry) {                         \
                { POLL; }                               \
                did_retry = 1;                          \
                goto retry;                             \
              }                                         \
              break;                                    \
            case TEST_POLL_NEXT:                        \
              { POLL; }                                 \
              break;                                    \
            case TEST_POLL_LAZY: break;                 \
            case TEST_POLL_ALWAYS: break;               \
            default: gasnett_unreachable();             \
          }                                             \
          break; /* end injection to this peer */       \
        }                                               \
      }                                                 \
      remain[r] -= count;                               \
      done &= !remain[r];                               \
    } /* end loop over ranks */                         \
    if ((poll_mode == TEST_POLL_LAZY) && imm_flag)      \
       { POLL; }                                        \
  } while (!done);                                      \
} while (0)

void init_remain(int isAMtest) {
  assert(myrank == 0);
  for (intrank_t r = 1; r < numrank; ++r) remain[r] = param_N;
  if (nbrhdinfo) { // Optionally exclude PSHM-peers by zeroing their remain[]
    for (gex_Rank_t i = 0; i < nbrhdsize; ++i) remain[nbrhdinfo[i].gex_jobrank] = 0;
  }
  if (isAMtest) {
    gasnett_atomic_set(&expect, 0, 0);
    upcxx::promise<> p;
    for (intrank_t r = 1; r < numrank; ++r) {
      if (remain[r]) { // tell each passive rank what to expect
        gasnett_atomic_val_t val = (gasnett_atomic_val_t)remain[r];
        upcxx::rpc(r, operation_cx::as_promise(p), [=]() { 
          gasnett_atomic_set(&expect, val, 0);
        });
      }
    }
    p.finalize().wait(); // await acknowledgment
  }
  upcxx::barrier(); // start barrier (active)
}

void report(const char *name, gex_Flags_t imm_flag, double elapsed, double *prev) {
  char delta[32];
  if (*prev > 0.0) {
    snprintf(delta, sizeof(delta), ", %+.2f%% improvement", 100.*((*prev) - elapsed)/(*prev));
  } else {
    delta[0] = '\0';
  }
  int tgts = (numrank-nbrhdsize);
  double tgt_bw = ((double)param_N * param_SZ) / (elapsed * 1024 * 1024);
  double agg_bw = tgt_bw * tgts;
  printf("%-8s %3i tgt IMM=%c completed in %7.3f seconds (%7.3f MiB/s per-tgt, %7.3f MiB/s agg)%s\n",
         name, tgts, imm_flag?'Y':'N', elapsed, tgt_bw, agg_bw, delta);
  *prev = elapsed;
}

void doFPAM(gex_Flags_t imm_flag) {
  if (myrank) {
    passive();
  } else {
    init_remain(1);
    auto inject = [=](intrank_t r) {
      return gex_AM_RequestMedium0(myteam, r, hidx_expect_dec_handler, local_addr, param_SZ,
                                   GEX_EVENT_GROUP, imm_flag);
    };
    gasnett_tick_t start_ticks = gasnett_ticks_now();
    ACTIVE( inject, gasnet_AMPoll());
    gex_NBI_Wait(GEX_EC_AM,0);
    gasnett_tick_t end_ticks = gasnett_ticks_now();
    double elapsed = 1e-9 * gasnett_ticks_to_ns(end_ticks - start_ticks);

    static double prev;
    report("FPAM:", imm_flag, elapsed, &prev);

    upcxx::barrier(); // end barrier (active)
  }
}


void doNPAM(gex_Flags_t imm_flag) {
  if (myrank) {
    passive();
  } else {
    init_remain(1);
    auto inject = [=](intrank_t r) {
      gex_AM_SrcDesc_t sd = gex_AM_PrepareRequestMedium(
                                myteam, r, /*gex buf*/nullptr, param_SZ, param_SZ,
                                /*lc_opt*/nullptr, imm_flag, 0);
      if (sd == GEX_AM_SRCDESC_NO_OP) return 1;
      memcpy(gex_AM_SrcDescAddr(sd), local_addr, param_SZ);
      #if GASNET_SUPPORTS_AM_COMMIT_V2 && GASNET_SUPPORTS_AM_CANCEL
        int result = gex_AM_CommitRequestMedium0_v2(sd, hidx_expect_dec_handler, param_SZ, imm_flag);
        if (result) {
          gex_AM_CancelRequestMedium(sd, 0);
          return 1;
        }
      #else
        gex_AM_CommitRequestMedium0(sd, hidx_expect_dec_handler, param_SZ);
      #endif
      return 0;
    };
    gasnett_tick_t start_ticks = gasnett_ticks_now();
    ACTIVE( inject, gasnet_AMPoll());
    gasnett_tick_t end_ticks = gasnett_ticks_now();
    double elapsed = 1e-9 * gasnett_ticks_to_ns(end_ticks - start_ticks);

    static double prev;
    report("NPAM:", imm_flag, elapsed, &prev);

    upcxx::barrier(); // end barrier (active)
  }
}


void doRPC_FF(gex_Flags_t imm_flag) {
  if (myrank) {
    passive();
  } else {
    init_remain(1);
    auto pay_view = upcxx::make_view(local_addr, local_addr+param_SZ);
    auto inject = [=](intrank_t r) {
      if (imm_flag) {
        try { 
          upcxx::experimental::rpc_ff_immediate(r,
                   [](upcxx::view<char> const &) { 
                     gasnett_atomic_decrement(&expect, 0); 
                   }, pay_view);
        } catch (upcxx::experimental::network_busy &exn) {
          return 1;
        }
      } else {
        upcxx::rpc_ff(r,
                   [](upcxx::view<char> const &) { 
                     gasnett_atomic_decrement(&expect, 0); 
                   }, pay_view);
      }
      return 0;
    };
    gasnett_tick_t start_ticks = gasnett_ticks_now();
    ACTIVE( inject, upcxx::progress() );
    gasnett_tick_t end_ticks = gasnett_ticks_now();
    double elapsed = 1e-9 * gasnett_ticks_to_ns(end_ticks - start_ticks);

    static double prev;
    report("RPC_FF:", imm_flag, elapsed, &prev);

    upcxx::barrier(); // end barrier (active)
  }
}

void doRPC(gex_Flags_t imm_flag) {
  if (myrank) {
    passive();
  } else {
    init_remain(1);
    upcxx::promise<> p;
    auto pay_view = upcxx::make_view(local_addr, local_addr+param_SZ);
    auto inject = [=](intrank_t r) {
      if (imm_flag) {
        try { 
          upcxx::experimental::rpc_immediate(r, operation_cx::as_promise(p), 
                   [](upcxx::view<char> const &) { 
                     gasnett_atomic_decrement(&expect, 0); 
                   }, pay_view);
        } catch (upcxx::experimental::network_busy &exn) {
          return 1;
        }
      } else {
        upcxx::rpc(r, operation_cx::as_promise(p), 
                   [](upcxx::view<char> const &) { 
                     gasnett_atomic_decrement(&expect, 0); 
                   }, pay_view);
      }
      return 0;
    };
    gasnett_tick_t start_ticks = gasnett_ticks_now();
    ACTIVE( inject, upcxx::progress() );
    p.finalize().wait();
    gasnett_tick_t end_ticks = gasnett_ticks_now();
    double elapsed = 1e-9 * gasnett_ticks_to_ns(end_ticks - start_ticks);

    static double prev;
    report("RPC:", imm_flag, elapsed, &prev);

    upcxx::barrier(); // end barrier (active)
  }
}

