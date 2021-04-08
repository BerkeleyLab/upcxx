#include "util.hpp"

int main(int argc, char **argv) {
  upcxx::init();
  print_test_header();

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

  print_test_success();
  upcxx::finalize();
}
