#include <iostream>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <set>
#include <unordered_map>
#include <random>

using namespace std;

upcxx::future<> test_team(const upcxx::team &tm) {
  upcxx::future<> all_done = upcxx::make_future();
  int me = tm.rank_me();
  int n = tm.rank_n();
  
  // iterate a large but logarithmic number of the "n" ranks
  for(int i=n; i != 0;) {
    // do a broadcast from rank "i"
    i = 31*i/32;
    
    all_done = upcxx::when_all(all_done,
      upcxx::broadcast(me, i, tm)
        .then([=](int got) {
          UPCXX_ASSERT_ALWAYS(got == i);
        })
      );
    all_done = upcxx::when_all(all_done,
      upcxx::broadcast<uint16_t>(uint16_t(0xbeefu*me), i, tm)
        .then([=](uint16_t got) {
          UPCXX_ASSERT_ALWAYS(got == uint16_t(0xbeefu*i));
        })
      );
    all_done = upcxx::when_all(all_done,
      upcxx::experimental::broadcast_nontrivial<uint16_t>(uint16_t(0xbeefu*me), i, tm)
        .then([=](uint16_t got) {
          UPCXX_ASSERT_ALWAYS(got == uint16_t(0xbeefu*i));
        })
      );
  }
  
  std::multiset<uint32_t> bag1;
  std::unordered_map<uint16_t, uint16_t> bag2; // histograms 16 bit "hashes" of bag1 elements
  uint32_t sum1 = 0; // sum of bag1 items
  {
    uint32_t tmp = 0xdeadbeefu*me;
    for(int j=0; j < 32000; j++) {
      tmp = 0x1234567u*tmp + 0xbeefu;
      tmp ^= tmp >> 11;
      bag1.insert(tmp);
      bag2[uint16_t(tmp)] += 1;
      sum1 += tmp;
    }
  }
  
  // broadcast a bunch of bag1's and have everyone validate against what they think
  // the broadcasting rank should have computed for its bag1.
  for(int i=n; i != 0;) {
    i = 31*i/32;
    all_done = upcxx::when_all(all_done,
      upcxx::experimental::broadcast_nontrivial(bag1, i, tm)
        .then([=](std::multiset<uint32_t> got) {
          uint32_t tmp = 0xdeadbeefu*i;
          for(int j=0; j < 32000; j++) {
            tmp = 0x1234567u*tmp + 0xbeefu;
            tmp ^= tmp >> 11;
            UPCXX_ASSERT_ALWAYS(got.count(tmp) != 0);
            got.erase(tmp);
          }
          UPCXX_ASSERT_ALWAYS(got.empty());
        })
      );
  }
  
  // reduce_all(+) sum1 twice as 16 bit and 32 bit and validate they match
  auto sum1_done = upcxx::reduce_all(sum1, upcxx::op_fast_add, tm);
  auto sum2_done = upcxx::reduce_all(uint16_t(sum1), [](uint16_t a, uint16_t b) { return a+b; }, tm);
  
  auto max1_done = upcxx::reduce_one(sum1, upcxx::op_fast_max, 0xbeef % tm.rank_n(), tm);
  auto max2_done = upcxx::experimental::reduce_one_nontrivial(sum1, (uint32_t const&(*)(uint32_t const&,uint32_t const&))std::max<uint32_t>, 0xbeef % tm.rank_n(), tm);
  
  auto min1_done = upcxx::reduce_all(sum1, upcxx::op_fast_min, tm);
  auto min2_done = upcxx::reduce_all(sum1, (uint32_t const&(*)(uint32_t const&,uint32_t const&))std::min<uint32_t>, tm); // std::min becomes a function pointer, which the impl does ensure to translate correctly
  
  auto and1_done = upcxx::reduce_all(sum1, upcxx::op_fast_bit_and, tm);
  auto and2_done = upcxx::reduce_all(bool(sum1 & 1), upcxx::op_fast_mul, tm);
  
  auto or1_done = upcxx::reduce_one(sum1, upcxx::op_fast_bit_or, 0xbeef % tm.rank_n(), tm);
  auto or2_done = upcxx::reduce_one(uint16_t(sum1), [](uint16_t a, uint16_t b) { return a|b; }, 0xbeef % tm.rank_n(), tm);
  
  // fails correctly with static_assert
  //auto bad = upcxx::reduce_one(std::make_pair(sum1,sum1), upcxx::op_fast_bit_or, 0xbeef % tm.rank_n(), tm);
  
  auto xor1_done = upcxx::reduce_all(sum1, upcxx::op_fast_bit_xor, tm);
  auto xor2_done = upcxx::reduce_all(uint8_t(sum1), [](uint8_t const &a, uint8_t b) { return a^b; }, tm);

  all_done = upcxx::when_all(all_done,
    upcxx::when_all(sum1_done, sum2_done,
                    max1_done, max2_done,
                    min1_done, min2_done,
                    and1_done, and2_done,
                    or1_done, or2_done,
                    xor1_done, xor2_done)
    .then([=,&tm](
        uint32_t sum1, uint16_t sum2,
        uint32_t max1, uint32_t max2,
        uint32_t min1, uint32_t min2,
        uint32_t and1, bool and2,
        uint32_t or1, uint16_t or2,
        uint32_t xor1, uint8_t xor2) {
      UPCXX_ASSERT_ALWAYS(uint16_t(sum1) == sum2);
      UPCXX_ASSERT_ALWAYS(min1 == min2);
      UPCXX_ASSERT_ALWAYS(bool(and1 & 1) == and2);
      UPCXX_ASSERT_ALWAYS(uint8_t(xor1) == xor2);
      if(tm.rank_me() == 0xbeef % tm.rank_n()) {
        UPCXX_ASSERT_ALWAYS(min1 <= max1);
        UPCXX_ASSERT_ALWAYS(max1 == max2);
        UPCXX_ASSERT_ALWAYS(uint16_t(or1) == or2);
      }
    })
  );

  // reduce_all(+) the histograms
  auto bag2_done = upcxx::experimental::reduce_all_nontrivial(bag2,
    #if 1
      [](std::unordered_map<uint16_t,uint16_t> a,
         std::unordered_map<uint16_t,uint16_t> const &b
        ) -> std::unordered_map<uint16_t,uint16_t> {
        for(auto xy: b)
          a[xy.first] += xy.second;
        return a;
      }
    #else
      [](std::unordered_map<uint16_t,uint16_t> a,
         std::unordered_map<uint16_t,uint16_t> b
        ) -> std::unordered_map<uint16_t,uint16_t> {
        for(auto xy=b.begin(); xy != b.end();) {
          a[xy->first] += xy->second;
          b.erase(xy++); // consume nodes of `b`
        }
        return a;
      }
    #endif
    ,tm
    );
  
  // validate that reduced(+) sum2 matches an alternative calculation based off
  // of the reduced histograms.
  all_done = upcxx::when_all(all_done,
      upcxx::when_all(sum2_done, bag2_done)
      .then([=](uint16_t sum2, std::unordered_map<uint16_t,uint16_t> const &bag2) {
        for(auto xy: bag2)
          sum2 -= xy.first * xy.second;
        UPCXX_ASSERT_ALWAYS(sum2 == 0);
      })
    );
  
  // throw in a barrier_async for API coverage.
  all_done = upcxx::when_all(all_done, upcxx::barrier_async(tm));
  all_done = upcxx::when_all(all_done, upcxx::barrier_async(tm));
  
  { // do a vector reduce
    const int n = 1000;
    double *src = new double[n];
    double *dst_one = new double[n];
    double *dst_all = new double[n];
    for(int i=0; i < n; i++) {
      src[i] = i + tm.rank_me();
      dst_one[i] = 666;
      dst_all[i] = 666;
    }
    
    auto vec1_done = upcxx::reduce_one(src, dst_one, n, upcxx::op_fast_add, 0, tm);
    auto vec2_done = upcxx::reduce_all(src, dst_all, n, std::plus<double>(), tm);
    
    all_done = upcxx::when_all(all_done,
        upcxx::when_all(vec1_done, vec2_done)
        .then([=,&tm]() {
          for(int i=0; i < n; i++) {
            double rn = tm.rank_n();
            if(tm.rank_me() == 0)
              UPCXX_ASSERT_ALWAYS(dst_one[i] == i*rn + (rn*rn - rn)/2);
            UPCXX_ASSERT_ALWAYS(dst_all[i] == i*rn + (rn*rn - rn)/2);
          }
          delete[] src;
          delete[] dst_one;
          delete[] dst_all;
        })
      );
  }
  
  return all_done;
}

int main() {
  upcxx::init();
  {
    print_test_header();
    
    std::mt19937_64 gen;
    gen.seed(upcxx::rank_me());
    auto rng = [&]() -> int {
      return (gen() % upcxx::rank_n()) + upcxx::rank_n();
    };
    
    upcxx::future<> all_done = upcxx::make_future();
    
    // world
    all_done = upcxx::when_all(all_done, test_team(upcxx::world()));
    
    // world split #1
    upcxx::team tm1 = upcxx::world().split(rng(), upcxx::rank_me());
    all_done = upcxx::when_all(all_done, test_team(tm1));
    
    // world split #2
    upcxx::team tm2 = upcxx::world().split(rng(), -1*upcxx::rank_me());
    all_done = upcxx::when_all(all_done, test_team(tm2));
    
    // world split #2 split #1
    upcxx::team tm3 = tm2.split(rng(), (unsigned)upcxx::rank_me()*0xdeadbeefu);
    all_done = upcxx::when_all(all_done, test_team(tm3));
   
    upcxx::team const &tm3c = tm3;
    upcxx::team tm4 = tm3c.split(upcxx::team::color_none, 0);
    tm4.destroy(); // tm4 is invalid: optional/no-op
    upcxx::team tm5 = tm3c.split(upcxx::team::color_none, 0);
    
    all_done.wait();
    
    tm3.destroy();
    tm2.destroy();
    tm1.destroy();
    
    print_test_success();
  }
  
  upcxx::finalize();
  return 0;
}
