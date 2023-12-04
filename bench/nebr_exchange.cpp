/* 
 * UPC++ benchmark: Neighbor exchange (contiguous data)
 * 
 * This benchmark emulates a halo exchange with probabilistically near neighbors.
 * At startup each rank randomly picks a number of neighbors from a normal
 * distribution around that rank. Then the whole world does as many collective
 * buffer exchanges as possible until the time runs out. A measurement is 
 * performed for each combination of neighbor number (nebrs) and buffer size
 * (sizes) given in the parameters along with the data-movement mechanism (via).
 * 
 * Reported dimensions:
 * 
 *   nebrs: Number of neighbors per rank. Neighbors need not be distinct ranks.
 *     Rank A can be the neighbor of B multiple times.
 * 
 *   size: the size of the buffer in bytes.
 * 
 *   via = {rput|rdzv|long}: The mechanism used to move the data.
 *     rput: An rput with remote_cx::as_rpc to signal remote completion.
 *     rdzv: An rpc that remotely issues an rget.
 *     long: Issues gex_AM_RequestLong that runs a custom AM handler to signal
 *       remote completion. UPC++ runtime not involved.
 *
 * Reported Measurements:
 * 
 *   bw = Bandwidth in bytes/second. This is the measured quantity as dependent
 *     upon the reported dimensions. It is computed as the average bandwidth
 *     across all ranks.
 *
 * Environment variables:
 * 
 *   sizes (integer list, default="1024"): List of buffer sizes in bytes. List
 *     values are separated by spaces and optional comma (,).
 * 
 *   nebrs (integer list, default="10"): List of neighbor counts. List values
 *     are separated by spaces and optional comma (,).
 * 
 *   nebr_stdev (decimal, default=10): Standard deviation of normal distribution
 *     used to randomly pick neighbor rank numbers.
 * 
 *   wait_secs (decimal, default=1): Number of seconds to run a given measurement.
 */

#include <upcxx/upcxx.hpp>

#include "common/operator_new.hpp"
#include "common/os_env.hpp"
#include "common/report.hpp"
#include "common/timer.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

using upcxx::intrank_t;
using upcxx::global_ptr;

using namespace std;
using namespace bench;

vector<size_t> buf_sizes;
vector<int> nebr_nums;
double nebr_stdev;
double wait_secs;

struct nebr_t {
  intrank_t rank;
  global_ptr<char> remote_buf;
};

struct mesh_t {
  size_t buf_size;
  global_ptr<char> local_buf;
  int in_nebr_n;
  std::vector<nebr_t> out_nebrs;
};

mesh_t make_mesh_local(size_t buf_size, int nebr_n) {
  mesh_t ans;
  ans.buf_size = buf_size;
  ans.local_buf = upcxx::new_array<char>(buf_size);
  
  std::seed_seq seq = {upcxx::rank_me()};
  std::mt19937_64 rng(seq);
  std::normal_distribution<> dist(0, nebr_stdev);
  
  int rn = upcxx::rank_n();
  
  for(int neb=0; neb < nebr_n; neb++) {
    int buddy;
    do {
      int offset = (int)std::ceil(dist(rng));
      buddy = upcxx::rank_me() + offset;
      buddy = (buddy%rn + rn) % rn;
    }
    while(buddy == upcxx::rank_me());
    
    ans.out_nebrs.push_back({buddy, nullptr});
  }
  
  return ans;
}

void obtain_mesh_remote(mesh_t &m) {
  m.in_nebr_n = 0;
  upcxx::dist_object<mesh_t*> dm(&m);
  
  int unacks = (int)m.out_nebrs.size();
    
  for(int i=0; i < (int)m.out_nebrs.size(); i++) {
    
    upcxx::rpc(m.out_nebrs[i].rank,
      [](upcxx::dist_object<mesh_t*> &dm) {
        (*dm)->in_nebr_n += 1;
        return (*dm)->local_buf;
      },
      dm
    ).then([&,i](global_ptr<char> buf) {
      m.out_nebrs[i].remote_buf = buf;
      unacks -= 1;
    });
  }
  
  while(0 != unacks)
    upcxx::progress();
  upcxx::barrier();
}

void destroy_mesh(mesh_t &m) {
  upcxx::delete_array(m.local_buf);
}

////////////////////////////////////////////////////////////////////////////////

// returns bytes sent
std::size_t exchange_via_rput(mesh_t &m) {
  upcxx::dist_object<int> acks_in(0);
  int acks_out = 0;
  char *local_buf = m.local_buf.local();
  const size_t buf_size = m.buf_size;
  
  for(int i=0; i < (int)m.out_nebrs.size(); i++) {
    upcxx::rput(
      local_buf,
      m.out_nebrs[i].remote_buf,
      buf_size,
      upcxx::source_cx::as_future() |
      upcxx::remote_cx::as_rpc(
        [](upcxx::dist_object<int> &acks_in) {
          *acks_in += 1;
        },
        acks_in
      )
    ).then([&acks_out]() {
      acks_out += 1;
    });
  }
  
  //unsigned spins = 0;
  
  while(
      acks_out != (int)m.out_nebrs.size() ||
      *acks_in != (int)m.in_nebr_n
    ) {
    upcxx::progress();
    
    //if(++spins == 1<<26)
    //  UPCXX_ASSERT_ALWAYS(0, "acks_out="<<m.out_nebrs.size()-acks_out<<" acks_in="<<m.in_nebr_n-*acks_in);
  }
  
  return buf_size*m.out_nebrs.size();
}

////////////////////////////////////////////////////////////////////////////////

// returns bytes sent
std::size_t exchange_via_rdzv(mesh_t &m) {
  upcxx::dist_object<int> acks_in(0);
  int acks_out = 0;
  const size_t buf_size = m.buf_size;
  
  for(int i=0; i < (int)m.out_nebrs.size(); i++) {
    global_ptr<char> src = m.local_buf;
    global_ptr<char> dst = m.out_nebrs[i].remote_buf;
    
    upcxx::rpc(
      m.out_nebrs[i].rank,
      [=](upcxx::dist_object<int> &acks_in) {
        auto *p_acks_in = &acks_in;
        return upcxx::rget(src, dst.local(), buf_size)
          .then([=]() {
            **p_acks_in += 1;
          });
      },
      acks_in
    ).then(
      [&]() { acks_out += 1; }
    );
  }
  
  while(
      acks_out != (int)m.out_nebrs.size() ||
      *acks_in != (int)m.in_nebr_n
    ) {
    upcxx::progress();
  }
  
  return buf_size*m.out_nebrs.size();
}


////////////////////////////////////////////////////////////////////////////////
// AMLong based exchange zone

#include <upcxx/upcxx_internal.hpp>

#include <gasnet.h>

unordered_map<unsigned,int> amlong_acks_in;
unsigned amlong_epoch = 0;
gex_HSL_t amlong_lock = GEX_HSL_INITIALIZER;

constexpr int amlong_handler_id = GEX_AM_INDEX_BASE + 99;

void amlong_handler(gex_Token_t, void *buf, size_t buf_size, gex_AM_Arg_t epoch_from) {
  gex_HSL_Lock(&amlong_lock);
  
  amlong_acks_in[epoch_from] += 1;
  
  gex_HSL_Unlock(&amlong_lock);
}

void setup_exchange_via_amlong() {
  gex_EP_t ep = gex_TM_QueryEP(upcxx::backend::gasnet::handle_of(upcxx::world()));
  
  gex_AM_Entry_t am_table[1] = {
    {amlong_handler_id, (gex_AM_Fn_t)amlong_handler, GEX_FLAG_AM_LONG | GEX_FLAG_AM_REQUEST, 1, nullptr, nullptr}
  };
  
  (void)gex_EP_RegisterHandlers(ep, am_table, sizeof(am_table)/sizeof(am_table[0]));
  
  upcxx::barrier();
}

std::size_t exchange_via_amlong(mesh_t &m) {
  const size_t buf_size = m.buf_size;
  char *const local_buf = m.local_buf.local();
  
  std::list<gex_Event_t> lcs;
  
  for(int i=0; i < (int)m.out_nebrs.size(); i++) {
    gex_Event_t lc;
    
    gex_AM_RequestLong1(
      upcxx::backend::gasnet::handle_of(upcxx::world()),
      m.out_nebrs[i].rank,
      amlong_handler_id,
      local_buf,
      buf_size,
      m.out_nebrs[i].remote_buf.raw_internal(upcxx::detail::internal_only{}),
      &lc,
      0,
      amlong_epoch
    );
    
    if(0 != gex_Event_Test(lc))
      lcs.push_back(lc);
  }
  
  do {
    gasnet_AMPoll();
    UPCXX_ASSERT_ALWAYS(amlong_acks_in[amlong_epoch] <= (int)m.in_nebr_n);
  }
  while(amlong_acks_in[amlong_epoch] != (int)m.in_nebr_n);
  
  amlong_acks_in.erase(amlong_epoch);
  amlong_epoch += 1;
  amlong_epoch &= 0x0fffffff; // stay within range of gex_AM_Arg_t (signed).
  
  while(!lcs.empty()) {
    gasnet_AMPoll();
    while(!lcs.empty() && 0 == gex_Event_Test(lcs.front()))
      lcs.pop_front();
  }
  
  return buf_size*m.out_nebrs.size();
}

////////////////////////////////////////////////////////////////////////////////

struct measure {
  double secs;
  uint64_t bytes;
  
  static measure plus(measure a, measure b) {
    return {a.secs + b.secs, a.bytes + b.bytes};
  }
};

// Run a collective function `fn` for duration of `wait_secs`. Write measurement
// into `table` at row `r`. Collective function must return num bytes moved by
// this rank that call.
template<typename Row, typename Fn>
void run_trial(Row r, std::unordered_map<Row,measure> &table, Fn fn_bytes_moved) {
  if(upcxx::rank_me() == 0)
    std::cout<<"Measuring "<<r<<std::endl<<std::flush;

  upcxx::barrier(); // synchronize ranks
  
  double total_secs = 0.0;
  uint64_t bytes = 0;
  int64_t rounds = 100;
  
  while(true) {
    bool last = rounds < 100;
    rounds = last ? 200 : rounds;
    
    //if(upcxx::rank_me()==0)
    //  std::cout<<"rounds="<<rounds<<std::endl<<std::flush;
    
    timer tim;
    for(int64_t i=0; i < rounds; i++)
      bytes += fn_bytes_moved();
    double dt = tim.elapsed();
    
    total_secs += dt;
    if(last) break;
    
    // estimate number of rounds needed to spend 75% of our remaining time budget.
    rounds = (double(rounds)/dt) * .75*(wait_secs - total_secs);
    
    // take min over ranks
    rounds = upcxx::reduce_all(rounds, upcxx::op_fast_min).wait();
  }

  table[r] = {total_secs, bytes};
}

auto make_row = [](int nebr_n, size_t buf_size, const char *via) {
  return column("nebrs", nebr_n)
       & column("size", buf_size)
       & column("via", via);
};

int main() {
  upcxx::init();

  if (upcxx::rank_n() < 2) {
    print_test_skipped("test requires two or more ranks");
    upcxx::finalize();
    return 0;
  }

  buf_sizes = os_env<vector<size_t>>("sizes", vector<size_t>({1<<10}));
  nebr_nums = os_env<vector<int>>("nebrs", std::vector<int>({10}));
  nebr_stdev = os_env<double>("nebr_stdev", 10.0);
  wait_secs = os_env<double>("wait_secs", 1.0);

  setup_exchange_via_amlong();
  
  std::unordered_map<decltype(make_row(0,0,0)), measure> table;
  
  for(int nebr_n: nebr_nums) {
    for(size_t buf_size: buf_sizes) {
      mesh_t m = make_mesh_local(buf_size, nebr_n);
      obtain_mesh_remote(m);
      
      if(1) run_trial(
        make_row(nebr_n, buf_size, "rput"), table,
        [&]() { return exchange_via_rput(m); }
      );
      if(1) run_trial(
        make_row(nebr_n, buf_size, "rdzv"), table,
        [&]() { return exchange_via_rdzv(m); }
      );
      if(1) run_trial(
        make_row(nebr_n, buf_size, "long"), table,
        [&]() { return exchange_via_amlong(m); }
      );
      
      destroy_mesh(m);
    }
  }
  
  for(int nebr_n: nebr_nums) {
    for(size_t buf_size: buf_sizes) {
      for(const char *via: {"rput","rdzv","long"}) {
        auto r = make_row(nebr_n, buf_size, via);
        table[r] = upcxx::reduce_all(table[r], measure::plus).wait();
      }
    }
  }
  
  if(upcxx::rank_me() == 0) {
    report rep(__FILE__);
    
    for(int nebr_n: nebr_nums) {
      for(size_t buf_size: buf_sizes) {
        for(const char *via: {"rput","rdzv","long"}) {
          auto r = make_row(nebr_n, buf_size, via);
          UPCXX_ASSERT_ALWAYS(0 != table.count(r));
          
          measure m = table[r];
          rep.emit({"bw"},
            column("bw", m.bytes/m.secs) &
            opnew_row() &
            r
          );
        }
        
        rep.blank();
      }
    }
  }
  
  if (!upcxx::rank_me())  std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
  return 0;
}
