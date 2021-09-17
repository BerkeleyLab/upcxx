#include <upcxx/upcxx.hpp>
#include "../util.hpp"

int main()
{
  upcxx::init();
  print_test_skipped(REASON);
  upcxx::finalize();
  return 0;
}
