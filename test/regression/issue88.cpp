// See: https://bitbucket.org/berkeleylab/upcxx/issues/88/progress-internal-advances-the-state-of

#include <upcxx/upcxx.hpp>

#include "../util.hpp"

#include <cstdlib>
#include <iostream>

int main() {
  upcxx::init();
  print_test_header();

  bool success = true;

  if(upcxx::rank_me() == 0) {
    upcxx::intrank_t nebr = (upcxx::rank_me() + 1) % upcxx::rank_n();
    upcxx::future<int> got = upcxx::rpc(nebr, [=]() { return upcxx::rank_me(); });
    
    int countdown = 10*1000*1000;
    while(!got.is_ready() && --countdown)
      upcxx::progress(upcxx::progress_level::internal);
    
    success = countdown == 0;
    got.wait();
  }
  
  print_test_success(success);
  upcxx::finalize();
  return 0;
}
