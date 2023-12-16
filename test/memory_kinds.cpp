#include <stddef.h>
#include <type_traits>
#include <iostream>
#include <functional>
#include <vector>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

using namespace upcxx;

volatile bool cuda_enabled;
volatile bool hip_enabled;
volatile bool ze_enabled;

std::vector<std::function<void()>> post_fini;

#define HAVE_KIND_INFO (UPCXX_VERSION >= 20220905)
#define HAVE_UUID      (UPCXX_VERSION >= 20230307)

template<typename Device>
void run_test(typename Device::id_type id, std::size_t heap_size) {

  using Allocator = upcxx::device_allocator<Device>;
  assert_same<typename Allocator::device_type, Device>();
  using id_type = typename Device::id_type;
  using gp_type = global_ptr<int, Device::kind>;
  using gp_any = global_ptr<int, memory_kind::any>;
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
    gpu_device *gdi = &di;
    assert(!di.is_active()); assert(!gdi->is_active());
    assert(di.device_id() == id_invalid);
    assert(gdi->kind() == Device::kind);
    Device di2(std::move(di));
    gpu_device *gdi2 = &di2;
    assert(!di.is_active()); assert(!gdi->is_active());
    assert(di.device_id() == id_invalid);
    assert(!di2.is_active()); assert(!gdi2->is_active());
    assert(di2.device_id() == id_invalid);
    #if HAVE_KIND_INFO
      std::string kind_info = gdi->kind_info();
      assert(kind_info.size() > 0);
      std::string kind_info2 = di.kind_info();
      assert(kind_info2.size() > 0);
    #endif

    Allocator ai = Allocator();
    heap_allocator *gai = &ai;
    assert(!ai.is_active()); assert(!gai->is_active());
    assert(ai.kind == Device::kind);
    Allocator ai2(std::move(ai));
    heap_allocator *gai2 = &ai2;
    assert(gai2->kind() == Device::kind);
    assert(!ai.is_active());
    assert(!ai2.is_active()); assert(!gai2->is_active());
    assert(Allocator::local(gp_null) == dp_null);
    assert(gp_null.is_local());
    assert(gp_null.local() == nullptr);
    assert(ai.device_id() == id_invalid);
    assert(Allocator::device_id(gp_null) == id_invalid);
    assert(ai.to_global_ptr(dp_null) == gp_null);
    ai.deallocate(gp_null);

    if (i < 18) { // sometimes explicit destroy
      entry_barrier lev;
      switch (i % 3) { // with varying eb
        case 0: lev = entry_barrier::user; break;
        case 1: lev = entry_barrier::internal; break;
        case 2: lev = entry_barrier::none; break;
      }
      if (i < 3)       di.destroy(lev);
      else if (i < 6)  di2.destroy(lev);
      else if (i < 9)  gdi->destroy(lev);
      else if (i < 12) gdi2->destroy(lev);
      else if (i < 15)  gai->destroy(lev);
      else if (i < 18) gai2->destroy(lev);
    }
  }

  #if UPCXX_VERSION >= 20220907
    auto desc = Device::kind;
  #else
    auto desc = std::string("MK:") + std::to_string((int)Device::kind);
  #endif
  int n_dev = Device::device_n();
  assert(n_dev >= 0);
  upcxx::barrier();
  { say s;
    s << "Testing " << n_dev << " " << desc << " GPUs\n";
    #if HAVE_UUID
      for (int i = 0; i < n_dev; i++) {
        s << "    UUID " << std::setw(2) << i << ": " << Device::uuid(i) << "\n";
      }
    #endif
  }
  int low = reduce_all(Device::device_n(), op_fast_min).wait();
  if (low == 0) {
    if (!rank_me())
      say("") << "WARNING: Some ranks lack a " << desc << " GPU. Active tests skipped.";
    return;
  }

  // deliberately create three heaps on the same device
  // managed via pointer for precision testing of destruction
  Device *d0 = new Device(id);
  gpu_device *gd0 = d0;
  Allocator *a0 = new Allocator(*d0, heap_size);
  heap_allocator *ga0 = a0;
  assert(d0->is_active()); assert(gd0->is_active()); 
  assert(a0->is_active()); assert(ga0->is_active()); 
  assert(d0->device_id() == id);
  assert(a0->device_id() == id);

  bool have1 = rank_me()%2;
  Device *d1 = new Device(have1?id:id_invalid);
  gpu_device *gd1 = d1;
  Allocator *a1 = new Allocator(*d1, heap_size);
  heap_allocator *ga1 = a1;
  assert(d1->is_active() == have1); assert(gd1->is_active() == have1); 
  assert(a1->is_active() == have1); assert(ga1->is_active() == have1);
  assert(d1->device_id() == (have1?id:id_invalid));
  assert(a1->device_id() == (have1?id:id_invalid));
  if (have1 && rank_me()%3) { // test moving an active device
    Device *d1a = new Device(std::move(*d1));
    assert(!d1->is_active());
    delete d1;
    d1 = d1a;
    gd1 = d1a;
    assert(d1->is_active()); assert(gd1->is_active()); 
    assert(a1->is_active()); assert(ga1->is_active());
  }

  bool have2 = !(rank_me()%2);
  Device *d2 = new Device(have2?id:id_invalid);
  gpu_device *gd2 = d2;
  Allocator *a2 = new Allocator(*d2, heap_size);
  heap_allocator *ga2 = a2;
  assert(d2->is_active() == have2); assert(gd2->is_active() == have2); 
  assert(a2->is_active() == have2); assert(ga2->is_active() == have2);
  assert(d2->device_id() == (have2?id:id_invalid));
  assert(a2->device_id() == (have2?id:id_invalid));
  if (have2 && rank_me()%3) { // test moving an active allocator
    Allocator *a2a = new Allocator(std::move(*a2));
    assert(!a2->is_active()); assert(!ga2->is_active());
    delete a2;
    a2 = a2a;
    ga2 = a2a;
    assert(d2->is_active()); assert(gd2->is_active()); 
    assert(a2->is_active()); assert(ga2->is_active());
  }

  // and a device with no heap
  Device *d3 = new Device(id);
  gpu_device *gd3 = d3;
  assert(d3->is_active()); assert(gd3->is_active());
  assert(d3->device_id() == id);

  // allocate some objects
  auto alloc_check = [=](Allocator *a, gp_any gpa) {
    assert(gpa);
    assert(gpa.dynamic_kind() == Device::kind);
    gp_type gp = static_kind_cast<Device::kind>(gpa);
    assert(gp);
    assert(a->is_active());
    dp_type dp = Allocator::local(gp);
    assert(dp != dp_null);
    size_t align = Device::template default_alignment<int>();
    assert((uintptr_t)dp % align == 0);
    gp_type gp1 = a->to_global_ptr(dp);
    assert(gp == gp1);
    assert(Allocator::device_id(gp) == a->device_id());
    assert(gp.dynamic_kind() == gpa.dynamic_kind());
    assert(gp == dynamic_kind_cast<Device::kind>(gpa));
    if (Device::kind != memory_kind::host)
      assert(!gp.is_local()); // unspecified, but true for all current devices
    #if TEST_ISSUE464
      auto invalid = gp.local(); // should assert in debug mode
    #endif
    return gp;
  };
  gp_type gp0 = alloc_check(a0,a0->template allocate<int>(1)); 
  gp_type gp1 = nullptr;
  if (have1) gp1 = alloc_check(a1,a1->template allocate<int>(10)); 
  gp_type gp2 = nullptr;
  if (have2) gp2 = alloc_check(a2,a2->template allocate<int>(20)); 
  assert(gp0 != gp1); assert(gp0 != gp2);

  a0->deallocate(gp0);
  a1->deallocate(gp1);
  //a2->deallocate(gp2); // deliberate leak

  { // exercise heap_allocator memory management
    { 
      gp_any gpa = alloc_check(a0,a0->template allocate<int>(1));
      a0->deallocate(gpa);
    }
    { 
      gp_any gpa = alloc_check(a0,a0->template allocate<int>(1));
      ga0->deallocate(gpa);
    }
    { 
      gp_any gpa = alloc_check(a0,ga0->template allocate<int>(1));
      a0->deallocate(gpa);
    }
    { 
      gp_any gpa = alloc_check(a0,ga0->template allocate<int>(1));
      ga0->deallocate(gpa);
    }
  }

  d0->destroy(); // normal destruction
  assert(!d0->is_active()); assert(!gd0->is_active());
  assert(!a0->is_active()); assert(!ga0->is_active());
  delete d0;
  delete ga0;

  ga1->destroy(); // destroy via heap_allocator
  assert(!d1->is_active()); assert(!gd1->is_active());
  assert(!a1->is_active()); assert(!ga1->is_active());
  delete a1;  
  delete gd1;

  d3->destroy(); // destroy with no allocator
  assert(!d3->is_active()); assert(!gd3->is_active());
  delete d3;

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
    say() << "ERROR: Failed to generate device bad_alloc exn!" << a4;
  } catch (std::bad_alloc &e) {
    say() << "got expected exn: " << e.what();
  }
  assert(d4->is_active());
  post_fini.push_back(std::function<void()>([=]() {
    delete d4;
  }));

  upcxx::barrier();
 
  // more exhaustive test of various paths to destroy
  for (int i=0; i < 4; i++) {
    bool have = (i ? !(rank_me()%2) : true);
    Device *dx = new Device(have?id:id_invalid);
    gpu_device *gdx = dx;
    Allocator *ax = new Allocator(*dx, heap_size);
    heap_allocator *gax = ax;
    assert(dx->is_active() == have); assert(gdx->is_active() == have); 
    assert(ax->is_active() == have); assert(gax->is_active() == have);
    assert(dx->device_id() == (have?id:id_invalid));
    assert(ax->device_id() == (have?id:id_invalid));
    if (i > 1 && have && rank_me()%3) {
      // move active allocator
      Allocator *axa = new Allocator(std::move(*ax));
      assert(!ax->is_active()); assert(!gax->is_active());
      delete ax;
      ax = axa;
      gax = axa;
      assert(dx->is_active()); assert(gdx->is_active()); 
      assert(ax->is_active()); assert(gax->is_active());
      if (i > 2) {
        // move active device
        Device *dxa = new Device(std::move(*dx));
        assert(!dx->is_active()); assert(!gdx->is_active()); 
        delete dx;
        dx = dxa;
        gdx = dxa;
      }
      assert(dx->is_active()); assert(gdx->is_active()); 
      assert(ax->is_active()); assert(gax->is_active());
    }
    if (have) { // exercise heap_allocator memory management
      ax->deallocate(alloc_check(ax,ax->template allocate<int>(1)));
      gax->deallocate(alloc_check(ax,ax->template allocate<int>(1)));
      ax->deallocate(alloc_check(ax,gax->template allocate<int>(1)));
      gax->deallocate(alloc_check(ax,gax->template allocate<int>(1)));
      alloc_check(ax,ax->template allocate<int>(1));  // deliberate leak
      alloc_check(ax,gax->template allocate<int>(1)); // deliberate leak
    }
    switch (i) {
      case 0: dx->destroy(); break;
      case 1: gdx->destroy(); break;
      case 2: ax->destroy(); break;
      case 3: gax->destroy(); break;
      default: assert(0);
    }
    assert(!dx->is_active()); assert(!gdx->is_active()); 
    assert(!ax->is_active()); assert(!gax->is_active());
    if (i%2) {
      delete dx;
      delete gax;
    } else {
      delete gdx;
      delete ax;
    }

    upcxx::barrier();

    { // test make_gpu_allocator
      Allocator inv = make_gpu_allocator<Device>(heap_size, id_invalid);
      assert(!inv.is_active());

      Allocator az = make_gpu_allocator<Device>(heap_size);
      assert(az.is_active()); 
      int azid = az.device_id();
      assert(azid >= 0);
      say() << "make_gpu_allocator<"<<desc<<">(): auto device_id=" << az.device_id();
      { Allocator az2 = std::move(az); // test move
        assert(!az.is_active()); 
        assert(az2.is_active()); assert(az2.device_id() == azid);
        az2.deallocate(alloc_check(&az2,az2.template allocate<int>(1)));
        az2.destroy();
      }
    }

    upcxx::barrier();
  }
}

int main() {
  upcxx::init();
  print_test_header();
  int me = upcxx::rank_me();

  // check that required device members exist with sane-looking values
  // note these should be defined even when HIP kind is disabled
  assert_same<hip_device::id_type, int>();
  assert_same<hip_device::pointer<double>, double *>();
  assert(hip_device::null_pointer<double>() == nullptr);
  assert(hip_device::default_alignment<double>() > 0);
  assert(hip_device::kind == memory_kind::hip_device);
  assert(hip_device::invalid_device_id != 0);
  if (me&1) assert(hip_device::device_n() >= 0);
  #if HAVE_KIND_INFO
    auto hip_info = hip_device::kind_info();
    if (!me) say() << "hip_device::kind_info():\n" << hip_info;
    assert(hip_info.size() > 0);
  #endif
  assert(hip_device::device_n() >= 0);
  #if UPCXX_KIND_HIP
    hip_enabled = true;
  #endif
  if (hip_enabled) { 
    run_test<hip_device>(0, 2<<20);
  }

  // check that required device members exist with sane-looking values
  // note these should be defined even when CUDA kind is disabled
  assert_same<cuda_device::id_type, int>();
  assert_same<cuda_device::pointer<double>, double *>();
  assert(cuda_device::null_pointer<double>() == nullptr);
  assert(cuda_device::default_alignment<double>() > 0);
  assert(cuda_device::kind == memory_kind::cuda_device);
  assert(cuda_device::invalid_device_id != 0);
  if (me&1) assert(cuda_device::device_n() >= 0);
  #if HAVE_KIND_INFO
    auto cuda_info = cuda_device::kind_info();
    if (!me) say() << "cuda_device::kind_info():\n" << cuda_info;
    assert(cuda_info.size() > 0);
  #endif
  assert(cuda_device::device_n() >= 0);
  #if UPCXX_KIND_CUDA
    cuda_enabled = true;
  #endif
  if (cuda_enabled) { 
    run_test<cuda_device>(0, 2<<20);
  }

  // check that required device members exist with sane-looking values
  // note these should be defined even when ZE kind is disabled
  assert_same<ze_device::id_type, int>();
  assert_same<ze_device::pointer<double>, double *>();
  assert(ze_device::null_pointer<double>() == nullptr);
  assert(ze_device::default_alignment<double>() > 0);
  assert(ze_device::kind == memory_kind::ze_device);
  assert(ze_device::invalid_device_id != 0);
  if (me&1) assert(ze_device::device_n() >= 0);
  #if HAVE_KIND_INFO
    auto ze_info = ze_device::kind_info();
    if (!me) say() << "ze_device::kind_info():\n" << ze_info;
    assert(ze_info.size() > 0);
  #endif
  assert(ze_device::device_n() >= 0);

  // test ze_device-specific accessors
  std::unordered_map<ze_device::device_handle_t, ze_device::id_type> device_to_id;
  std::unordered_map<ze_device::driver_handle_t, ze_device::context_handle_t> driver_to_context;
  for (ze_device::id_type id = 0; id < ze_device::device_n(); id++) {
    ze_device::device_handle_t zeDevice = ze_device::device_id_to_device_handle(id);
    ze_device::driver_handle_t zeDriver = ze_device::device_id_to_driver_handle(id);
    assert(zeDevice && zeDriver);
    assert(device_to_id.count(zeDevice) == 0);
    device_to_id[zeDevice] = id;
    ze_device::id_type qid = ze_device::device_handle_to_device_id(zeDevice);
    assert(qid == id);
    ze_device::context_handle_t zeContext = ze_device::get_driver_context(zeDriver);
    assert(zeContext);
    if (driver_to_context.count(zeDriver) > 0) {
      assert(driver_to_context[zeDriver] == zeContext);
    } else {
      driver_to_context[zeDriver] = zeContext;
    }
    ze_device::set_driver_context(zeContext, zeDriver);
    assert(zeContext == ze_device::get_driver_context(zeDriver));
    ze_device::set_driver_context(zeContext, zeDevice);
    assert(zeContext == ze_device::get_driver_context(zeDevice));
    if (id == 0) {
      assert(zeContext == ze_device::get_driver_context());
      ze_device::set_driver_context(zeContext);
      assert(zeContext == ze_device::get_driver_context());
    }
  }
  #if UPCXX_KIND_ZE
    ze_enabled = true;
  #endif
  if (ze_enabled) { 
    run_test<ze_device>(0, 2<<20);
  }

  {
    assert(gpu_heap_allocator::kind == gpu_default_device::kind);
    gpu_heap_allocator inv = make_gpu_allocator(0, gpu_default_device::invalid_device_id);
    assert(!inv.is_active());

    gpu_heap_allocator az = make_gpu_allocator(2<<20);
    say() << "make_gpu_allocator<default>(): auto device_id=" << az.device_id();
    if (gpu_default_device::device_n() > 0) {
      assert(az.is_active());
      assert(az.device_id() >= 0);
      auto gp = az.allocate<int>(1);
      assert(gp);
      assert(decltype(gp)::kind == gpu_default_device::kind);
      az.deallocate(gp);
    }
    az.destroy();
  }

  upcxx::finalize();

  for (auto &f : post_fini) {
    f();
  }

  if (!me) print_test_success();
}

