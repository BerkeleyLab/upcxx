#include <upcxx/upcxx.hpp>
#include "util.hpp"

using namespace std;

using gp_u32 = upcxx::global_ptr<uint32_t>;

constexpr int peer_n = 10;
gp_u32 my_buf;
gp_u32 peer_bufs[peer_n];

int my_slot_recv[2][peer_n]={}, peer_slot_sent[2][peer_n]={};

int dest_rank(int64_t i, int me = upcxx::rank_me()) {
  return (me + i*i + 1) % upcxx::rank_n();
}
int src_rank(int64_t i, int me = upcxx::rank_me()) {
  int r = (me - i*i - 1) % upcxx::rank_n();
  if(r < 0) r += upcxx::rank_n();
  return r;
}

// computes a size distributed exponentially so that it has good
// chance of hitting all the wire protocols
int calc_size(int round, int peer, int origin = upcxx::rank_me()) {
  uint64_t x = round + 1;
  x *= 0x12345678d34db33f;
  x ^= x >> 25;
  x += origin;
  x *= 0xdeadbeef01234567;
  x ^= x >> 35;
  x += peer;
  x *= 0x1234567844332211;
  x ^= x >> 25;
  // x is now a 64-bit hash of (round,peer,origin)
  int sh = x % 18; // pick shift amount from [0,18) (use low bits)
  x >>= 64-18; // x is now 18-bit value (use high bits)
  x >>= sh; // drop some of the 18 bits
  return int(x);
}

uint32_t* src_slot(int round, int i) {
  return my_buf.local() + (i<<18) + ((peer_n*(round%2))<<18);
}

gp_u32 dest_slot(int round, int i) {
  return peer_bufs[i] + (i<<18) + ((peer_n*(1-round%2))<<18);
}

// instantiated with either upcxx::{source|operation}_cx
// to test different am long protocls (with and without reply)
template<typename SourceCx/*upcxx::{source|operation}_cx*/>
void do_put(int round, int peer, int *outgoing) {
  int r1 = round + 1;
  uint32_t *src = src_slot(r1, peer);
  gp_u32 dest = dest_slot(r1, peer);
  int size = calc_size(r1, peer);
  //say()<<"put size="<<size;
  
  std::vector<uint32_t> check(size);
  for(int i=0; i < size; i++)
    check[i] = src[i];
  
  *outgoing += 1;
  upcxx::rput(src, dest, size,
    SourceCx::as_lpc(upcxx::current_persona(),
      [=]() {
        *outgoing -= 1;
        upcxx::rpc_ff(src_rank(peer), [=]() {
          UPCXX_ASSERT_ALWAYS(peer_slot_sent[r1%2][peer] < r1);
          peer_slot_sent[r1%2][peer] = r1;
        });
      }
    ) |
    upcxx::remote_cx::as_rpc(
      [r1,peer,size,dest](std::vector<uint32_t> const &check1) {
        UPCXX_ASSERT_ALWAYS(my_slot_recv[r1%2][peer] < r1);
        my_slot_recv[r1%2][peer] = r1;
        
        UPCXX_ASSERT_ALWAYS(size == (int)check1.size());
        uint32_t *p = dest.local();
        for(int i=0; i < size; i++)
          UPCXX_ASSERT_ALWAYS(check1[i] == p[i]);
      },
      std::move(check)
    )
  );
}

int main() {
  upcxx::init(); {
    print_test_header();
    my_buf = upcxx::new_array<uint32_t>(2*peer_n<<18);

    for(int i=0; i < 2*peer_n<<18; i++)
      my_buf.local()[i] = upcxx::rank_me()*16 + i%16;
    
    upcxx::barrier();
    
    { // gather my_buf's into peer_bufs
      int need = peer_n;
      for(int i=0; i < peer_n; i++) {
        upcxx::rpc(
          dest_rank(i), []() { return my_buf; }
        ).then(
          [i,&need](gp_u32 got) {
            peer_bufs[i] = got;
            need -= 1;
          }
        );
      }
      
      while(need != 0)
        upcxx::progress();
    }

    upcxx::barrier();

    int outgoing = 0;

    constexpr int round_n = 3;
    for(int round=0; round < round_n;) {

      for(int peer=0; peer < peer_n; peer++) {
        while(my_slot_recv[round%2][peer] < round || peer_slot_sent[round%2][peer] < round)
          upcxx::progress();

        if((round + peer) % 2)
          do_put<upcxx::source_cx>(round, peer, &outgoing);
        else
          do_put<upcxx::operation_cx>(round, peer, &outgoing);
      }
      round += 1;
    }

    while(true) {
      bool done = outgoing == 0;
      for(int peer=0; peer < peer_n; peer++) {
        done &= my_slot_recv[round_n%2][peer] == round_n;
        done &= my_slot_recv[(round_n-1)%2][peer] == round_n-1;
      }
      if(done) break;
      
      upcxx::progress();
    }
    upcxx::barrier();
  }

  print_test_success();
  upcxx::finalize();
}
