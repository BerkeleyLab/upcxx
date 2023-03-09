#include <upcxx/upcxx.hpp>
#include <iostream>
#include "util.hpp"

using namespace upcxx;

#ifndef DEVICE
#error "This example requires UPC++ to be built with GPU support."
#endif

// demonstrate std::bad_alloc exception behavior on device memory exhaustion
int main() {
  upcxx::init();

  print_test_header();
  size_t me = upcxx::rank_me(); 
  size_t billion = 1000000000ULL;
  size_t trillion = billion*1000ULL;

  Device dev(0);
  device_allocator<Device> *dap = nullptr;
  assert(dev.is_active());

  try {
    if (!me) say("") << "Making an absurd segment request on some ranks...";
    barrier();
    // odd ranks request 1MB
    // even ranks request roughly rank+1 TB
    size_t mysz;
    if (me % 2 == 1) mysz = 1<<20;
    else             mysz = (me+1)*trillion; 
    assert(mysz > 0);
    dap = new device_allocator<Device>(dev, mysz);
    say() << "ERROR:  device_allocator construction failed to throw exception!";
  } catch (std::bad_alloc const &e) {
    say() << "Caught expected exception: \n" << e.what();
    #if RETHROW
      throw;
    #endif
  }
  assert(!dap);
  assert(dev.is_active());
  barrier();

  try {
    if (!me) say("") << "Now asking for something reasonable...";
    barrier();
    int64_t mysz = 1<<20; 
    assert(mysz > 0);
    dap = new device_allocator<Device>(dev, (size_t)mysz);
    assert(dap);
    assert(dap->segment_size() == mysz);
    assert(static_cast<heap_allocator*>(dap)->segment_size() == mysz);
    assert(dap->segment_used() == 0);
    assert(static_cast<heap_allocator*>(dap)->segment_used() == 0);
    int64_t used = 0;
    for (int i = 0; i < 100; i++) {
      auto gp = dap->allocate<double>(1);
      assert(gp);
      int64_t now_used = dap->segment_used();
      assert(now_used > used);
      assert(now_used <= mysz);

      // test some UNSPECIFIED behaviors of the allocator
      auto gp2 = dap->allocate<double>(1);
      assert(gp2);
      int64_t now_used2 = dap->segment_used();
      assert(now_used2 > now_used);
      assert(now_used2 <= mysz);
      dap->deallocate(gp2);
      int64_t now_used3 = dap->segment_used();
      assert(now_used3 >= now_used); // may be > due to block merging with existing allocations

      auto gp3 = dap->allocate<double>(1);
      assert(gp3);
      int64_t now_used4 = dap->segment_used();
      assert(now_used4 == now_used2);
      assert(now_used4 <= mysz);
      dap->deallocate(gp3);
      int64_t now_used5 = dap->segment_used();
      assert(now_used5 == now_used3); // merging should not happen in this case

      used = now_used5;
    }
  } catch (std::bad_alloc const &e) {
    say() << "ERROR: Caught unexpected exception: \n" << e.what();
  }

  assert(dap);
  assert(dap->is_active()); assert(dev.is_active());
  dev.destroy();
  assert(!dap->is_active()); assert(!dev.is_active());
  assert(dap->segment_size() == 0);
  assert(dap->segment_used() == 0);
  delete dap;
  
  print_test_success();

  upcxx::finalize();
}
