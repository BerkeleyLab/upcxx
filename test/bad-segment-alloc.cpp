#include <upcxx/upcxx.hpp>
#include <iostream>
#include "util.hpp"

using namespace upcxx;

#if !UPCXX_CUDA_ENABLED
#error "This example requires UPC++ to be built with CUDA support."
#endif

// demonstrate std::bad_alloc exception behavior on device memory exhaustion
int main() {
  upcxx::init();

  print_test_header();
  size_t me = upcxx::rank_me(); 
  size_t billion = 1000000000ULL;
  size_t trillion = billion*1000ULL;

  cuda_device dev(0);
  device_allocator<cuda_device> *dap = nullptr;
  assert(dev.is_active());

  try {
    if (!me) say("") << "Making an absurd segment request on some ranks...";
    barrier();
    // even ranks request 1MB
    // odd ranks request roughly rank TB
    size_t mysz;
    if (me % 2 == 0) mysz = 1<<20;
    else             mysz = me*trillion; 
    assert(mysz > 0);
    dap = new device_allocator<cuda_device>(dev, mysz);
    say() << "ERROR:  device_allocator<cuda_device> failed to throw exception!";
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
    size_t mysz = 1<<20; 
    assert(mysz > 0);
    dap = new device_allocator<cuda_device>(dev, mysz);
    assert(dap);
  } catch (std::bad_alloc const &e) {
    say() << "ERROR: Caught unexpected exception: \n" << e.what();
  }

  assert(dap);
  assert(dap->is_active());
  delete dap;
  assert(dev.is_active());
  dev.destroy();
  
  print_test_success();

  upcxx::finalize();
}
