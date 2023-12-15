#ifndef _8b851191_7faf_4677_a1aa_3f070dfa7b91
#define _8b851191_7faf_4677_a1aa_3f070dfa7b91

#include <upcxx/backend_fwd.hpp>
#include <upcxx/device_fwd.hpp>
#include <upcxx/device_allocator.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstdint>

#if UPCXXI_ZE_ENABLED
  // feature macro: ONLY changes when a new spec is officially released that alters ZE feature
  #define UPCXX_KIND_ZE 202309L
#else
  #undef UPCXX_KIND_ZE
#endif

extern "C" {
  // these incomplete types mirror those used in Level Zero handle types
  struct _ze_driver_handle_t;
  struct _ze_device_handle_t;
  struct _ze_context_handle_t;
}

namespace upcxx {

  class ze_device final : public gpu_device {
    friend struct detail::device_allocator_core<ze_device>;
    friend class device_allocator<ze_device>;
    
  public:
    using gpu_device::id_type;
    using gpu_device::pointer; 
    using gpu_device::null_pointer;
    using gpu_device::invalid_device_id;
    using gpu_device::auto_device_id;
    using gpu_device::device_id;
    
    // opaque types mirroring the Level Zero types of similar name
    typedef struct ::_ze_context_handle_t *context_handle_t;
    typedef struct ::_ze_driver_handle_t  *driver_handle_t;
    typedef struct ::_ze_device_handle_t  *device_handle_t;
    
    static constexpr memory_kind kind = memory_kind::ze_device;

    ze_device() : gpu_device(detail::internal_only(), invalid_device_id,
                               memory_kind::ze_device) {}
    ze_device(id_type device_id);
    ze_device(ze_device const&) = delete;
    ze_device(ze_device&& other) : gpu_device(std::move(other)) {}
    ze_device& operator=(ze_device&& other) = default;

    static id_type device_n();

    template<typename T>
    static constexpr std::size_t default_alignment() {
      return default_alignment_erased(sizeof(T), alignof(T), normal_alignment);
    }
    
    static std::string kind_info();
    static std::string uuid(id_type);

    void destroy(upcxx::entry_barrier eb = entry_barrier::user) override;

    static constexpr bool use_gex_mk(detail::internal_only) {
      #if UPCXXI_GEX_MK_ZE
        return true;
      #else
        return false;
      #endif
    }

    // device_id mapping to/from Level Zero ze_{device,driver}_handle_t
    static device_handle_t device_id_to_device_handle(id_type device_id);
    static driver_handle_t device_id_to_driver_handle(id_type device_id);
    static id_type         device_handle_to_device_id(device_handle_t device_handle);

    // Level Zero Driver Context (ze_context_handle_t) control:
    static context_handle_t get_driver_context(driver_handle_t driver_handle);
    static context_handle_t get_driver_context(device_handle_t device_handle = nullptr);
    static void             set_driver_context(context_handle_t context_handle, 
                                               driver_handle_t driver_handle);
    static void             set_driver_context(context_handle_t context_handle, 
                                               device_handle_t device_handle = nullptr);

  private:
    std::string kind_info_dispatch() const override {
      return kind_info();
    }
    static constexpr int min_alignment = 16;
    static constexpr int normal_alignment = 256;
  };

  namespace detail {
    template<>
    struct device_allocator_core<ze_device>: device_allocator_base {

      device_allocator_core() {}
      device_allocator_core(ze_device &dev, void *base, std::size_t size);
      device_allocator_core(device_allocator_core&&) = default;
      device_allocator_core& operator=(device_allocator_core&&) = default;
      ~device_allocator_core() { release(); }
      void release();
    };

    #if UPCXXI_ZE_ENABLED
      extern void ze_copy_local(int heap_d, void *buf_d, int heap_s, void const *buf_s, 
                                  std::size_t size, backend::device_cb *cb);
    #endif

  } // namespace detail
}
#endif
