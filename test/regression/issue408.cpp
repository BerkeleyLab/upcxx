#include <upcxx/upcxx.hpp>
#include "../util.hpp"

using namespace upcxx;

struct T {
  int x;
  T() {}
  T(int x_) : x(x_) {}
  UPCXX_SERIALIZED_FIELDS(x)
};

int main() {
  upcxx::init();
  print_test_header();

  auto futs = rpc(
      0,
      operation_cx::as_future() | operation_cx::as_future(),
      []() {
          return T(-3);
        }
    );
  UPCXX_ASSERT_ALWAYS(std::get<0>(futs).wait_reference().x == -3);
  UPCXX_ASSERT_ALWAYS(std::get<1>(futs).wait_reference().x == -3);

  print_test_success();
  upcxx::finalize();
}
