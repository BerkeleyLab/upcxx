#include <upcxx/upcxx.hpp>
#include <iostream>
#include <sstream>
#include <assert.h>

using namespace upcxx;

int main() {
  upcxx::init();
 
  persona &p = upcxx::master_persona();

  future<int> f1 = p.lpc([]() -> int { return 42; });
  assert(f1.wait() == 42);

  future<int&> f2 = p.lpc([]() -> int& { static int res = 42; return res; });
  assert(f2.wait() == 42);
  
  future<int> f3 = p.lpc([]() -> int&& { static int res = 42; return std::move(res); });
  assert(f3.wait() == 42);
  
  upcxx::finalize();
  return 0;
}
