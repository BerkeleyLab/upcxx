#include "util.hpp"

int main(int argc, char **argv) {
  upcxx::init();
  print_test_header();

  { // as_defer
    // scalar rput/rget
    upcxx::global_ptr<int> gptr = upcxx::new_<int>();
    auto fut1 = upcxx::rput(3, gptr, upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!fut1.ready());
    fut1.wait();

    auto fut2 = upcxx::rget(gptr, upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!fut2.ready());
    UPCXX_ASSERT_ALWAYS(fut2.wait() == 3);

    upcxx::promise<> pro3;
    upcxx::rput(-7, gptr, upcxx::operation_cx::as_defer_promise(pro3));
    UPCXX_ASSERT_ALWAYS(!pro3.finalize().ready());
    pro3.get_future().wait();

    upcxx::promise<int> pro4;
    upcxx::rget(gptr, upcxx::operation_cx::as_defer_promise(pro4));
    UPCXX_ASSERT_ALWAYS(!pro4.finalize().ready());
    UPCXX_ASSERT_ALWAYS(pro4.get_future().wait() == -7);

    // vector rput/rget
    upcxx::global_ptr<int> gptr2 = upcxx::new_<int>(11);
    auto futs5 = upcxx::rput(gptr2.local(), gptr, 1,
                             upcxx::source_cx::as_defer_future() |
                             upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!std::get<0>(futs5).ready());
    UPCXX_ASSERT_ALWAYS(!std::get<1>(futs5).ready());
    std::get<0>(futs5).wait();
    std::get<1>(futs5).wait();

    auto fut6 = upcxx::rget(gptr2, gptr.local(), 1,
                            upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!fut6.ready());
    fut6.wait();

    // atomics
    upcxx::atomic_domain<std::int64_t> ad({upcxx::atomic_op::load,
                                           upcxx::atomic_op::store});
    upcxx::global_ptr<std::int64_t> aptr = upcxx::new_<std::int64_t>(0);
    auto fut7 = ad.store(aptr, 3, std::memory_order_relaxed,
                         upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!fut7.ready());
    fut7.wait();

    auto fut8 = ad.load(aptr, std::memory_order_relaxed,
                        upcxx::operation_cx::as_defer_future());
    UPCXX_ASSERT_ALWAYS(!fut8.ready());
    UPCXX_ASSERT_ALWAYS(fut8.wait() == 3);

    ad.destroy();
  }

  { // as_eager
    // scalar rput/rget
    upcxx::global_ptr<int> gptr = upcxx::new_<int>();
    auto fut1 = upcxx::rput(3, gptr, upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(fut1.ready());

    auto fut2 = upcxx::rget(gptr, upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(fut2.ready());
    UPCXX_ASSERT_ALWAYS(fut2.result() == 3);

    upcxx::promise<> pro3;
    upcxx::rput(-7, gptr, upcxx::operation_cx::as_eager_promise(pro3));
    UPCXX_ASSERT_ALWAYS(pro3.finalize().ready());

    upcxx::promise<int> pro4;
    upcxx::rget(gptr, upcxx::operation_cx::as_eager_promise(pro4));
    UPCXX_ASSERT_ALWAYS(pro4.finalize().ready());
    UPCXX_ASSERT_ALWAYS(pro4.get_future().result() == -7);

    // vector rput/rget
    upcxx::global_ptr<int> gptr2 = upcxx::new_<int>(11);
    auto futs5 = upcxx::rput(gptr2.local(), gptr, 1,
                             upcxx::source_cx::as_eager_future() |
                             upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(std::get<0>(futs5).ready());
    UPCXX_ASSERT_ALWAYS(std::get<1>(futs5).ready());

    auto fut6 = upcxx::rget(gptr2, gptr.local(), 1,
                            upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(fut6.ready());

    // atomics
    upcxx::atomic_domain<std::int64_t> ad({upcxx::atomic_op::load,
                                           upcxx::atomic_op::store});
    upcxx::global_ptr<std::int64_t> aptr = upcxx::new_<std::int64_t>(0);
    auto fut7 = ad.store(aptr, 3, std::memory_order_relaxed,
                         upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(fut7.ready());

    auto fut8 = ad.load(aptr, std::memory_order_relaxed,
                        upcxx::operation_cx::as_eager_future());
    UPCXX_ASSERT_ALWAYS(fut8.ready());
    UPCXX_ASSERT_ALWAYS(fut8.result() == 3);

    ad.destroy();
  }

  print_test_success();
  upcxx::finalize();
}
