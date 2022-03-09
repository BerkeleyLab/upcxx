#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <iostream>

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
    template<typename R>
    static int* deserialize(R &r, void *spot) {
      return ::new (spot) int(r.template read<int>());
    }
  };
};

bool got_ff = false;

int main() {
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
          UPCXX_ASSERT_ALWAYS(f.ready());
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
    UPCXX_ASSERT_ALWAYS(f.ready());
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
    
    upcxx::barrier(); // ensures dist_object lifetime
  }

  print_test_success();
  
  upcxx::finalize();
  return 0;
}
