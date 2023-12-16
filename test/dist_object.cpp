#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <iostream>
#include <unordered_map>

using namespace std;
using namespace upcxx;

struct asym_type {
  int i;
  asym_type(int i_) : i(i_) {}
  struct upcxx_serialization {
    template<typename W>
    static void serialize(W &w, asym_type const &x) {
      w.template write<int>(x.i);
    }
    template<typename R, typename Storage>
    static int* deserialize(R &r, Storage storage) {
      return storage.construct(r.template read<int>());
    }
  };
};

struct non_default_constructible {
  int x;
  non_default_constructible(int x_) : x(x_) {}
};

bool got_ff = false;

// dist_object construction before init (and destruction after finalize)
dist_object<non_default_constructible> global_obj1;
dist_object<non_default_constructible> global_obj2{upcxx::inactive, -2};

dist_id<int> global_id1;
dist_id<int> global_id2;

int main() {
  // dist_object construction before init (and destruction after finalize)
  dist_object<non_default_constructible> preinit_obj1;
  dist_object<non_default_constructible> preinit_obj2{upcxx::inactive, -3};
  // member functions that are permitted before init
  UPCXX_ASSERT_ALWAYS(!global_obj1.is_active());
  UPCXX_ASSERT_ALWAYS(!global_obj1.has_value());
  UPCXX_ASSERT_ALWAYS(!global_obj2.is_active());
  UPCXX_ASSERT_ALWAYS(global_obj2.has_value());
  UPCXX_ASSERT_ALWAYS(!preinit_obj1.is_active());
  UPCXX_ASSERT_ALWAYS(!preinit_obj1.has_value());
  UPCXX_ASSERT_ALWAYS(!preinit_obj2.is_active());
  UPCXX_ASSERT_ALWAYS(preinit_obj2.has_value());
  global_obj1.emplace(-4);
  preinit_obj1.emplace(-5);
  UPCXX_ASSERT_ALWAYS(global_obj1.has_value());
  UPCXX_ASSERT_ALWAYS(preinit_obj1.has_value());
  UPCXX_ASSERT_ALWAYS((*global_obj1).x == -4);
  UPCXX_ASSERT_ALWAYS((*global_obj2).x == -2);
  UPCXX_ASSERT_ALWAYS((*preinit_obj1).x == -5);
  UPCXX_ASSERT_ALWAYS((*preinit_obj2).x == -3);
  UPCXX_ASSERT_ALWAYS(global_obj1->x == -4);
  UPCXX_ASSERT_ALWAYS(global_obj2->x == -2);
  UPCXX_ASSERT_ALWAYS(preinit_obj1->x == -5);
  UPCXX_ASSERT_ALWAYS(preinit_obj2->x == -3);

  dist_id<int> preinit_id1;
  dist_id<int> preinit_id2;
  UPCXX_ASSERT_ALWAYS(global_id1 == global_id2);
  UPCXX_ASSERT_ALWAYS(global_id1 == dist_id<int>());
  UPCXX_ASSERT_ALWAYS(preinit_id1 == preinit_id2);
  UPCXX_ASSERT_ALWAYS(preinit_id1 == global_id1);

  upcxx::init();

  print_test_header();
  
  intrank_t me = upcxx::rank_me();
  intrank_t n = upcxx::rank_n();
  intrank_t nebr = (me + 1) % n;
  
  { // dist_object lifetime scope
    upcxx::dist_object<int> obj1{100 + me};
    upcxx::dist_object<int> obj2{200 + me};
    upcxx::dist_object<int> obj3{300 + me};
    
    future<int> const f = when_all(
      upcxx::rpc(nebr,
        [=](dist_object<int> &his1, dist_object<int> const &his2, dist_id<int> id1) {
          cout << me << "'s nebr values = "<< *his1 << ", " << *his2 << '\n';
          UPCXX_ASSERT_ALWAYS(*his1 == 100 + upcxx::rank_me(), "incorrect value for neighbor 1");
          UPCXX_ASSERT_ALWAYS(*his2 == 200 + upcxx::rank_me(), "incorrect value for neighbor 2");

          team & t1 = his1.team();
          const team & t2 = his2.team();
          UPCXX_ASSERT_ALWAYS(&t1 == &t2);
          UPCXX_ASSERT_ALWAYS(&t2 == &world());

          UPCXX_ASSERT_ALWAYS(his1.id() == id1);
          UPCXX_ASSERT_ALWAYS(his1.id() != his2.id());
          dist_id<int> const idc = his1.id();
          future<dist_object<int>&> f = idc.when_here();
          UPCXX_ASSERT_ALWAYS(f.is_ready());
          UPCXX_ASSERT_ALWAYS(&f.result() == &his1);
          // issue 312:
          //UPCXX_ASSERT_ALWAYS(&idc.here() == &his1);
          //UPCXX_ASSERT_ALWAYS(&id1.here() == &his1);
        },
        obj1, obj2, obj1.id()
      ),
      obj3.fetch(nebr)
    );
   
    int expect = 300+nebr;
    // exercise const future
    UPCXX_ASSERT_ALWAYS(f.wait() == expect);
    UPCXX_ASSERT_ALWAYS(f.wait<0>() == expect);
    UPCXX_ASSERT_ALWAYS(std::get<0>(f.wait_tuple()) == expect);
    UPCXX_ASSERT_ALWAYS(f.is_ready());
    UPCXX_ASSERT_ALWAYS(f.result() == expect);
    UPCXX_ASSERT_ALWAYS(f.result<0>() == expect);
    UPCXX_ASSERT_ALWAYS(std::get<0>(f.result_tuple()) == expect);
    f.then([=](int val) { UPCXX_ASSERT_ALWAYS(val == expect); }).wait();

    upcxx::dist_object<int> &obj3c = obj3;
    UPCXX_ASSERT_ALWAYS(obj3c.fetch(nebr).wait() == expect);

    upcxx::dist_object<asym_type> obj4{300 + me};
    future<int> f4 = obj4.fetch(nebr);
    UPCXX_ASSERT_ALWAYS(f4.wait() == expect);

    upcxx::rpc_ff(nebr,
        [=](dist_object<int> &his1, dist_object<int> const &his2) {
          UPCXX_ASSERT_ALWAYS(*his1 == 100 + upcxx::rank_me(), "incorrect value for neighbor 1");
          UPCXX_ASSERT_ALWAYS(*his2 == 200 + upcxx::rank_me(), "incorrect value for neighbor 2");
          got_ff = true;
        },
        obj1, obj2
      );
    
    while(!got_ff)
      upcxx::progress();
    
    // spec issue 192 additions
    upcxx::dist_object<int> obj5;
    UPCXX_ASSERT_ALWAYS(!obj5.is_active());
    UPCXX_ASSERT_ALWAYS(!obj5.has_value());
    obj5.emplace(400 + upcxx::rank_me());
    UPCXX_ASSERT_ALWAYS(!obj5.is_active());
    UPCXX_ASSERT_ALWAYS(obj5.has_value());
    obj5.activate(upcxx::world());
    UPCXX_ASSERT_ALWAYS(obj5.is_active());
    upcxx::dist_object<int> obj6;
    obj6 = std::move(obj5);
    UPCXX_ASSERT_ALWAYS(!obj5.is_active());
    UPCXX_ASSERT_ALWAYS(obj6.is_active());
    UPCXX_ASSERT_ALWAYS(obj6.has_value());
    UPCXX_ASSERT_ALWAYS(*obj6 == 400 + upcxx::rank_me());
    UPCXX_ASSERT_ALWAYS(obj6.fetch(nebr).wait() == 400 + nebr);
    upcxx::dist_object<int> obj7{upcxx::inactive, 500 + upcxx::rank_me()};
    UPCXX_ASSERT_ALWAYS(!obj7.is_active());
    UPCXX_ASSERT_ALWAYS(obj7.has_value());
    UPCXX_ASSERT_ALWAYS(*obj7 == 500 + upcxx::rank_me());
    // tests with initializer lists
    dist_object<std::unordered_map<int, int>> d1 =
      dist_object<std::unordered_map<int, int>>({});
    UPCXX_ASSERT_ALWAYS(d1.is_active());
    UPCXX_ASSERT_ALWAYS(d1.has_value());
    dist_object<std::unordered_map<int, int>> d2{};
    UPCXX_ASSERT_ALWAYS(!d2.is_active());
    UPCXX_ASSERT_ALWAYS(!d2.has_value());
    dist_object<std::unordered_map<int, int>> d3 = {};
    UPCXX_ASSERT_ALWAYS(!d3.is_active());
    UPCXX_ASSERT_ALWAYS(!d3.has_value());
    dist_object<std::unordered_map<int, int>> d4 =
      dist_object<std::unordered_map<int, int>>{};
    UPCXX_ASSERT_ALWAYS(!d4.is_active());
    UPCXX_ASSERT_ALWAYS(!d4.has_value());
    dist_object<std::unordered_map<int, int>> d5{{}};
    UPCXX_ASSERT_ALWAYS(d5.is_active());
    UPCXX_ASSERT_ALWAYS(d5.has_value());

    upcxx::barrier(); // ensures dist_object lifetime
  }

  print_test_success();
  
  upcxx::finalize();
  return 0;
}
