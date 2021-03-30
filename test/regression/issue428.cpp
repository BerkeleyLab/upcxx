#include <upcxx/upcxx.hpp>
#include <iostream>

using namespace upcxx;

int main() {
  upcxx::init();

  constexpr int N = 100;
  char A[N];
  auto myview = make_view(A, A+N);

  {
    auto f = rpc(0, [](view<char> const &){}, myview);
    f.wait();
  }
  
  {
    auto f = rpc(world(), 0, [](view<char> const &){}, myview);
    f.wait();
  }
  
  upcxx::finalize();
  return 0;
}
