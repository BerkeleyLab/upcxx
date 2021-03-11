#include <upcxx/upcxx.hpp>
#include "../util.hpp"

using namespace std;

int main() {
  upcxx::init();
  print_test_header();
  
  int countdown = 2;
  auto handler1 = [&](std::string xs) {
    UPCXX_ASSERT_ALWAYS(xs == std::string(10*upcxx::rank_n(),'x'));
    countdown -= 1;
  };
  
  #if 1 // avoid issue 266
  auto handler2 = [&](std::string xs) {
    UPCXX_ASSERT_ALWAYS(xs == std::string(10*upcxx::rank_n(),'x'));
    countdown -= 1;
  };
  #else
  auto handler2 = handler1;
  #endif

  upcxx::experimental::reduce_all_nontrivial(
    std::string(10,'x'),
    upcxx::experimental::op_add,
    upcxx::world(),
    upcxx::operation_cx::as_lpc(upcxx::current_persona(), handler1) |
    upcxx::operation_cx::as_lpc(upcxx::current_persona(), handler2)
  );
  
  while(countdown != 0)
    upcxx::progress();

  print_test_success(true);
  upcxx::finalize();
}
