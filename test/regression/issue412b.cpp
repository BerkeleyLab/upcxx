#include <upcxx/upcxx.hpp>
#include <iostream>
#include <stdlib.h>
#include "../util.hpp"

using namespace upcxx;

template<typename CO>
void destroy(CO *&pobj, entry_barrier eb=entry_barrier::user) {
  if (!pobj) return;
  pobj->destroy(eb);
  delete pobj;
  pobj = nullptr;
}

int main(int argc, char **argv) {
    upcxx::init();

    int test = 0;
    UPCXX_ASSERT_ALWAYS(argc == 2, "Pass test number as an argument");
    test = std::atoi(argv[1]);

    print_test_header();

    auto ad1 = new atomic_domain<int>({atomic_op::fetch_add});
    auto ad2 = new atomic_domain<int>({atomic_op::fetch_add});
    auto ad3 = new atomic_domain<int>({atomic_op::fetch_add});

    cuda_device dev1(cuda_device::invalid_device_id);
    cuda_device dev2(cuda_device::invalid_device_id);
    cuda_device dev3(cuda_device::invalid_device_id);

    auto tm1 = new team(world().split(0, 0));
    auto tm2 = new team(world().split(0, 0));
    auto tm3 = new team(world().split(0, 0));

    {
      dist_object<int> foo(1);
      foo.fetch(rank_me()).then([&](int) { 
        #define CASE(i, action) case i: { \
          if (!rank_me()) std::cout << "invoking: " #action << std::endl; \
          volatile bool truth = true; /* avoid pedantic warning from PGI */ \
          if (truth) action; \
          break; }
        switch (test) {
          // previously permitted, now prohibited cases
          CASE(0, return barrier_async())
          CASE(1, dist_object<int> ok(0))
          CASE(2, dist_object<int> ok(world(),0))
          CASE(3, destroy(ad1, entry_barrier::internal))
          CASE(4, destroy(ad2, entry_barrier::none))
          CASE(5, destroy(tm1, entry_barrier::internal))
          CASE(6, destroy(tm2, entry_barrier::none))
          CASE(7, dev1.destroy(entry_barrier::internal))
          CASE(8, dev2.destroy(entry_barrier::none))
          CASE(9, return reduce_one<int>(1,op_fast_add,0).then([](int){}))
          CASE(10, return reduce_all<int>(1,op_fast_add).then([](int){}))
          CASE(11, return broadcast<int>(1,0).then([](int){}))

          // always prohibited cases
          CASE(100, barrier())
          CASE(101, destroy(ad3))
          CASE(102, atomic_domain<int> ad4({atomic_op::fetch_add}))
          CASE(103, destroy(tm3))
          CASE(104, team tm4 = world().split(0, 0))
          CASE(105, dev3.destroy())
          CASE(106, cuda_device dev4(cuda_device::invalid_device_id))
          CASE(107, finalize())
          default:
            if (!rank_me()) std::cout << "unknown test: " << test << std::endl;
        }
        return make_future();
      }).wait();
      barrier();
    }

    // avoid memory leaks:
    destroy(tm1);
    destroy(tm2);
    destroy(tm3);
    destroy(ad1);
    destroy(ad2);
    destroy(ad3);

    print_test_success(false);

    upcxx::finalize();
    return 0;
}
