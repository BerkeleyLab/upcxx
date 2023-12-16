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

    std::ostringstream position;
    intrank_t set_rank = -1, set_size = -1;
    #if UPCXX_VERSION >= 20210905
    {
      std::pair<intrank_t, intrank_t> pos = upcxx::local_team_position();
      std::tie(set_rank,set_size) = pos;

      UPCXX_ASSERT_ALWAYS(set_size > 0);
      UPCXX_ASSERT_ALWAYS(set_rank >= 0 && set_rank < set_size);
      position << " (position: " << set_rank << "/" << set_size << ")";
    }
    #endif

    upcxx::team const &locals = upcxx::local_team();

    say()<<"local_team: "<<locals.rank_me()<<"/"<<locals.rank_n()
         << position.str() << ": " << hostname();
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

    if (set_size > 0) { // have local_team_position()
      intrank_t set_size_max = upcxx::reduce_all(set_size, upcxx::op_fast_max).wait();
      UPCXX_ASSERT_ALWAYS(set_size_max == set_size); // all ranks in world agree
      intrank_t set_rank_max = upcxx::reduce_all(set_rank, upcxx::op_fast_max, locals).wait();
      UPCXX_ASSERT_ALWAYS(set_rank_max == set_rank); // all local ranks agree

      // validate each local team has a distinct set_rank in [0,set_size)
      intrank_t check = (peer_me == 0 ? set_rank + 1 : 0);
      intrank_t expect = 0; 
      for (int i = 1; i <= set_size; i++) expect += i;
      intrank_t result = upcxx::reduce_all(check, upcxx::op_fast_add).wait();
      UPCXX_ASSERT_ALWAYS(result == expect,"result="<<result<<" expect="<<expect);
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
    upcxx::deallocate(*dp);

    print_test_success();
  }
  upcxx::finalize();
}
