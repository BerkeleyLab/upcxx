#include "util.hpp"

#define CHECK_READY(fut, expected) \
  UPCXX_ASSERT_ALWAYS(fut.ready() == expected)
#define CHECK_RESULT(fut, eager, expected) \
  UPCXX_ASSERT_ALWAYS((eager ? fut.result() : fut.wait()) == expected)

template<typename SrcFutCxFn, typename OpFutCxFn,
         typename OpEmptyPromCxFn, typename OpIntPromCxFn>
void test(bool eager, SrcFutCxFn src_fut_cx_fn, OpFutCxFn op_fut_cx_fn,
          OpEmptyPromCxFn empty_prom_fn, OpIntPromCxFn int_prom_fn) {
  upcxx::global_ptr<int> gptr = upcxx::new_<int>();
  auto fut1 = upcxx::rput(3, gptr, op_fut_cx_fn());
  CHECK_READY(fut1, eager);
  fut1.wait();

  auto fut2 = upcxx::rget(gptr, op_fut_cx_fn());
  CHECK_READY(fut2, eager);
  CHECK_RESULT(fut2, eager, 3);

  upcxx::promise<> pro3;
  upcxx::rput(-7, gptr, empty_prom_fn(pro3));
  CHECK_READY(pro3.finalize(), eager);
  pro3.get_future().wait();

  upcxx::promise<int> pro4;
  upcxx::rget(gptr, int_prom_fn(pro4));
  CHECK_READY(pro4.finalize(), eager);
  CHECK_RESULT(pro4.get_future(), eager, -7);

  // vector rput/rget
  upcxx::global_ptr<int> gptr2 = upcxx::new_<int>(11);
  auto futs5 = upcxx::rput(gptr2.local(), gptr, 1,
                           op_fut_cx_fn() | src_fut_cx_fn());
  CHECK_READY(std::get<0>(futs5), eager);
  CHECK_READY(std::get<1>(futs5), eager);
  std::get<0>(futs5).wait();
  std::get<1>(futs5).wait();

  auto fut6 = upcxx::rget(gptr2, gptr.local(), 1, op_fut_cx_fn());
  CHECK_READY(fut6, eager);
  fut6.wait();

  // atomics -- cannot assume synchronous completion
  if (!eager) {
    upcxx::atomic_domain<std::int64_t> ad({upcxx::atomic_op::load,
                                           upcxx::atomic_op::store});
    upcxx::global_ptr<std::int64_t> aptr = upcxx::new_<std::int64_t>(0);
    auto fut7 = ad.store(aptr, 3, std::memory_order_relaxed, op_fut_cx_fn());
    CHECK_READY(fut7, eager);
    fut7.wait();

    auto fut8 = ad.load(aptr, std::memory_order_relaxed, op_fut_cx_fn());
    CHECK_READY(fut8, eager);
    CHECK_RESULT(fut8, eager, 3);

    ad.destroy();
  }
}

int main(int argc, char **argv) {
  upcxx::init();
  print_test_header();

  test(!UPCXX_DEFER_COMPLETION,
       upcxx::source_cx::as_future,
       upcxx::operation_cx::as_future,
       upcxx::operation_cx::as_promise<>,
       upcxx::operation_cx::as_promise<int>);
  test(false,
       upcxx::source_cx::as_defer_future,
       upcxx::operation_cx::as_defer_future,
       upcxx::operation_cx::as_defer_promise<>,
       upcxx::operation_cx::as_defer_promise<int>);
  test(true,
       upcxx::source_cx::as_eager_future,
       upcxx::operation_cx::as_eager_future,
       upcxx::operation_cx::as_eager_promise<>,
       upcxx::operation_cx::as_eager_promise<int>);

  print_test_success();
  upcxx::finalize();
}
