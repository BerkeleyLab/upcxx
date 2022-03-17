#include "../util.hpp"

using namespace upcxx;

team t0;
atomic_domain<int> ad0;
cuda_device cd0;
hip_device hd0;
device_allocator<cuda_device> dacd0;
device_allocator<hip_device> dahd0;

int main() {
  init();
  print_test_header();

  UPCXX_ASSERT_ALWAYS(!t0.is_active());
  UPCXX_ASSERT_ALWAYS(!ad0.is_active());
  UPCXX_ASSERT_ALWAYS(!cd0.is_active());
  UPCXX_ASSERT_ALWAYS(!hd0.is_active());
  UPCXX_ASSERT_ALWAYS(!dacd0.is_active());
  UPCXX_ASSERT_ALWAYS(!dahd0.is_active());

  {
    team t1;
    team t2;
    t1 = std::move(t2);
    UPCXX_ASSERT_ALWAYS(!t1.is_active());
    UPCXX_ASSERT_ALWAYS(!t2.is_active());
    team t3 = world().split(rank_me(), 0);
    t1 = std::move(t3);
    UPCXX_ASSERT_ALWAYS(t1.is_active());
    UPCXX_ASSERT_ALWAYS(!t3.is_active());
    team t4 = std::move(t1);
    UPCXX_ASSERT_ALWAYS(t4.is_active());
    UPCXX_ASSERT_ALWAYS(!t1.is_active());
    t1 = std::move(t4);
    UPCXX_ASSERT_ALWAYS(t1.is_active());
    t1.destroy();
    UPCXX_ASSERT_ALWAYS(!t1.is_active());
    t2.destroy(); // allowed on inactive team
  }

  {
    atomic_domain<int> ad1;
    atomic_domain<int> ad2;
    ad1 = std::move(ad2);
    UPCXX_ASSERT_ALWAYS(!ad1.is_active());
    UPCXX_ASSERT_ALWAYS(!ad2.is_active());
    atomic_domain<int> ad3{{atomic_op::load}};
    ad1 = std::move(ad3);
    UPCXX_ASSERT_ALWAYS(ad1.is_active());
    UPCXX_ASSERT_ALWAYS(!ad3.is_active());
    atomic_domain<int> ad4 = std::move(ad1);
    UPCXX_ASSERT_ALWAYS(ad4.is_active());
    UPCXX_ASSERT_ALWAYS(!ad1.is_active());
    ad1 = std::move(ad4);
    UPCXX_ASSERT_ALWAYS(ad1.is_active());
    ad1.destroy();
    UPCXX_ASSERT_ALWAYS(!ad1.is_active());
    // ad2.destroy(); // NOT allowed on inactive atomic_domain
  }

  {
    cuda_device cd1;
    cuda_device cd2;
    cd1 = std::move(cd2);
    UPCXX_ASSERT_ALWAYS(!cd1.is_active());
    UPCXX_ASSERT_ALWAYS(!cd2.is_active());
    if (cuda_device::device_n() > 0) {
      cuda_device cd3{0};
      cd1 = std::move(cd3);
      UPCXX_ASSERT_ALWAYS(cd1.is_active());
      UPCXX_ASSERT_ALWAYS(!cd3.is_active());
      cuda_device cd4 = std::move(cd1);
      UPCXX_ASSERT_ALWAYS(cd4.is_active());
      UPCXX_ASSERT_ALWAYS(!cd1.is_active());
      cd1 = std::move(cd4);
      UPCXX_ASSERT_ALWAYS(cd1.is_active());
    }
    cd1.destroy();
    UPCXX_ASSERT_ALWAYS(!cd1.is_active());
    cd2.destroy(); // allowed on inactive cuda_device
  }

  {
    hip_device hd1;
    hip_device hd2;
    hd1 = std::move(hd2);
    UPCXX_ASSERT_ALWAYS(!hd1.is_active());
    UPCXX_ASSERT_ALWAYS(!hd2.is_active());
    if (hip_device::device_n() > 0) {
      hip_device hd3{0};
      hd1 = std::move(hd3);
      UPCXX_ASSERT_ALWAYS(hd1.is_active());
      UPCXX_ASSERT_ALWAYS(!hd3.is_active());
      hip_device hd4 = std::move(hd1);
      UPCXX_ASSERT_ALWAYS(hd4.is_active());
      UPCXX_ASSERT_ALWAYS(!hd1.is_active());
      hd1 = std::move(hd4);
      UPCXX_ASSERT_ALWAYS(hd1.is_active());
    }
    hd1.destroy();
    UPCXX_ASSERT_ALWAYS(!hd1.is_active());
    hd2.destroy(); // allowed on inactive hip_device
  }

  {
    device_allocator<cuda_device> da1;
    device_allocator<cuda_device> da2;
    da1 = std::move(da2);
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    UPCXX_ASSERT_ALWAYS(!da2.is_active());
    device_allocator<cuda_device> da3 =
      make_gpu_allocator<cuda_device>(4*1024*1024);
    bool active = da3.is_active();
    da1 = std::move(da3);
    UPCXX_ASSERT_ALWAYS(da1.is_active() == active);
    UPCXX_ASSERT_ALWAYS(!da3.is_active());
    device_allocator<cuda_device> da4 = std::move(da1);
    UPCXX_ASSERT_ALWAYS(da4.is_active() == active);
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    da1 = std::move(da4);
    UPCXX_ASSERT_ALWAYS(da1.is_active() == active);
    da1.destroy();
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    da2.destroy(); // allowed on inactive device_allocator
  }

  {
    device_allocator<hip_device> da1;
    device_allocator<hip_device> da2;
    da1 = std::move(da2);
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    UPCXX_ASSERT_ALWAYS(!da2.is_active());
    device_allocator<hip_device> da3 =
      make_gpu_allocator<hip_device>(4*1024*1024);
    bool active = da3.is_active();
    da1 = std::move(da3);
    UPCXX_ASSERT_ALWAYS(da1.is_active() == active);
    UPCXX_ASSERT_ALWAYS(!da3.is_active());
    device_allocator<hip_device> da4 = std::move(da1);
    UPCXX_ASSERT_ALWAYS(da4.is_active() == active);
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    da1 = std::move(da4);
    UPCXX_ASSERT_ALWAYS(da1.is_active() == active);
    da1.destroy();
    UPCXX_ASSERT_ALWAYS(!da1.is_active());
    da2.destroy(); // allowed on inactive device_allocator
  }

  print_test_success();
  finalize();
}
