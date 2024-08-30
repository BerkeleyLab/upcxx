// This micro-benchmark measures the performance of immediate-mode RPC across payload size
// Reported sizes are a view-based user-level payload, and somewhat undercount the actual size on-the-wire
//

#ifndef USE_WINDOW
#define USE_WINDOW 1
#endif

#include <upcxx/upcxx.hpp>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <assert.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include "common/timer.hpp"

using namespace upcxx;
using upcxx::experimental::say;
using upcxx::experimental::network_busy;
using upcxx::experimental::rpc_immediate;
using upcxx::experimental::rpc_ff_immediate;

int nranks, self, peer;
team peer_team;
uint64_t iters;
uint64_t maxsz;
uint64_t windowsz;
const int szscale = 2;

uint64_t retry_limit;
uint64_t retry_sum = 0;
uint64_t retry_max = 0;

using ms = std::chrono::duration<double, std::milli>;
ms pause_time{0};
inline void pause_sleep(void) {
  std::this_thread::sleep_for(pause_time);
}
inline void wait_with_pause(upcxx::future<> f) {
  while (!f.is_ready()) {
    pause_sleep();
    progress();
  } 
}

char *payload;

uint64_t recv_cnt, expect_cnt, ack_cnt; // used to track rpc_ff arrival

template<typename Fn, typename ...Args>
void inject_ff(int rank, Fn &&fn, Args &&...args) {
  uint64_t retry = 0;
  while(true) {
    if (retry >= retry_limit) {
      // blocking send
      rpc_ff(rank, fn, args...);
      return;
    }
    try { 
      rpc_ff_immediate(rank, fn, args...);
      return;
    } catch (network_busy &exn) {
      static bool first = [=]() {
      #if SHOW_EXN
        if (!self) say() << "First rpc_ff_immediate exception:\n" << exn.what();
      #endif
        return false;
      }();
      retry++;
      retry_sum++;
      retry_max = std::max(retry, retry_max);
    }
  }
}

template<typename Fn, typename Cx, typename ...Args>
auto inject(int rank, Cx &&completions, Fn &&fn, Args &&...args) ->
           decltype(upcxx::rpc(rank, completions, fn, args...)) {
  uint64_t retry = 0;
  while(true) {
    if (retry >= retry_limit) {
      // blocking send
      return rpc(rank, completions, fn, args...);
    }
    try { 
      return rpc_immediate(rank, completions, fn, args...);
    } catch (network_busy &exn) {
      static bool first = [=]() {
      #if SHOW_EXN
        if (!self) say() << "First rpc_immediate exception:\n" << exn.what();
      #endif
        return false;
      }();
      retry++;
      retry_sum++;
      retry_max = std::max(retry, retry_max);
    }
  }
}

template<bool flood, bool ff, bool fullduplex=false> 
void run_test(bool iamprimary, const char *desc) {

  if (!upcxx::rank_me()) {
    std::cout << "\n*** Testing " << desc 
    #if USE_WINDOW
              << (flood ? std::string(", window size ") + std::to_string(windowsz) : "")
    #endif
              << ", pause=" << pause_time.count() << "ms"
              << std::endl;
    std::cout << "     "
              << std::right << std::setw(10) << "Payload sz" 
              << " "
              << std::right << std::setw(14) << "Total time" 
              << "    "
              << (flood ? "Payload Bandwidth " : "Round-trip Latency")
              << "  Retry Rate  Max Retry"
              << std::endl;
  }

  #if USE_WINDOW
    // Precompute windowing parameters for flood tests, used to ensure each sender has
    // no more than windowsz RPCs in-flight at any time.  We break each window into a 
    // two-stage pipeline, waiting on the first half after injecting the second half.
    static const uint64_t batch_sz = windowsz / 2;
    static const uint64_t batch_cnt = iters / batch_sz; 
    assert(batch_cnt * batch_sz == iters);
  #endif

  recv_cnt = 0; 
  upcxx::barrier();
  {
  #if !USE_WINDOW
    uint64_t warmup_iters = iters;
  #else
    uint64_t warmup_iters = batch_sz;
  #endif
    // Pay some warm-up costs
    // The tests below are intended to measure steady-state behavior, so first we
    // try to induce any one-time startup costs (e.g. dynamic buffer allocation) 
    
    if (fullduplex || iamprimary) { // sender
      promise <> p;
      auto pay_view = make_view(payload, payload+1024); // a largish eager message
      for (uint64_t i = 0; i < warmup_iters; i++) {
        if (ff) {
          inject_ff(peer, [](view<char> const &){ recv_cnt++; }, pay_view);
        } else {
          inject(peer, operation_cx::as_promise(p), [](view<char> const &) {}, pay_view);
        }
      }
      pause_sleep();
      p.finalize().wait();
    }
    if (fullduplex || !iamprimary) { // am recvr
      pause_sleep();
      if (ff) {
        while (recv_cnt < warmup_iters) progress();
        assert(recv_cnt == warmup_iters);
      }
    }
  }
  upcxx::barrier();

  for (uint64_t sz = 1; sz < szscale*maxsz; sz *= szscale) {
    retry_sum = 0;
    retry_max = 0;
    if (sz > maxsz) sz = maxsz;
    auto pay_view = make_view(payload, payload+sz); // the data payload
    recv_cnt = 0; 
    ack_cnt = 0; 
    #if USE_WINDOW
      expect_cnt = batch_sz; 
    #endif

    barrier(); 
    bench::timer start; // start time

    if (flood) { // many-at-a-time flood test
      // ---------------------------------------------
      // one-way rpc_ff() flood
      if (ff) { 
      #if !USE_WINDOW
        if (fullduplex || iamprimary) { // sender
          for (uint64_t i = 0; i < iters; i++) {
            inject_ff(peer, [](view<char> const &){ recv_cnt++; }, pay_view);
          }
        }
        if (fullduplex || !iamprimary) { // await arrivals
          while (recv_cnt < iters) { pause_sleep(); progress(); }
          assert(recv_cnt == iters);
        }
      #else // USE_WINDOW
        for (uint64_t b = 0; b < batch_cnt; b++) {
          if (fullduplex || iamprimary) { // sender
            for (uint64_t i = 0; i < batch_sz; i++) {
              inject_ff(peer, [](view<char> const &){ 
                  // rpc_ff has no semantic return messages
                  // insert a synthetic acknowledgment once per batch,
                  // to throttle the number in flight
                  if (++recv_cnt == expect_cnt) {
                    pause_sleep();
                    expect_cnt += batch_sz;
                    rpc_ff(peer, []() { ack_cnt++; });
                  }
                }, pay_view);
            } // for i: batch send
            while (ack_cnt < b) progress(); // await ack for previous batch
          }
        }
        if (fullduplex || !iamprimary) { // am recvr
          while (recv_cnt < iters) progress();
          assert(recv_cnt == iters);
        }
      #endif
      } else {    
      // ---------------------------------------------
      // round-trip rpc() flood
        if (fullduplex || iamprimary) { // sender
        #if !USE_WINDOW
          promise <> p;
          for (uint64_t i = 0; i < iters; i++) {
            inject(peer, operation_cx::as_promise(p),
                [=](view<char> const &) -> void { 
                  if (!i) pause_sleep();
                  return; 
                }, pay_view);
          }
          wait_with_pause(p.finalize()); // await all acknowledgments
        #else // USE_WINDOW
          promise<> p1,p2;
          for (uint64_t b = 0; b < batch_cnt; b++) {
            for (uint64_t i = 0; i < batch_sz; i++) {
              inject(peer, operation_cx::as_promise(p1),
                  [=](view<char> const &) -> void { 
                    if (!i) pause_sleep();
                    return; 
                  }, pay_view);
            }
            wait_with_pause(p2.finalize()); // await acknowledgments for previous half-window
            p2 = p1;
            p1 = promise<>(); // reset for next
          }
          wait_with_pause(p2.finalize()); // await acknowledgments for final half-window
        #endif
        }
        wait_with_pause(upcxx::barrier_async());
        //pause_sleep();
      }
    } else {  // one-at-a-time ping-pong test
      assert(!fullduplex);
      // ---------------------------------------------
      // one-way rpc_ff() ping-pong
      if (ff) {   
        if (iamprimary) { // send the data
          for (uint64_t i = 0; i < iters; i++) {
            inject_ff(peer, [](view<char> const &){ recv_cnt++; }, pay_view);
            while (recv_cnt <= i) progress(); // await acknowledgment
          }
        } else { // passive receiver just sends empty acknowledgments
          for (uint64_t i = 0; i < iters; i++) {
            while (recv_cnt <= i) { // await arrival
              if (!recv_cnt) pause_sleep(); 
              progress(); 
            }
            rpc_ff(peer, [](){ recv_cnt++; }); // send ack
          }
        }
        assert(recv_cnt == iters);
      } else {   
      // ---------------------------------------------
      // round-trip rpc() ping-then-ack
        if (iamprimary) {
          for (uint64_t i = 0; i < iters; i++) {
            auto f = inject(peer, [](view<char> const &) -> void { return; }, pay_view);
            f.wait(); // await acknowledgment
          }
        } else pause_sleep();
      }
    }

    barrier();  // ensure global completion of test

    retry_max = reduce_one(retry_max, op_fast_max, 0, peer_team).wait();
    retry_sum = reduce_one(retry_sum, op_fast_add, 0, peer_team).wait();
    if (iamprimary) {
      double total_time = start.elapsed();
      double retry_avg = double(retry_sum) / (fullduplex?2*iters:iters);
      std::stringstream ss;
      ss << std::setw(3) << self << ": " 
         << std::setw(10) << sz << " " 
         << std::setw(14) << total_time << " s ";
      if (flood)  {
        double data_sent = sz * iters;
        if (fullduplex) data_sent *= 2;
        double bw = data_sent / (1024*1024) / total_time;
        ss << std::setw(10) << bw <<  " MiB/s";
      } else {
        double lat = (total_time / iters) * 1e6;
        ss << std::setw(10) << lat << " us   ";
      }
      ss << "   ";
      ss << std::setw(10) << retry_avg*100 << "%";
      ss << " ";
      ss << std::setw(10) << retry_max;
      ss << "\n";

      std::cout << ss.str() << std::flush;
    }

    // try to ensure drainage of rendezvous free-behind messages, 
    // to prevent perturbing subsequent iterations
    progress();
    rpc(peer,[]{}).wait();
    progress();

    barrier();

  } // sz
} // run_test

// Usage: a.out (iterations) <window_size> <max_payload> <pause_ms> <retry_limit>
// Compiling with -DUSE_WINDOW=0 disables windowing (and window argument)
int main(int argc, char **argv) {
  upcxx::init();
  
  { // argument parsing/validation
    int c = 1;
    #define PARSE_ARG(var, dflt) do { \
      if (argc > c) var = atol(argv[c++]); \
      if (var < 1) var = dflt; \
    } while (0)

    PARSE_ARG(iters, 1000);
  #if USE_WINDOW
    PARSE_ARG(windowsz, 100);
    if (windowsz > iters/2) windowsz = iters/2;
    if (windowsz % 2)       windowsz++; // windowsz must be even
    if (iters % windowsz)   iters += windowsz-(iters % windowsz); // and evenly divide iters
    assert(windowsz % 2 == 0);
    assert(windowsz >= 2 && windowsz <= iters/2);
    assert(iters % windowsz == 0);
  #endif
    PARSE_ARG(maxsz, 1*1024*1024);
    if (argc > c) pause_time = ms{atof(argv[c++])};
    PARSE_ARG(retry_limit, 100);
  }

  nranks = upcxx::rank_n();
  self = upcxx::rank_me();
  // cross-machine symmetric pairing
  if (nranks % 2 == 1 && self == nranks - 1) peer = self;
  else if (self < nranks/2) peer = self + nranks/2;
  else peer = self - nranks/2;
  peer_team = world().split(std::min(peer,self),0);
  std::stringstream ss;
  char hname[255] = {};
  gethostname(hname, sizeof(hname));

  ss << self << "/" << nranks << " : " << hname << " : peer=" << peer << "\n";
  std::cout << ss.str() << std::flush;

  payload = new char[maxsz];
  bool iamprimary = self <= peer;

  upcxx::barrier();

  if (!upcxx::rank_me())
    std::cout << "Running RPC IMMEDIATE performance test with " << iters <<" iterations, "
              << "retry_limit="<<retry_limit<<" ..." << std::endl;

  upcxx::barrier();
  run_test<false, false>(iamprimary, "rpc round-trip latency (one-at-a-time)");

  upcxx::barrier();
  run_test<false, true>(iamprimary, "rpc_ff round-trip latency (one-at-a-time)");

  upcxx::barrier();
  run_test<true, false>(iamprimary, "rpc uni-directional flood bandwidth (many-at-a-time)");

  upcxx::barrier();
  run_test<true, true>(iamprimary, "rpc_ff uni-directional flood bandwidth (many-at-a-time)");

  if (nranks > 1) {
    upcxx::barrier();
    run_test<true, false, true>(iamprimary, "rpc bi-directional flood bandwidth (many-at-a-time)");

    upcxx::barrier();
    run_test<true, true, true>(iamprimary, "rpc_ff bi-directional flood bandwidth (many-at-a-time)");
  }

  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout << "SUCCESS" << std::endl;

  delete [] payload;

  upcxx::finalize();

  return 0;
}

