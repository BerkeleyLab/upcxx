#include <upcxx/upcxx.hpp>
#include <iostream>
#include <vector>
#include <cstdio>
#include "../util.hpp"

using namespace std;

long incoming;

int main(int argc, char **argv) {
  long iters = 0;
  std::size_t sz = 0;
  if (argc > 1) iters = atol(argv[1]);
  if (argc > 2) sz = (std::size_t)atol(argv[2]);
  if (iters <= 0) iters = 10000;
  if (sz <= 0) sz = 10*1024*1024;
  incoming = iters;

  upcxx::init();
  print_test_header();
  int me = upcxx::rank_me();
  int peer = (upcxx::rank_me() + 1) % upcxx::rank_n();
  if (!me) cout << upcxx::rank_n() << " ranks running " << iters << " iterations of " << sz << " bytes" << endl;

  upcxx::barrier();

  std::vector<char> myvec;
  myvec.resize(sz);
  printf("Hello from rank %i\n",me);
  cout << flush;

  upcxx::barrier();

  while (iters--) {
    upcxx::rpc_ff(peer, [=](upcxx::view<char> view) {
       assert(view.size() == sz);
       incoming--;
    }, 
    upcxx::make_view(myvec)
  );
  }

  do { upcxx::progress(); } while(incoming);
  
  print_test_success(); 
  upcxx::finalize();
  return 0;
}
