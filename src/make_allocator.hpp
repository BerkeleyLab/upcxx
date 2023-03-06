#ifndef _03c39657_6fd1_43b5_a7b0_5361274cac91
#define _03c39657_6fd1_43b5_a7b0_5361274cac91

#include <upcxx/device_allocator.hpp>
#include <upcxx/cuda.hpp>
#include <upcxx/hip.hpp>
#include <upcxx/ze.hpp>
#include <upcxx/team.hpp>

namespace upcxx {

#if   UPCXX_GPU_DEFAULT_DEVICE_CUDA // per-TU user override
  using gpu_default_device = cuda_device;
#elif UPCXX_GPU_DEFAULT_DEVICE_HIP  // per-TU user override
  using gpu_default_device = hip_device;
#elif UPCXX_GPU_DEFAULT_DEVICE_ZE  // per-TU user override
  using gpu_default_device = ze_device;
#elif UPCXXI_HIP_ENABLED
  // HIP higher priority than CUDA so that when user has configured HIP-over-CUDA support
  // (which also requires CUDA) we default to HIP which is more likely what they want
  using gpu_default_device = hip_device;
#elif UPCXXI_ZE_ENABLED
  using gpu_default_device = ze_device;
#elif UPCXXI_CUDA_ENABLED
  using gpu_default_device = cuda_device;
#else // no GPU support
  using gpu_default_device = cuda_device;
#endif

  using gpu_heap_allocator = device_allocator<gpu_default_device>;

  template<typename Device = gpu_default_device>
  device_allocator<Device> make_gpu_allocator(size_t size,
                                              typename Device::id_type device_id=Device::auto_device_id,
                                              void *base = nullptr) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_ALWAYS_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXX_ASSERT_ALWAYS(base == nullptr || device_id != Device::auto_device_id,
                         "make_gpu_allocator may not provide a device memory pointer with Device::auto_device_id");

    if (device_id == Device::auto_device_id) {
      typename Device::id_type dev_n = Device::device_n();
      if (dev_n == 0) { // no devices -> inactive
        device_id = Device::invalid_device_id;
      } else {
        // Assume local_team() processes share all visible GPUs, and do our best to distribute the load.
        // If processes can actually only see disjoint (equivalent) GPUs, then this still yields a reasonable choice.
        static typename Device::id_type next_dev =
           local_team().rank_me() * ( (dev_n + local_team().rank_n() - 1) / local_team().rank_n() );
        device_id = (next_dev % dev_n);
        next_dev = device_id + 1;
      }
    }

    return device_allocator<Device>(detail::internal_only(), device_id, size, base);
  }

} // namespace

#endif
