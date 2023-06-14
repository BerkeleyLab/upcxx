#include "util.hpp"

struct A {
  static int live_count;
  A() {
    ++live_count;
  }
  A(const A&) {
    ++live_count;
  }
  ~A() {
    --live_count;
  }
};

int A::live_count = 0;

template<typename SrcFutCxFn, typename OpFutCxFn,
         typename OpEmptyPromCxFn, typename OpIntPromCxFn,
         typename OpAPromCxFn>
void test(bool bypass, bool op_eager, bool src_eager,
          SrcFutCxFn src_fut_cx_fn, OpFutCxFn op_fut_cx_fn,
          OpEmptyPromCxFn empty_prom_fn, OpIntPromCxFn int_prom_fn,
          OpAPromCxFn a_prom_fn,
          upcxx::global_ptr<std::int64_t> gptr,
          upcxx::global_ptr<std::int64_t> aptr,
          upcxx::atomic_domain<std::int64_t> &ad) {
#define CHECK_READY(fut, eagerness) \
  ([=]() { \
    auto ready = fut.is_ready(); \
    UPCXX_ASSERT_ALWAYS((ready <= eagerness) && \
                        (ready >= (bypass && eagerness)), \
    "ready()="<<ready<<" eagerness="<<eagerness<<" bypass="<<bypass); \
  })()
#define CHECK_RESULT(fut, expected) \
  UPCXX_ASSERT_ALWAYS((fut.is_ready() ? fut.result() : fut.wait()) == expected)

  // reset values
  upcxx::rput(std::int64_t(0), gptr).wait();
  ad.store(aptr, std::int64_t(0), std::memory_order_relaxed).wait();

  // scalar rput/rget
  auto fut1 = upcxx::rput(std::int64_t(3), gptr, op_fut_cx_fn());
  CHECK_READY(fut1, op_eager);
  fut1.wait();

  auto fut2 = upcxx::rget(gptr, op_fut_cx_fn());
  CHECK_READY(fut2, op_eager);
  CHECK_RESULT(fut2, 3);

  upcxx::promise<> pro3;
  upcxx::rput(std::int64_t(-7), gptr, empty_prom_fn(pro3));
  CHECK_READY(pro3.finalize(), op_eager);
  pro3.get_future().wait();

  upcxx::promise<std::int64_t> pro4;
  upcxx::rget(gptr, int_prom_fn(pro4));
  CHECK_READY(pro4.finalize(), op_eager);
  CHECK_RESULT(pro4.get_future(), -7);

  // vector rput/rget
  std::int64_t val = 11;
  auto futs5 = upcxx::rput(&val, gptr, 1,
                           op_fut_cx_fn() | src_fut_cx_fn());
  CHECK_READY(std::get<0>(futs5), op_eager);
  CHECK_READY(std::get<1>(futs5), src_eager);
  std::get<1>(futs5).wait();
  val = 0; // clear val before reading back into it below
  std::get<0>(futs5).wait();

  auto fut6 = upcxx::rget(gptr, &val, 1, op_fut_cx_fn());
  CHECK_READY(fut6, op_eager);
  fut6.wait();
  UPCXX_ASSERT_ALWAYS(val == 11);

  { // atomics -- cannot assume synchronous completion
  bool bypass = false; // force checking to assume no bypass
  auto fut7 = ad.store(aptr, 3, std::memory_order_relaxed, op_fut_cx_fn());
  CHECK_READY(fut7, op_eager);
  fut7.wait();

  auto fut8 = ad.load(aptr, std::memory_order_relaxed, op_fut_cx_fn());
  CHECK_READY(fut8, op_eager);
  CHECK_RESULT(fut8, 3);
  } // atomics

  // check promise ref counting
  {
    upcxx::promise<A> pro5(2);
    pro5.fulfill_result(A{});
    UPCXX_ASSERT_ALWAYS(A::live_count);
    rput(std::int64_t(3), gptr, a_prom_fn(pro5));
    pro5.finalize().wait();
    UPCXX_ASSERT_ALWAYS(A::live_count);
  }
  UPCXX_ASSERT_ALWAYS(!A::live_count);

  #undef CHECK_READY
  #undef CHECK_RESULT
}

void test_all(bool bypass,
              upcxx::global_ptr<std::int64_t> gptr,
              upcxx::global_ptr<std::int64_t> aptr,
              upcxx::atomic_domain<std::int64_t> &ad) {
  test(bypass, !UPCXX_DEFER_COMPLETION, !UPCXX_DEFER_COMPLETION,
       upcxx::source_cx::as_future,
       upcxx::operation_cx::as_future,
       upcxx::operation_cx::as_promise<>,
       upcxx::operation_cx::as_promise<std::int64_t>,
       upcxx::operation_cx::as_promise<A>,
       gptr, aptr, ad);
  test(bypass, false, false,
       upcxx::source_cx::as_defer_future,
       upcxx::operation_cx::as_defer_future,
       upcxx::operation_cx::as_defer_promise<>,
       upcxx::operation_cx::as_defer_promise<std::int64_t>,
       upcxx::operation_cx::as_defer_promise<A>,
       gptr, aptr, ad);
  test(bypass, true, true,
       upcxx::source_cx::as_eager_future,
       upcxx::operation_cx::as_eager_future,
       upcxx::operation_cx::as_eager_promise<>,
       upcxx::operation_cx::as_eager_promise<std::int64_t>,
       upcxx::operation_cx::as_eager_promise<A>,
       gptr, aptr, ad);
}


int main() {
  upcxx::init();
  print_test_header();

  upcxx::global_ptr<std::int64_t> gptr = upcxx::new_<std::int64_t>(0);
  upcxx::global_ptr<std::int64_t> aptr = upcxx::new_<std::int64_t>(0);
  upcxx::atomic_domain<std::int64_t> ad({upcxx::atomic_op::load,
                                         upcxx::atomic_op::store});
  upcxx::dist_object<upcxx::global_ptr<std::int64_t>> all_gptrs{gptr};
  upcxx::dist_object<upcxx::global_ptr<std::int64_t>> all_aptrs{aptr};

  // loopback
  if (upcxx::rank_me() == 0)
    say() << "loopback test";
  test_all(true, gptr, aptr, ad);

  upcxx::barrier();

  // local peer
  if (upcxx::local_team().rank_n() > 1) {
    if (upcxx::rank_me() == 0)
      say() << "local peer test";
    upcxx::intrank_t peer =
      upcxx::local_team()[(upcxx::local_team().rank_me() + 1) %
                          upcxx::local_team().rank_n()];
    upcxx::global_ptr<std::int64_t> gptr_peer =
      all_gptrs.fetch(peer).wait();
    upcxx::global_ptr<std::int64_t> aptr_peer =
      all_aptrs.fetch(peer).wait();
    test_all(true, gptr_peer, aptr_peer, ad);
  }

  upcxx::barrier();

  // non-local peer
  if (upcxx::local_team().rank_n() < upcxx::rank_n()) {
    if (upcxx::rank_me() == 0)
      say() << "non-local peer test";
    upcxx::team transposed =
      upcxx::world().split(upcxx::local_team().rank_me(), 0);
    upcxx::intrank_t peer =
      transposed[(transposed.rank_me() + 1) % transposed.rank_n()];
    upcxx::global_ptr<std::int64_t> gptr_peer =
      all_gptrs.fetch(peer).wait();
    upcxx::global_ptr<std::int64_t> aptr_peer =
      all_aptrs.fetch(peer).wait();
    test_all(peer == upcxx::rank_me(), gptr_peer, aptr_peer, ad);
    transposed.destroy();
  }

  ad.destroy();
  upcxx::delete_(gptr);
  // aptr intentionally leaked

  print_test_success();
  upcxx::finalize();
}
