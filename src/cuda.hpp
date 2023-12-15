#ifndef _62341dee_845f_407c_9241_cd36da9f0e1c
#define _62341dee_845f_407c_9241_cd36da9f0e1c

#include <upcxx/backend_fwd.hpp>
#include <upcxx/device_fwd.hpp>
#include <upcxx/device_allocator.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstdint>

#if UPCXXI_CUDA_ENABLED
  // feature macro: ONLY changes when a new spec is officially released that alters CUDA feature
  #define UPCXX_KIND_CUDA 202309L
#else
  #undef UPCXX_KIND_CUDA
#endif

namespace upcxx {

  class cuda_device final : public gpu_device {
    friend struct detail::device_allocator_core<cuda_device>;
    friend class device_allocator<cuda_device>;
    
  public:
    using gpu_device::id_type;
    using gpu_device::pointer; 
    using gpu_device::null_pointer;
    using gpu_device::invalid_device_id;
    using gpu_device::auto_device_id;
    using gpu_device::device_id;
    
    static constexpr memory_kind kind = memory_kind::cuda_device;

    cuda_device() : gpu_device(detail::internal_only(), invalid_device_id,
                               memory_kind::cuda_device) {}
    cuda_device(id_type device_id);
    cuda_device(cuda_device const&) = delete;
    cuda_device(cuda_device&& other) : gpu_device(std::move(other)) {}
    cuda_device& operator=(cuda_device&& other) = default;

    static id_type device_n();

    template<typename T>
    static constexpr std::size_t default_alignment() {
      return default_alignment_erased(sizeof(T), alignof(T), normal_alignment);
    }

    static std::string kind_info();
    static std::string uuid(id_type);

    void destroy(upcxx::entry_barrier eb = entry_barrier::user) override;

    static constexpr bool use_gex_mk(detail::internal_only) {
      #if UPCXXI_GEX_MK_CUDA
        return true;
      #else
        return false;
      #endif
    }

  private:
    std::string kind_info_dispatch() const override {
      return kind_info();
    }
    static constexpr int min_alignment = 16;
    static constexpr int normal_alignment = 256;
  };

  namespace detail {
    template<>
    struct device_allocator_core<cuda_device>: device_allocator_base {

      device_allocator_core() {}
      device_allocator_core(cuda_device &dev, void *base, std::size_t size);
      device_allocator_core(device_allocator_core&&) = default;
      device_allocator_core& operator=(device_allocator_core&&) = default;
      ~device_allocator_core() { release(); }
      void release();
    };

    #if UPCXXI_CUDA_ENABLED
      extern void cuda_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s, 
                                  std::size_t size, backend::device_cb *cb);
    #endif

  } // namespace detail
}
#endif
