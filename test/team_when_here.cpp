#include <upcxx/upcxx.hpp>
#include <iostream>
#include <unistd.h>
#include "util.hpp"

using namespace upcxx;

int main() {
  init();
  print_test_header();
  if (upcxx::rank_n() < 2) {
      print_test_skipped("test requires two or more ranks");
      upcxx::finalize();
      return 0;
  }

  say() << "Hello";
  if (rank_me() == 0) {
    sleep(1);
    say() << "starting progress";
    for (int i=0; i < 100; i++) upcxx::progress();
    say() << "ending progress";
    sleep(1);
  }

  static bool constructed = false;
  std::vector<int> ranks(rank_n());
  std::iota(ranks.begin(), ranks.end(), 0);
  say() << "entering constructor";
  static team t = world().create(ranks);
  say() << "left constructor";
  constructed = true;
  team_id id = t.id(); 
  UPCXX_ASSERT_ALWAYS(id.when_here().is_ready());
  UPCXX_ASSERT_ALWAYS(&id.here() == &t);

  if (rank_me() != 0) {
    rpc(0, [](team_id id, int src) {
      say() << "began RPC callback from " << src;
      future<team&> f2 = id.when_here();
      //UPCXX_ASSERT_ALWAYS(!f2.is_ready());
      return f2.then([=](team &t2) { 
        say() << "when_here callback from " << src; 
        UPCXX_ASSERT_ALWAYS(in_progress());
        UPCXX_ASSERT_ALWAYS(constructed);
        UPCXX_ASSERT_ALWAYS(t2.id() == id);
        UPCXX_ASSERT_ALWAYS(&t2 == &t);
        UPCXX_ASSERT_ALWAYS(&id.here() == &t);
        future<team&> f3 = id.when_here();
        UPCXX_ASSERT_ALWAYS(f3.is_ready());
        UPCXX_ASSERT_ALWAYS(&f3.result() == &t);
      });
    }, id, rank_me()).wait();
  }

  say() << "starting barrier";
  upcxx::barrier();
  say() << "ending barrier";

  t.destroy();
  #if 0 // currently prohibited
  auto fd = id.when_here();
  UPCXX_ASSERT_ALWAYS(!fd.is_ready());
  #endif

  print_test_success();
  
  upcxx::finalize();
  return 0;
}
