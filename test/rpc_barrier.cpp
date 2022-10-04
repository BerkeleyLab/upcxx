#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <deque>

#include <upcxx/upcxx.hpp>

#include "util.hpp"

using namespace upcxx;
using namespace std;

// Barrier state bitmasks.
uint64_t state_bits[2] = {0, 0};
size_t payload_limit;

struct barrier_action {
  int epoch;
  intrank_t round;
  std::deque<std::deque<char>> extra;

  UPCXX_SERIALIZED_FIELDS(epoch, round, extra)

  barrier_action() = default;
  barrier_action(int epoch, intrank_t round):
    epoch{epoch},
    round{round},
    extra( 
        (0x9e3779b9u*uint32_t(100*epoch + round)) % payload_limit,
        std::deque<char>(1,'x')
    ) {
  }
  
  void operator()() {
    for(auto x: extra)
      UPCXX_ASSERT_ALWAYS(x[0] == 'x');
    
    uint64_t bit = uint64_t(1)<<round;
    UPCXX_ASSERT_ALWAYS(0 == (state_bits[epoch & 1] & bit));
    state_bits[epoch & 1] |= bit;
  }
};

// This is a correct but NON-PERFORMANT implementation of barrier using RPC
// Applications should call upcxx::barrier() or upcxx::barrier_async() instead.
// This code is a test of RPC correctness and not tuned for performance
// (for example, the messages deliberately carry large/complex payloads for
//  the purpose of validating the correctness of payload delivery).
void rpc_barrier() {
  intrank_t rank_n = upcxx::rank_n();
  intrank_t rank_me = upcxx::rank_me();
  
  static unsigned epoch_bump = 0;
  int epoch = epoch_bump++;
  
  intrank_t round = 0;
  
  while(1<<round < rank_n) {
    uint64_t bit = uint64_t(1)<<round;
    
    intrank_t peer = rank_me + bit;
    if(peer >= rank_n) peer -= rank_n;
    
    #if 1
      // Use random message sizes
      future<> src_cx = upcxx::rpc_ff(peer, source_cx::as_future(), barrier_action{epoch, round});
    #else
      // The more concise lambda way, none of the barrier_action code
      // is necessary.
      future<> src_cx = upcxx::rpc_ff(peer, source_cx::as_future(), [=]() {
        state_bits[epoch & 1] |= bit;
      });
    #endif

    bool src_cx_waiting = true;
    src_cx.then([&](){ src_cx_waiting = false; });
    
    while(src_cx_waiting || 0 == (state_bits[epoch & 1] & bit))
      upcxx::progress();
    
    round += 1;
  }
  
  state_bits[epoch & 1] = 0;
}

bool got_right = false, got_left = false;

int main(int argc, char **argv) {
  upcxx::init();

  print_test_header();
  
  intrank_t rank_me = upcxx::rank_me();
  intrank_t rank_n = upcxx::rank_n();

  payload_limit = 8192;
  if (argc > 1) payload_limit = atoi(argv[1]);
  else if (os_env<bool>("UPCXX_OVERSUBSCRIBED",false)) payload_limit = 256;
  
  for(int i=0; i < 10; i++) {
    rpc_barrier();
    
    if(i % rank_n == rank_me) {
      say() << "Barrier "<<i;
    }
  }
  
  intrank_t right = (rank_me + 1) % rank_n;
  intrank_t left = (rank_me - 1 + rank_n) % rank_n;
  
  {
    future<> src;
    future<int> op;
    std::tie(src, op) = upcxx::rpc(
      right,
      source_cx::as_future() | operation_cx::as_future(),
      []() {
        say() << "from left";
        got_left = true;
        return 0xbeef;
      }
    );
    
    when_all(src,op).wait();
    UPCXX_ASSERT_ALWAYS(op.result() == 0xbeef, "rpc returned wrong value");
  }
  
  rpc_barrier();

  UPCXX_ASSERT_ALWAYS(got_left, "no left found before barrier");
  UPCXX_ASSERT_ALWAYS(!got_right, "right found before barrier");
  
  got_left = false;
  
  rpc_barrier();
  
  {
    future<int> fut = upcxx::rpc(left, [=]() {
      say() << "from right";
      got_right = true;
      return rank_me;
    });
    
    fut.wait();
    UPCXX_ASSERT_ALWAYS(fut.result() == rank_me, "rpc returned wrong value");
  }

  rpc_barrier();

  UPCXX_ASSERT_ALWAYS(got_right, "no right found after barrier");
  UPCXX_ASSERT_ALWAYS(!got_left, "left found after barrier");

  upcxx::barrier();
  
  print_test_success();

  upcxx::finalize();
  return 0;
}
