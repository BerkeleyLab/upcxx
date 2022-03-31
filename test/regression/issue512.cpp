#include <upcxx/upcxx.hpp>
#include "../util.hpp"

int main(int argc, char* argv[])
{
  upcxx::init();
  print_test_header();
  auto fut = upcxx::make_future();
  when_all(fut);
  print_test_success();
  upcxx::finalize();
  return 0;
}
