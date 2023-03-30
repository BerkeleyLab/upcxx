#include <upcxx/upcxx.hpp>

#include "../util.hpp"

using namespace std;
using namespace upcxx;

// This test exercises self-moves of major object types

int main() {
  upcxx::init();

  print_test_header();
  
  intrank_t me = upcxx::rank_me();
  intrank_t n = upcxx::rank_n();
  intrank_t nebr = (me + 1) % n;
  
  {
    int val = 100 + me;
    upcxx::dist_object<int> obj1{val};
    upcxx::dist_id<int> id = obj1.id();
    UPCXX_ASSERT_ALWAYS(obj1.is_active());
    UPCXX_ASSERT_ALWAYS(*obj1 == val);
    UPCXX_ASSERT_ALWAYS(obj1.id() == id);
    obj1 = std::move(obj1);
    UPCXX_ASSERT_ALWAYS(obj1.is_active());
    UPCXX_ASSERT_ALWAYS(*obj1 == val);
    UPCXX_ASSERT_ALWAYS(obj1.id() == id);
    UPCXX_ASSERT_ALWAYS(obj1.fetch(nebr).wait() == 100 + nebr);

    upcxx::dist_object<int> obj2;
    UPCXX_ASSERT_ALWAYS(!obj2.is_active());
    UPCXX_ASSERT_ALWAYS(!obj2.has_value());
    obj2 = std::move(obj2);
    UPCXX_ASSERT_ALWAYS(!obj2.is_active());
    UPCXX_ASSERT_ALWAYS(!obj2.has_value());

    upcxx::barrier(); // ensures dist_object lifetime
  }
 {
  team tm1 = world().split(rank_me(),0);
  UPCXX_ASSERT_ALWAYS(tm1.is_active());
  UPCXX_ASSERT_ALWAYS(tm1.rank_n() == 1);
  tm1 = std::move(tm1);
  UPCXX_ASSERT_ALWAYS(tm1.is_active());
  UPCXX_ASSERT_ALWAYS(tm1.rank_n() == 1);
  tm1.destroy();

  team tm2;
  UPCXX_ASSERT_ALWAYS(!tm2.is_active());
  tm2 = std::move(tm2);
  UPCXX_ASSERT_ALWAYS(!tm2.is_active());

  auto alloc = make_gpu_allocator(2<<20);
  auto active = alloc.is_active();
  auto id = alloc.device_id();
  alloc = std::move(alloc);
  UPCXX_ASSERT_ALWAYS(alloc.is_active() == active);
  UPCXX_ASSERT_ALWAYS(alloc.device_id() == id);
  alloc.destroy();

  gpu_heap_allocator alloc2;
  UPCXX_ASSERT_ALWAYS(!alloc2.is_active());
  alloc2 = std::move(alloc2);
  UPCXX_ASSERT_ALWAYS(!alloc2.is_active());

  atomic_domain<int> ad({atomic_op::fetch_add});
  UPCXX_ASSERT_ALWAYS(ad.is_active());
  ad = std::move(ad);
  UPCXX_ASSERT_ALWAYS(ad.is_active());
  ad.destroy();

  atomic_domain<int> ad2;
  UPCXX_ASSERT_ALWAYS(!ad2.is_active());
  ad2 = std::move(ad2);
  UPCXX_ASSERT_ALWAYS(!ad2.is_active());
 }

  print_test_success();
  
  upcxx::finalize();
  return 0;
}
