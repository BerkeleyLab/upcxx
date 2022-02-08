#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <unordered_set>

using upcxx::intrank_t;
using upcxx::global_ptr;
using upcxx::dist_object;

int main() {
  upcxx::init();
  {
    print_test_header();

    upcxx::team const &locals = upcxx::local_team();

    say()<<"local_team: "<<locals.rank_me()<<"/"<<locals.rank_n()<< ": "<<hostname();
    upcxx::barrier();

    UPCXX_ASSERT_ALWAYS(upcxx::world().rank_n() == upcxx::rank_n());
    UPCXX_ASSERT_ALWAYS(upcxx::world().rank_me() == upcxx::rank_me());
    
    UPCXX_ASSERT_ALWAYS(global_ptr<float>(nullptr).local() == nullptr);

    intrank_t peer_me = locals.rank_me();
    intrank_t peer_n = locals.rank_n();

    for(int i=0; i < peer_n; i++) {
      UPCXX_ASSERT_ALWAYS(locals.from_world(locals[i]) == i);
      UPCXX_ASSERT_ALWAYS(locals.from_world(locals[i], -0xbeef) == i);
      UPCXX_ASSERT_ALWAYS(upcxx::local_team_contains(locals[i]));
    }
    
    { // Try and generate some non-local ranks, not entirely foolproof.
      std::unordered_set<int> some_remotes;
      for(int i=0; i < peer_n; i++)
        some_remotes.insert((locals[i] + locals.rank_n()) % upcxx::rank_n());
      for(int i=0; i < peer_n; i++)
        some_remotes.erase(locals[i]);

      for(int remote: some_remotes) {
        UPCXX_ASSERT_ALWAYS(locals.from_world(remote, -0xbeef) == -0xbeef);
        UPCXX_ASSERT_ALWAYS(!upcxx::local_team_contains(remote));
        UPCXX_ASSERT_ALWAYS(upcxx::world().from_world(remote, -0xdad) == remote);
      }
    }
    
    dist_object<global_ptr<int>> dp(upcxx::allocate<int>(peer_n));

    for(int i=0; i < peer_n; i++) {
      upcxx::future<global_ptr<int>> f;
      if (i&1) {
        f = upcxx::rpc(
          locals, (peer_me + i) % peer_n,
          [=](dist_object<global_ptr<int>> &dp) {
            return upcxx::to_global_ptr<int>(dp->local() + i);
          },
          dp
        );
      } else {
        f = upcxx::rpc(
          locals[(peer_me + i) % peer_n],
          [=](dist_object<global_ptr<int>> &dp) {
            return upcxx::to_global_ptr<int>(dp->local() + i);
          },
          dp
        );
      }
      global_ptr<int> p = f.wait();

      UPCXX_ASSERT_ALWAYS(p == upcxx::to_global_ptr(p.local()));
      UPCXX_ASSERT_ALWAYS(p == upcxx::try_global_ptr(p.local()));
      UPCXX_ASSERT_ALWAYS(p.is_local());
      UPCXX_ASSERT_ALWAYS(p.where() == locals[(peer_me + i) % peer_n]);
      
      *p.local() = upcxx::rank_me();
    }
    
    {
      int hi;
      UPCXX_ASSERT_ALWAYS(!upcxx::try_global_ptr(&hi));
      // uncomment and watch it die via assert
      //upcxx::to_global_ptr(&hi);
    }
    
    upcxx::barrier();

    for(int i=0; i < peer_n; i++) {
      intrank_t want = locals[(peer_me + peer_n-i) % peer_n];
      intrank_t got = dp->local()[i];
      UPCXX_ASSERT_ALWAYS(want == got, "Want="<<want<<" got="<<got);
    }

    print_test_success();
  }
  upcxx::finalize();
}
