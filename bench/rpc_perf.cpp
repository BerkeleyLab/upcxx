// This micro-benchmark measures the performance of selected RPC protocols across payload size
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
#include "common/timer.hpp"

using namespace upcxx;

int nranks, self, peer;
uint64_t iters;
uint64_t maxsz;
uint64_t windowsz;
const int szscale = 2;

char *payload;

uint64_t recv_cnt, expect_cnt, ack_cnt; // used to track rpc_ff arrival

template<bool flood, bool ff, bool fullduplex=false> 
void run_test(bool iamprimary, const char *desc) {
  if (!upcxx::rank_me()) {
    std::cout << "*** Testing " << desc 
    #if USE_WINDOW
              << (flood ? std::string(", window size ") + std::to_string(windowsz) : "")
    #endif
              << std::endl;
    std::cout << "     "
              << std::right << std::setw(10) << "Payload sz" 
              << " "
              << std::right << std::setw(14) << "Total time" 
              << "    "
              << (flood ? "Payload Bandwidth" : "Round-trip Latency")
              << std::endl;
  }
  upcxx::barrier();

  #if USE_WINDOW
    // Precompute windowing parameters for flood tests, used to ensure each sender has
    // no more than windowsz RPCs in-flight at any time.  We break each window into a 
    // two-stage pipeline, waiting on the first half after injecting the second half.
    static const uint64_t batch_sz = windowsz / 2;
    static const uint64_t batch_cnt = iters / batch_sz; 
    assert(batch_cnt * batch_sz == iters);
  #endif

  for (uint64_t sz = 1; sz < szscale*maxsz; sz *= szscale) {
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
      if (ff) {   // one-way rpc_ff()
      #if !USE_WINDOW
        if (fullduplex || iamprimary) { // sender
          for (uint64_t i = 0; i < iters; i++) {
            rpc_ff(peer, [](view<char> const &){ recv_cnt++; }, pay_view);
          }
        }
        if (fullduplex || !iamprimary) { // await arrivals
          while (recv_cnt < iters) progress();
          assert(recv_cnt == iters);
        }
      #else // USE_WINDOW
        for (uint64_t b = 0; b < batch_cnt; b++) {
          if (fullduplex || iamprimary) { // sender
            for (uint64_t i = 0; i < batch_sz; i++) {
              rpc_ff(peer, 
                [](view<char> const &){ 
                  // rpc_ff has no semantic return messages
                  // insert a synthetic acknowledgment once per batch,
                  // to throttle the number in flight
                  if (++recv_cnt == expect_cnt) {
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
      } else {    // round-trip rpc() ping-then-ack
        if (fullduplex || iamprimary) { // sender
        #if !USE_WINDOW
          promise <> p;
          for (uint64_t i = 0; i < iters; i++) {
            rpc(peer, operation_cx::as_promise(p),
                [](view<char> const &) -> void { return; }, pay_view);
          }
          p.finalize().wait(); // await all acknowledgments
        #else // USE_WINDOW
          promise<> p1,p2;
          for (uint64_t b = 0; b < batch_cnt; b++) {
            for (uint64_t i = 0; i < batch_sz; i++) {
              rpc(peer, operation_cx::as_promise(p1),
                  [](view<char> const &) -> void { return; }, pay_view);
            }
            p2.finalize().wait(); // await acknowledgments for previous half-window
            p2 = p1;
            p1 = promise<>(); // reset for next
          }
          p2.finalize().wait(); // await acknowledgments for final half-window
        #endif
        }
      }
    } else {  // one-at-a-time ping-pong test
      assert(!fullduplex);
      if (ff) {   // one-way rpc_ff()
        if (iamprimary) { // send the data
          for (uint64_t i = 0; i < iters; i++) {
            rpc_ff(peer, [](view<char> const &){ recv_cnt++; }, pay_view);
            while (recv_cnt <= i) progress(); // await acknowledgment
          }
        } else { // passive receiver just sends empty acknowledgments
          for (uint64_t i = 0; i < iters; i++) {
            while (recv_cnt <= i) progress(); // await arrival
            rpc_ff(peer, [](){ recv_cnt++; }); // send ack
          }
        }
        assert(recv_cnt == iters);
      } else {   // round-trip rpc() ping-then-ack
        if (iamprimary) {
          for (uint64_t i = 0; i < iters; i++) {
            auto f = rpc(peer, [](view<char> const &) -> void { return; }, pay_view);
            f.wait(); // await acknowledgment
          }
        }
      }
    }

    barrier();  // ensure global completion of test

    if (iamprimary) {
      double total_time = start.elapsed();
      std::stringstream ss;
      ss << std::setw(3) << self << ": " 
         << std::setw(10) << sz << " " 
         << std::setw(14) << total_time << " s ";
      if (flood)  {
        double data_sent = sz * iters;
        if (fullduplex) data_sent *= 2;
        double bw = data_sent / (1024*1024) / total_time;
        ss << std::setw(10) << bw << " MiB/s\n";
      } else {
        double lat = (total_time / iters) * 1e6;
        ss << std::setw(10) << lat << " us\n";
      }

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

// Usage: a.out (iterations) <window_size> <max_payload>
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
    PARSE_ARG(maxsz, 4*1024*1024);
  }

  nranks = upcxx::rank_n();
  self = upcxx::rank_me();
  // cross-machine symmetric pairing
  if (nranks % 2 == 1 && self == nranks - 1) peer = self;
  else if (self < nranks/2) peer = self + nranks/2;
  else peer = self - nranks/2;
  std::stringstream ss;
  char hname[255] = {};
  gethostname(hname, sizeof(hname));

  ss << self << "/" << nranks << " : " << hname << " : peer=" << peer << "\n";
  std::cout << ss.str() << std::flush;

  payload = new char[maxsz];
  bool iamprimary = self <= peer;

  upcxx::barrier();

  if (!upcxx::rank_me())
    std::cout << "Running RPC performance test with " << iters <<" iterations..." << std::endl;

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

