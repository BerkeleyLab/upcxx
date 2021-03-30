#include <stddef.h>
#include <type_traits>
#include <iostream>
#include <cassert>
#include <functional>
#include <vector>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

using namespace upcxx;

volatile bool cuda_enabled;
upcxx::cuda_device *gpu_device;
upcxx::device_allocator<upcxx::cuda_device> *gpu_alloc;

std::vector<std::function<void()>> post_fini;

template<typename Device>
void run_test(typename Device::id_type id, std::size_t heap_size) {

  using Allocator = upcxx::device_allocator<Device>;
  assert_same<typename Allocator::device_type, Device>();
  using id_type = typename Device::id_type;
  using gp_type = global_ptr<int, Device::kind>;
  using dp_type = typename Device::template pointer<int>;

  const gp_type gp_null;
  assert(!gp_null);
  constexpr dp_type dp_null = Device::template null_pointer<int>();
  dp_type dp_null0 = dp_type();
  // we don't technically require a default constructed Device:pointer to
  // correspond to null, but it would be very surprising if it did not.
  assert(dp_null0 == dp_null);
  constexpr id_type id_invalid = Device::invalid_device_id;

  // test inactive devices and allocators
  for (int i=0; i < 20+upcxx::rank_me(); i++) { 
    Device di = Device();
    assert(!di.is_active());
    assert(di.device_id() == id_invalid);
    Device di2(std::move(di));
    assert(!di.is_active());
    assert(di.device_id() == id_invalid);
    assert(!di2.is_active());
    assert(di2.device_id() == id_invalid);

    Allocator ai = Allocator();
    assert(!ai.is_active());
    Allocator ai2(std::move(ai));
    assert(!ai.is_active());
    assert(!ai2.is_active());
    assert(Allocator::local(gp_null) == dp_null);
    assert(Allocator::device_id(gp_null) == id_invalid);
    assert(ai.to_global_ptr(dp_null) == gp_null);
    ai.deallocate(gp_null);

    if (i < 6) { // sometimes explicit destroy
      entry_barrier lev;
      switch (i % 3) { // with varying eb
        case 0: lev = entry_barrier::user; break;
        case 1: lev = entry_barrier::internal; break;
        case 2: lev = entry_barrier::none; break;
      }
      if (i < 3) di.destroy(lev);
      else      di2.destroy(lev);
    }
  }

  // deliberately create three heaps on the same device
  // managed via pointer for precision testing of destruction
  Device *d0 = new Device(id);
  Allocator *a0 = new Allocator(*d0, heap_size);
  assert(d0->is_active()); assert(a0->is_active());
  assert(d0->device_id() == id);

  bool have1 = rank_me()%2;
  Device *d1 = new Device(have1?id:id_invalid);
  Allocator *a1 = new Allocator(*d1, heap_size);
  assert(d1->is_active() == have1); assert(a1->is_active() == have1);
  assert(d1->device_id() == (have1?id:id_invalid));
  if (have1 && rank_me()%3) { // test moving an active device
    Device *d1a = new Device(std::move(*d1));
    assert(!d1->is_active());
    delete d1;
    d1 = d1a;
    assert(d1->is_active()); assert(a1->is_active());
  }

  bool have2 = !(rank_me()%2);
  Device *d2 = new Device(have2?id:id_invalid);
  Allocator *a2 = new Allocator(*d2, heap_size);
  assert(d2->is_active() == have2); assert(a2->is_active() == have2);
  assert(d2->device_id() == (have2?id:id_invalid));
  if (have2 && rank_me()%3) { // test moving an active allocator
    Allocator *a2a = new Allocator(std::move(*a2));
    assert(!a2->is_active());
    delete a2;
    a2 = a2a;
    assert(d2->is_active()); assert(a2->is_active());
  }

  // and a device with no heap
  Device *d3 = new Device(id);
  assert(d3->is_active());
  assert(d3->device_id() == id);

  // allocate some objects
  auto alloc_check = [=](Allocator *a, size_t num) {
    assert(a->is_active());
    gp_type gp = a->template allocate<int>(num); 
    assert(gp);
    dp_type dp = Allocator::local(gp);
    assert(dp != dp_null);
    gp_type gp1 = a->to_global_ptr(dp);
    assert(gp == gp1);
    assert(Allocator::device_id(gp) == id);
    return gp;
  };
  gp_type gp0 = alloc_check(a0,1); 
  gp_type gp1 = nullptr;
  if (have1) gp1 = alloc_check(a1,10); 
  gp_type gp2 = nullptr;
  if (have2) gp2 = alloc_check(a2,20); 
  assert(gp0 != gp1); assert(gp0 != gp2);

  a0->deallocate(gp0);
  a1->deallocate(gp1);
  //a2->deallocate(gp2); // deliberate leak

  d0->destroy(); // normal destruction
  assert(!d0->is_active());
  assert(!a0->is_active());

  delete a1;     // allocator destructor,
  assert(d1->is_active() == have1);
  d1->destroy(); // ... then device destroy

  d3->destroy(); // destroy with no allocator

  // defer 2 to post-finalize
  post_fini.push_back(std::function<void()>([=]() {
    delete d2;
    delete a2;
  }));

  upcxx::barrier();

  // check bad_alloc exhaustion
  Device *d4 = new Device(id);
  assert(d4->is_active());
  try {
    Allocator *a4 = new Allocator(*d4, 1ULL<<60);
    say() << "ERROR: Failed to generate device bad_alloc exn!";
  } catch (std::bad_alloc &e) {
    say() << "got expected exn: " << e.what();
  }
  assert(d4->is_active());
  post_fini.push_back(std::function<void()>([=]() {
    delete d4;
  }));
}

int main() {
  upcxx::init();
  print_test_header();
  int me = upcxx::rank_me();

  // check that required device members exist with sane-looking values
  // note these should be defined even when CUDA kind is disabled
  assert_same<cuda_device::id_type, int>();
  assert_same<cuda_device::pointer<double>, double *>();
  assert(cuda_device::null_pointer<double>() == nullptr);
  assert(cuda_device::default_alignment<double>() > 0);
  assert(cuda_device::kind == memory_kind::cuda_device);
  assert(cuda_device::invalid_device_id != 0);
  #if UPCXX_CUDA_ENABLED
    cuda_enabled = true;
  #endif
  if (cuda_enabled) { 
    run_test<cuda_device>(0, 2<<20);
  }

  upcxx::finalize();

  for (auto &f : post_fini) {
    f();
  }

  if (!me) print_test_success();
}

