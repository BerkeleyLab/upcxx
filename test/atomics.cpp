#include <iostream>
#include <vector>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

using namespace std;

using upcxx::team;
using upcxx::global_ptr;
using upcxx::atomic_op;

std::vector<atomic_op> access_ops = { 
       atomic_op::load, atomic_op::store, 
       atomic_op::compare_exchange
};
std::vector<atomic_op> arithmetic_ops = { 
       atomic_op::add, atomic_op::fetch_add,
       atomic_op::sub, atomic_op::fetch_sub,          
       atomic_op::mul, atomic_op::fetch_mul,
       atomic_op::min, atomic_op::fetch_min,
       atomic_op::max, atomic_op::fetch_max,
       atomic_op::inc, atomic_op::fetch_inc,
       atomic_op::dec, atomic_op::fetch_dec,
};
std::vector<atomic_op> bitwise_ops = { 
       atomic_op::bit_and, atomic_op::fetch_bit_and,
       atomic_op::bit_or, atomic_op::fetch_bit_or,
       atomic_op::bit_xor, atomic_op::fetch_bit_xor,
};
std::vector<atomic_op> all_ops;
std::vector<atomic_op> fp_ops;

constexpr int ITERS = 10;

// let's all hit the same rank
upcxx::intrank_t target_rank(const team &tm) {
  return 0xbeef % tm.rank_n();
}

template <typename T>
void test_fetch_add(const team &tm, global_ptr<T> target_counter,
                    const upcxx::atomic_domain<T> &dom) {
  T expected_val = static_cast<T>(tm.rank_n() * ITERS);
  if (tm.rank_me() == 0) {
    cout << "Test fetch_add: atomics, expect value " << expected_val << endl;
    
    // always use atomics to access or modify counter - alternative API
    dom.store(target_counter, (T)0, memory_order_relaxed).wait();
  }
  upcxx::barrier(tm);
  for (int i = 0; i < ITERS; i++) {
    // increment the target
    switch (i%4) { 
      case 0: {
        // This should cause an assert failure
        //auto prev = dom.fetch_sub(target_counter, (T)1, memory_order_relaxed).wait();
        int prev;
        if (i % 2) {
          auto prevT = dom.fetch_add(target_counter, (T)1, memory_order_relaxed).wait();
          prev = int(prevT);
        } else {
          T prevT;
          dom.fetch_add(target_counter, (T)1, &prevT, memory_order_relaxed).wait();
          prev = int(prevT);
        }
        UPCXX_ASSERT_ALWAYS(prev >= 0 && prev < tm.rank_n() * ITERS, 
              "atomic::fetch_add result out of range");
        break;
      }
      case 1: {
        int prev;
        if (i % 2) {
          upcxx::promise<T> p;
          dom.fetch_add(target_counter, (T)1, memory_order_relaxed, upcxx::operation_cx::as_promise(p));
          auto prevT = p.finalize().wait();
          prev = int(prevT);
        } else {
          upcxx::promise<> p;
          T prevT;
          dom.fetch_add(target_counter, (T)1, &prevT, memory_order_relaxed, upcxx::operation_cx::as_promise(p));
          p.finalize().wait();
          prev = int(prevT);
        }
        UPCXX_ASSERT_ALWAYS(prev >= 0 && prev < tm.rank_n() * ITERS, 
              "atomic::fetch_add result out of range");
        break;
      }
      case 2: {
        upcxx::future<> f = dom.add(target_counter, (T)1, memory_order_relaxed);
        f.wait();
        break;
      }
      case 3: {
        upcxx::promise<> p; 
        dom.add(target_counter, (T)1, memory_order_relaxed, upcxx::operation_cx::as_promise(p));
        p.finalize().wait();
        break;
      }
    }
  }
  
  upcxx::barrier(tm);
  
  if (tm.rank_me() == target_rank(tm)) {
    T val = dom.load(target_counter, memory_order_relaxed).wait();
    cout << "Final value is " << val << endl;
    UPCXX_ASSERT_ALWAYS(val == expected_val, 
              "incorrect final value for the counter");
  }
  
  upcxx::barrier(tm);
}

template <typename T>
void test_put_get(const team &tm, global_ptr<T> target_counter, const upcxx::atomic_domain<T> &dom) {
  if (tm.rank_me() == 0) {
    cout << "Test puts and gets: expect a random rank number" << endl;
    // always use atomics to access or modify counter
    dom.store(target_counter, (T)0, memory_order_relaxed).wait();
  }
  upcxx::barrier(tm);
  
  if (tm.rank_me() == target_rank(tm)) {
    UPCXX_ASSERT_ALWAYS(target_counter.where() == upcxx::rank_me());
    UPCXX_ASSERT_ALWAYS(dom.load(target_counter, memory_order_relaxed).wait() == 0);
  }
  upcxx::barrier(tm);
  
  for (int i = 0; i < ITERS * 10; i++) {
    int v;
    if (i % 2) {
      v = int(dom.load(target_counter, memory_order_relaxed).wait());
    } else {
      T result;
      dom.load(target_counter, &result, memory_order_relaxed).wait();
      v = int(result);
    }
    UPCXX_ASSERT_ALWAYS(v >=0 && v < tm.rank_n(), "atomic_get out of range: " << v);
    dom.store(target_counter, (T)tm.rank_me(), memory_order_relaxed).wait();
  }
  
  upcxx::barrier(tm);
  
  if (tm.rank_me() == target_rank(tm)) {
    int v = int(dom.load(target_counter, memory_order_relaxed).wait());
    cout << "Final value is " << v << endl;
    UPCXX_ASSERT_ALWAYS( v >= 0 && v < tm.rank_n(),
        "atomic put and get test result out of range: got="<<v<<" range=[0,"<<tm.rank_n()<<")"
      );
  }
  
  upcxx::barrier(tm);
}

#define CHECK_ATOMIC_VAL(v, V) UPCXX_ASSERT_ALWAYS(v == V, "expected " << V << ", got " << v);

template <typename T>
void test_all_ops(const team &tm, global_ptr<T> target_counter, const upcxx::atomic_domain<T> &dom) {
  if (tm.rank_me() == 0) {
    dom.store(target_counter, (T)42, memory_order_relaxed).wait();
    T v = dom.load(target_counter, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);
    v = dom.load(const_pointer_cast<const T>(target_counter), memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);
    dom.inc(target_counter, memory_order_relaxed).wait();
    v = dom.fetch_inc(target_counter, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 43);
    dom.dec(target_counter, memory_order_relaxed).wait();
    v = dom.fetch_dec(target_counter, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 43);
    dom.add(target_counter, 7, memory_order_relaxed).wait();
    v = dom.fetch_add(target_counter, 5, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 49);
    dom.sub(target_counter, 3, memory_order_relaxed).wait();
    v = dom.fetch_sub(target_counter, 2, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 51);
    v = dom.compare_exchange(target_counter, 49, 42, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 49);
    v = dom.compare_exchange(target_counter, 0, 3, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);

    dom.load(target_counter, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);
    dom.load(const_pointer_cast<const T>(target_counter), &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);
    dom.inc(target_counter, memory_order_relaxed).wait();
    dom.fetch_inc(target_counter, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 43);
    dom.dec(target_counter, memory_order_relaxed).wait();
    dom.fetch_dec(target_counter, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 43);
    dom.add(target_counter, 7, memory_order_relaxed).wait();
    dom.fetch_add(target_counter, 5, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 49);
    dom.sub(target_counter, 3, memory_order_relaxed).wait();
    dom.fetch_sub(target_counter, 2, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 51);
    dom.compare_exchange(target_counter, 49, 42, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 49);
    dom.compare_exchange(target_counter, 0, 3, &v, memory_order_relaxed).wait();
    CHECK_ATOMIC_VAL(v, 42);
  }
  upcxx::barrier(tm);
}

template <typename T>
void test_team_t(const upcxx::team &tm, std::vector<atomic_op> ops) {
  upcxx::atomic_domain<T> ad_all( ops, tm);

  // get the global pointer to the target counter
  global_ptr<T> target_counter =
    upcxx::broadcast(upcxx::allocate<T>(1), target_rank(tm), tm).wait();

  test_all_ops(tm, target_counter, ad_all);
  test_fetch_add(tm, target_counter, ad_all);
  test_put_get(tm, target_counter, ad_all);
  ad_all.destroy();

  // NOTE: target_counter is *deliberately* leaked here, to avoid the possibility
  // of technically breaking atomicity semantics. See spec 13.1-2
}

template<typename T, size_t sz>
struct _test_type {
  void operator()(const char *TN, const upcxx::team &tm, std::vector<atomic_op> const &ops) {
    if (!upcxx::rank_me()) 
      std::cout << " --- SKIPPING atomic_domain<" << TN << "> --- (" << (sz*8) << "-bit)" << std::endl;
    upcxx::barrier();
  }
};
template<typename T>
struct _test_type<T,4> {
  void operator()(const char *TN, const upcxx::team &tm, std::vector<atomic_op> const &ops) {
    if (!upcxx::rank_me()) 
      std::cout << " --- Testing atomic_domain<" << TN << "> --- (32-bit)" << std::endl;
    upcxx::barrier();
    test_team_t<T>(tm,ops);
  }
};
template<typename T>
struct _test_type<T,8> {
  void operator()(const char *TN, const upcxx::team &tm, std::vector<atomic_op> const &ops) {
    if (!upcxx::rank_me()) 
      std::cout << " --- Testing atomic_domain<" << TN << "> --- (64-bit)" << std::endl;
    upcxx::barrier();
    test_team_t<T>(tm,ops);
  }
};
#define TEST_TYPE(T,ops) _test_type<T, sizeof(T)>()(#T, tm, ops)


void test_team(const upcxx::team &tm) {

  upcxx::atomic_domain<int32_t> ad_i({atomic_op::store, atomic_op::fetch_add}, tm);

  // uncomment to evaluate error checking
  //upcxx::atomic_domain<const int> ad_cint({upcxx::atomic_op::load});
  // will fail with an error message about no move/copy assignment or copy constructor
  //upcxx::atomic_domain<int32_t> ad = std::move(ad_i);
  //upcxx::atomic_domain<int32_t> ad = ad_i;
  //upcxx::atomic_domain<int32_t> ad2 = ad_i; 
  // this will fail with an error message about an unsupported domain
  //ad_i.load(upcxx::allocate<int32_t>(1), memory_order_relaxed).wait();
  // this will fail with a null ptr message
  //ad_i.store(nullptr, (unsigned long)0, memory_order_relaxed).wait();
  // fails with illegal op
  //upcxx::atomic_domain<double> ad_d2({atomic_op::fetch_bit_and,atomic_op::bit_or}, tm);
  //ad_d2.fetch_bit_and(upcxx::allocate<double>(1), (double)0, memory_order_relaxed);
  
  ad_i.destroy();

  // test operation for all supported types

  // Fixed-width integers
  TEST_TYPE(int32_t,  all_ops);
  TEST_TYPE(uint32_t, all_ops);
  TEST_TYPE(int64_t,  all_ops);
  TEST_TYPE(uint64_t, all_ops);

  // Floating-point
  TEST_TYPE(float,    fp_ops);
  TEST_TYPE(double,   fp_ops);

  // General integer types
  TEST_TYPE(short,              all_ops);
  TEST_TYPE(unsigned short,     all_ops);
  TEST_TYPE(int,                all_ops);
  TEST_TYPE(unsigned int,       all_ops);
  TEST_TYPE(long,               all_ops);
  TEST_TYPE(unsigned long,      all_ops);
  TEST_TYPE(long long,          all_ops);
  TEST_TYPE(unsigned long long, all_ops);
  TEST_TYPE(ptrdiff_t,          all_ops);
  TEST_TYPE(size_t,             all_ops);
  TEST_TYPE(intptr_t,           all_ops);
  TEST_TYPE(uintptr_t,          all_ops);

  TEST_TYPE(intmax_t,  all_ops);
  TEST_TYPE(uintmax_t, all_ops);

  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout << "PASSED" << std::endl;
}

int main(int argc, char **argv) {
  upcxx::init();
  print_test_header();

  fp_ops = arithmetic_ops;
  fp_ops.insert(fp_ops.end(), access_ops.begin(), access_ops.end());
  all_ops = fp_ops;
  all_ops.insert(all_ops.end(), bitwise_ops.begin(), bitwise_ops.end());

  {

    #define MSG(desc) \
      if (!upcxx::rank_me()) \
        std::cout << "****************** " << desc << " ******************" << std::endl; \
      upcxx::barrier();
    MSG("Testing atomics on world team");
    test_team(upcxx::world());
    
    MSG("Testing atomics on split world team");
    team tm0 = upcxx::world().split(upcxx::rank_me() % 3, 0);
    test_team(tm0);
    tm0.destroy();
    
    MSG("Testing atomics on local team");
    test_team(upcxx::local_team());
    
    MSG("Testing atomics on split local team");
    team tm1 = upcxx::local_team().split(upcxx::world().rank_me() % 2, 0);
    test_team(tm1);
    tm1.destroy();
  }
  print_test_success();
  upcxx::finalize();
  return 0;
}
