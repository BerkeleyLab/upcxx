#include <upcxx/upcxx.hpp>
#include <iostream>
#include <unistd.h>
#include "util.hpp"

using namespace upcxx;

int main() {
  init();
  print_test_header();

  say() << "Hello";
  if (rank_me() == 0) {
    sleep(1);
    say() << "starting progress";
    for (int i=0; i < 100; i++) upcxx::progress();
    say() << "ending progress";
    sleep(1);
  }

  static bool constructed = false;
  say() << "entering constructor";
  dist_object<int> d(rank_me());
  say() << "left constructor";
  constructed = true;
  dist_id<int> id = d.id(); 
  auto f1 = id.when_here();
  UPCXX_ASSERT_ALWAYS(!f1.is_ready());
  UPCXX_ASSERT_ALWAYS(&id.here() == &d);

  if (rank_me() != 0) {
    upcxx::progress();
    UPCXX_ASSERT_ALWAYS(f1.is_ready());
    UPCXX_ASSERT_ALWAYS(id.when_here().is_ready());
    UPCXX_ASSERT_ALWAYS(&id.here() == &d);

    rpc(0, [](dist_id<int> id, int src) {
      say() << "began RPC callback from " << src;
      future<dist_object<int>&> f2 = id.when_here();
      UPCXX_ASSERT_ALWAYS(!f2.is_ready());
      return f2.then([=](dist_object<int>&) { 
        say() << "when_here callback from " << src; 
        UPCXX_ASSERT_ALWAYS(in_progress());
        UPCXX_ASSERT_ALWAYS(constructed);
      });
    }, id, rank_me()).wait();
  }

  say() << "starting barrier";
  upcxx::barrier();
  say() << "ending barrier";

  print_test_success();
  
  upcxx::finalize();
  return 0;
}
