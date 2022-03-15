#include "../util.hpp"

using namespace upcxx;

int main(int argc, char **argv) {
  upcxx::init();
  print_test_header();

  future<> f  = make_future();
  auto     f2 = when_all(f,f);
  future<> f3 = when_all(f,f);
  auto     f4 = f2;
  future<> f5 = f;
  future<> f6(f2);
  future<> f7;
  f7 = f2;

  print_test_success();
  upcxx::finalize();
}
