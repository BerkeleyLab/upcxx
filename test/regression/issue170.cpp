#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  team t = world().split(1, rank_me());
  team_id id = t.id();
  assert(&t != &world());
  assert(id != world().id());
  
  future<team &> f = id.when_here();
  assert(f.is_ready());
  assert(&f.result() == &t);

  team &t1 = id.here();
  assert(&t1 == &t);

  t.destroy();

  print_test_success();
  upcxx::finalize();
  return 0;
}
