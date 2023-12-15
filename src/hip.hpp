#ifndef _fb4d4046_9f23_43b8_9d94_fcb1c2aa0c85
#define _fb4d4046_9f23_43b8_9d94_fcb1c2aa0c85

#include <upcxx/backend_fwd.hpp>
#include <upcxx/device_fwd.hpp>
#include <upcxx/device_allocator.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstdint>

#if UPCXXI_HIP_ENABLED
  // feature macro: ONLY changes when a new spec is officially released that alters HIP feature
  #define UPCXX_KIND_HIP 202309L
#else
  #undef UPCXX_KIND_HIP
#endif

namespace upcxx {

  class hip_device final : public gpu_device {
    friend struct detail::device_allocator_core<hip_device>;
    friend class device_allocator<hip_device>;
    
  public:
    using gpu_device::id_type;
    using gpu_device::pointer;
    using gpu_device::null_pointer;
    using gpu_device::invalid_device_id;
    using gpu_device::auto_device_id;
    using gpu_device::device_id;
    
    static constexpr memory_kind kind = memory_kind::hip_device;

    hip_device() : gpu_device(detail::internal_only(), invalid_device_id,
                              memory_kind::hip_device) {}
    hip_device(id_type device_id);
    hip_device(hip_device const&) = delete;
    hip_device(hip_device&& other) : gpu_device(std::move(other)) {}
    hip_device& operator=(hip_device&& other) = default;

    static id_type device_n();

    template<typename T>
    static constexpr std::size_t default_alignment() {
      return default_alignment_erased(sizeof(T), alignof(T), normal_alignment);
    }

    static std::string kind_info();
    static std::string uuid(id_type);

    void destroy(upcxx::entry_barrier eb = entry_barrier::user) override;

    static constexpr bool use_gex_mk(detail::internal_only) {
      #if UPCXXI_GEX_MK_HIP
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
    struct device_allocator_core<hip_device>: device_allocator_base {

      device_allocator_core() {}
      device_allocator_core(hip_device &dev, void *base, std::size_t size);
      device_allocator_core(device_allocator_core&&) = default;
      device_allocator_core& operator=(device_allocator_core&&) = default;
      ~device_allocator_core() { release(); }
      void release();
    };

    #if UPCXXI_HIP_ENABLED
      extern void hip_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s, 
                                  std::size_t size, backend::device_cb *cb);
    #endif

  } // namespace detail
}
#endif
