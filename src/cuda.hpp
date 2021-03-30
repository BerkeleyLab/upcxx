#ifndef _62341dee_845f_407c_9241_cd36da9f0e1c
#define _62341dee_845f_407c_9241_cd36da9f0e1c

#include <upcxx/backend_fwd.hpp>
#include <upcxx/cuda_fwd.hpp>
#include <upcxx/device_allocator.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstdint>

namespace upcxx {

  class cuda_device {
    friend struct detail::device_allocator_core<cuda_device>;
    friend class device_allocator<cuda_device>;
    int device_;
    int heap_idx_;
    
  public:
    template<typename T>
    using pointer = T*;
    using id_type = int;

    template<typename T>
    static constexpr T* null_pointer() { return nullptr; }
    
    static constexpr memory_kind kind = memory_kind::cuda_device;

    static constexpr id_type invalid_device_id = -1;

    cuda_device(int device = invalid_device_id);
    cuda_device(cuda_device const&) = delete;
    cuda_device(cuda_device&& other) : 
      device_(other.device_), heap_idx_(other.heap_idx_) {
      other.device_ = invalid_device_id; 
      other.heap_idx_ = -1;
    }
    ~cuda_device();

    int device_id() const { return device_; }
    bool is_active() const { return device_ != invalid_device_id; }

    template<typename T>
    static constexpr std::size_t default_alignment() {
      return alignof(T) < 256 ? 256 : alignof(T);
    }

    void destroy(upcxx::entry_barrier eb = entry_barrier::user);

  private:
    static id_type device_id(detail::internal_only, int heap_idx);
  };

  namespace detail {
    template<size_t val> 
    struct device_allocator_core_min_align {
      static constexpr std::size_t min_alignment = val;
    };
    template<size_t val>
    constexpr std::size_t device_allocator_core_min_align<val>::min_alignment; // see issue #333

    template<>
    struct device_allocator_core<cuda_device>: device_allocator_base, device_allocator_core_min_align<16> {

      device_allocator_core();
      device_allocator_core(cuda_device &dev, void *base, std::size_t size);
      device_allocator_core(device_allocator_core&&) = default;
      void destroy();
      ~device_allocator_core();
    };

  }
}
#endif
