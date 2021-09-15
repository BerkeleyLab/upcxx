#include <iostream>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <set>
#include <vector>
#include <unordered_map>
#include <iomanip>
#include <cmath>
#include <random>

using namespace std;
using upcxx::team;
using upcxx::team_id;

// true iff both teams have the same members in the same order
bool team_equal(const team &tm1, const team &tm2) {
  if (tm1.rank_n() != tm2.rank_n()) return false;
  for (int i=0; i < tm1.rank_n(); i++) {
    if (tm1[i] != tm2[i]) return false;
  }
  return true;
}

std::unordered_set<team_id> ids;

void id_check(const team &tm) {
  team_id id = tm.id();
  if (tm.rank_me() == 0) {
    say s;
    s << " \t";
    for (size_t i=0; i < sizeof(team_id); i++)
      s << std::setfill('0') << std::setw(2) << std::hex 
        << int( ((unsigned char *)&id)[i] );
    s << " [ " << std::dec;
    for (int i = 0; i < tm.rank_n(); i++) {
      if (i) s << ", ";
      s << tm[i];
    }
    s << " ] ";
  }

  barrier(tm);
  UPCXX_ASSERT_ALWAYS(ids.count(id) == 0);
  ids.insert(id);
  barrier(tm);
}

void test_team(const upcxx::team &tm) {
  int me = tm.rank_me();
  int n = tm.rank_n();
  id_check(tm);
 
  // re-create tm via different overloads
  std::vector<upcxx::intrank_t> v_all;
  v_all.resize(n);
  std::iota(v_all.begin(), v_all.end(), 0); // [0,1,..(n-1)]
  std::forward_list<int> l_all(v_all.cbegin(), v_all.cend());

  team tc1 = tm.create(v_all);
  team tc2 = tm.create(v_all.data(), v_all.data()+n);
  team tc3 = tm.create(l_all.cbegin(), l_all.cend());

  id_check(tc1);
  UPCXX_ASSERT_ALWAYS(team_equal(tc1, tm));
  id_check(tc2);
  UPCXX_ASSERT_ALWAYS(team_equal(tc2, tm));
  id_check(tc3);
  UPCXX_ASSERT_ALWAYS(team_equal(tc3, tm));

  // re-create tm via world ranks 
  std::vector<long> v_all2;
  for (int i = 0; i < n; i++) {
    v_all2.push_back(tm[i]);
  }
  team tc4 = upcxx::world().create(v_all2);
  id_check(tc4);
  UPCXX_ASSERT_ALWAYS(team_equal(tc4, tm));

  bool i_am_odd = (me % 2);
  // construct vectors of the even and odd ranks in tm that match our phase
  std::vector<int> v_even, v_odd, vw_even, vw_odd;
  for (int i = 0; i < n; i++) {
    if (i%2) {
      if (i_am_odd) {
        v_odd.push_back(i);
        vw_odd.push_back(tm[i]);
      }
    } else {
      if (!i_am_odd) {
        v_even.push_back(i);
        vw_even.push_back(tm[i]);
      }
    }
  }
  // create two teams in one call:
  std::vector<int> &v_pick = (i_am_odd ? v_odd : v_even);
  team tc5 = tm.create(v_pick);

  // validate tc5:
  id_check(tc5);
  UPCXX_ASSERT_ALWAYS(tc5.rank_n() == (int)v_pick.size());
  std::vector<int> &vw_pick = (i_am_odd ? vw_odd : vw_even);
  for (size_t i=0; i < v_pick.size(); i++)
    UPCXX_ASSERT_ALWAYS(tc5[i] == vw_pick[i]);

  // separate calls where half the ranks are passive in each call:
  team tc6e = upcxx::world().create(vw_even);
  team tc6o = upcxx::world().create(vw_odd);
  if (i_am_odd) {
    id_check(tc6o);
    UPCXX_ASSERT_ALWAYS(team_equal(tc6o, tc5));
  } else {
    id_check(tc6e);
    UPCXX_ASSERT_ALWAYS(team_equal(tc6e, tc5));
    tc6e.destroy();
  }
  tc6o.destroy(); // destroys teams that are valid and invalid (optional) 

  tc1.destroy();
  tc2.destroy();
  tc3.destroy();
  tc4.destroy();
  tc5.destroy();
}

int main() {
  upcxx::init();
  {
    print_test_header();
    
    std::mt19937_64 gen;
    gen.seed(upcxx::rank_me());
    auto rng = [&]() -> int {
      return (gen() % 
                 (1 + (int)std::log2(upcxx::rank_n())) )
             + upcxx::rank_n();
    };
    
    // world
    test_team(upcxx::world());

    // world split #1
    team tm1 = upcxx::world().split(rng(), upcxx::rank_me());
    test_team(tm1);
    
    // world split #2
    team tm2 = upcxx::world().split(rng(), -1*upcxx::rank_me());
    test_team(tm2);
    
    // world split #2 split #1
    team tm3 = tm2.split(rng(), (unsigned)upcxx::rank_me()*0xdeadbeefu);
    test_team(tm3);
   
    // world reversed
    team tm4 = upcxx::world().split(0, -1*upcxx::rank_me());
    test_team(tm4);
    
    tm4.destroy();
    tm3.destroy();
    tm2.destroy();
    tm1.destroy();
    
    { // exercise invalid team/team_id
      team_id fake;
      UPCXX_ASSERT_ALWAYS(fake == team_id());
      team t = upcxx::world().create((int*)nullptr, (int*)nullptr);
      for (int i=0; i < upcxx::rank_me()+2; i++)
        t.destroy(); // invalid.destroy is a non-collective no-op

      // uncomment to exercise erronous use cases that assert in debug codemode:
      //team &nope = fake.here();
      //auto f = fake.when_here();
      //team nope = t.create(std::vector<int>());
      //team nope = t.split(0,0);
      //say() << t[0];
      //say() << t.id();
      //say() << t.rank_me() << "/" << t.rank_n();
      //team nope = upcxx::world().create(std::vector<int>{0});
      //team nope = upcxx::world().create(std::vector<int>{upcxx::rank_me(),-2});
      //team nope = upcxx::world().create(std::vector<int>{upcxx::rank_me(),upcxx::rank_me()});
    }
    
    print_test_success();
  }
  
  upcxx::finalize();
  return 0;
}
