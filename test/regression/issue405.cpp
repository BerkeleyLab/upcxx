#include <upcxx/upcxx.hpp>
#include <iostream>
#include <assert.h>

using namespace upcxx;

int main() {
  upcxx::init();

  global_ptr<int> gp = new_<int>();
  if (!rank_me()) *gp.local() = 42;
  auto gp0 = broadcast(gp,0).wait();
  global_ptr<int> gp2 = new_<int>(0);
  int *lp = gp2.local();
  copy(gp0, lp, 1).wait();
  assert(*lp == 42);
  copy(gp0, gp2, 1).wait();
  assert(*gp2.local() == 42);
  barrier();
  *gp.local() = 0;
  copy(lp, gp, 1).wait();
  assert(*gp.local() == 42);
 
  barrier();
  delete_(gp);
  delete_(gp2);
  if (!rank_me()) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
  return 0;
}
