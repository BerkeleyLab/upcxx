#ifndef _1c3c7029_0525_47d5_b67d_48d7e2dba80a
#define _1c3c7029_0525_47d5_b67d_48d7e2dba80a

#include <upcxx/backend_fwd.hpp>
#include <upcxx/intru_queue.hpp>
#include <upcxx/memory_kind.hpp>

#include <utility>

#if UPCXXI_GEX_MK_CUDA \
 || UPCXXI_GEX_MK_HIP \
 || UPCXXI_GEX_MK_ZE // || ...
  #define UPCXXI_GEX_MK_ANY 1 // true iff ANY memory kind is using GASNet MK
#else
  #undef  UPCXXI_GEX_MK_ANY
#endif
#if (!UPCXXI_CUDA_ENABLED || UPCXXI_GEX_MK_CUDA) \
 && (!UPCXXI_HIP_ENABLED  || UPCXXI_GEX_MK_HIP) \
 && (!UPCXXI_ZE_ENABLED   || UPCXXI_GEX_MK_ZE) // && ...
  #define UPCXXI_GEX_MK_ALL 1 // true iff ALL memory kinds are using GASNet MK
#else
  #undef  UPCXXI_GEX_MK_ALL
#endif

namespace upcxx {
namespace detail { 
  class device;
  struct device_allocator_base; 
}
namespace backend {

  // backend::heap_state: base class for managing state
  // associated with a particular dynamically created device heap,
  // and the global table of such states, indexed by heap_idx
  class heap_state {
  
  private: // native[0] heaps correspond to GASNet EPs, reference[1] heaps do not.
  #if !UPCXXI_MANY_KINDS // no device support
    static constexpr int max_heaps_cat[2] = { 1, 0 };
  #elif UPCXXI_GEX_MK_ANY && UPCXXI_MAXEPS > 1
    static constexpr int max_heaps_cat[2] = { UPCXXI_MAXEPS, UPCXXI_MAXEPS };
  #else
    static constexpr int max_heaps_cat[2] = { 1, 32};
  #endif

  // object state:
  public:   detail::device *device_base;
  public:   detail::device_allocator_base *alloc_base;
  private:  memory_kind const my_kind;

  // static state:
  public: 
    static constexpr int max_heaps = max_heaps_cat[0] + max_heaps_cat[1];
    static_assert(max_heaps >= 1, "bad value of max_heaps");

  private:
    static heap_state *heaps[max_heaps];
    static int heap_count[2];

  public:
    heap_state(memory_kind k) : 
      device_base(nullptr), alloc_base(nullptr), my_kind(k) {}
    memory_kind kind() { return my_kind; }

    static void init();
    static int alloc_index(bool uses_gex_mk) {
      // currently we do not recycle heap_idx when using GASNet memory kinds,
      // until GASNet grows the ability to recycle endpoints
      const bool recycle = !uses_gex_mk;
      const int cat = !uses_gex_mk;

      UPCXX_ASSERT_ALWAYS(heap_count[cat] < max_heaps_cat[cat], "exceeded max device opens: " << max_heaps_cat[cat]);
      int idx;
      if (recycle) {
        int base = (uses_gex_mk ? 1 : max_heaps_cat[0]);
        int lim = base + max_heaps_cat[cat];
        for (idx=base; idx < lim; idx++) {
          if (!heaps[idx]) break;
        }
        UPCXX_ASSERT(idx > 0 && idx < lim);
      } else {
        idx = heap_count[cat];
      }
      UPCXX_ASSERT_ALWAYS(idx < max_heaps && heaps[idx] == nullptr, "internal error on heap creation");
      heap_count[cat]++;
      return idx;
    }
    static void free_index(int heap_idx) {
      UPCXX_ASSERT_ALWAYS(heap_idx > 0 && heap_idx < max_heaps, "invalid free_index: " << heap_idx);
      const bool uses_gex_mk = (heap_idx < max_heaps_cat[0]);
      const int cat = !uses_gex_mk;
      const bool recycle = !uses_gex_mk;
      UPCXX_ASSERT_ALWAYS(heaps[heap_idx] == nullptr && heap_count[cat] > 0, "internal error on heap destruction");
      if (recycle) heap_count[cat]--;
    }

    // retrieve reference to heap_state pointer at heap_idx, with bounds-checking
    static inline heap_state *&get(std::int32_t heap_idx, bool allow_null = false) {
      UPCXX_ASSERT(heap_count[0] <= max_heaps_cat[0] && heap_count[1] <= max_heaps_cat[1]);
      UPCXX_ASSERT(heap_idx > 0 && heap_idx < max_heaps, "invalid heap_idx (corrupted global_ptr?)");
      heap_state *&hs = heaps[heap_idx];
      UPCXX_ASSERT(hs || allow_null, "heap_idx referenced a null heap");
      UPCXX_ASSERT(!hs || (hs->kind() != memory_kind::host && hs->kind() < memory_kind::any), "invalid kind in heap_state");
      return hs;
    }
  };

  // device_cb: class that encapsulates one type-erased in-flight device event for
  // enqueing and holds the callback to be executed upon completion
  struct device_cb {
    detail::intru_queue_intruder<device_cb> intruder;
    void *event;
    heap_state *hs;
    #if UPCXXI_ZE_ENABLED
      void *extra;
    #endif
    virtual void execute_and_delete() = 0;
  };

  template<typename Fn>
  struct device_cb_fn final: device_cb {
    Fn fn;
    device_cb_fn(const Fn &f): fn(f) {}
    device_cb_fn(Fn &&f): fn(std::move(f)) {}
    void execute_and_delete() {
      fn();
      delete this;
    }
  };

  template<typename Fn>
  device_cb_fn<typename std::remove_reference<Fn>::type>*
  make_device_cb(Fn &&fn) {
    return new device_cb_fn<typename std::remove_reference<Fn>::type>(std::forward<Fn>(fn));
  }

  // This type is contained within `__thread` storage, so it must be:
  //   1. trivially destructible.
  //   2. constexpr constructible equivalent to zero-initialization.
  struct persona_device_state {
  #if UPCXXI_CUDA_ENABLED
    struct persona_cuda_state {
      // queue of pending events
      detail::intru_queue< device_cb, detail::intru_queue_safety::none,
                           &device_cb::intruder > cbs;
    } cuda;
  #endif
  #if UPCXXI_HIP_ENABLED
    struct persona_hip_state {
      // queue of pending events
      detail::intru_queue< device_cb, detail::intru_queue_safety::none,
                           &device_cb::intruder > cbs;
    } hip;
  #endif
  #if UPCXXI_ZE_ENABLED
    struct persona_ze_state {
      // queue of pending events
      detail::intru_queue< device_cb, detail::intru_queue_safety::none,
                           &device_cb::intruder > cbs;
    } ze;
  #endif
  };

} // namespace backend

namespace detail { // device is unspecified (for now)
class device {
 protected:
  // internal state:
  int heap_idx_; // -1 == inactive
  const memory_kind kind_;

  // methods:
  device(detail::internal_only, memory_kind kind) : 
    heap_idx_(-1), kind_(kind) {};
  device(device const&) = delete;
  device(device&& other) :
    device(detail::internal_only{}, other.kind_) {
    *this = std::move(other);
  }
  device& operator=(device&& other) {
    if (&other == this) return *this; // see issue 547
    UPCXX_ASSERT(heap_idx_ == -1,
                 "Move assignment is only allowed an an inactive device");
    UPCXX_ASSERT(kind_ == other.kind_);
    heap_idx_ = other.heap_idx_;
    if (heap_idx_ >= 0) {
      backend::heap_state *hs = backend::heap_state::get(heap_idx_);
      UPCXX_ASSERT(hs->device_base == &other);
      hs->device_base = this; // update registration
      other.heap_idx_ = -1; // deactivate
    }
    return *this;
  }

  template<typename Device>
  static typename Device::id_type heap_idx_to_device_id(int heap_idx);

  virtual std::string kind_info_dispatch() const = 0;

 public:
  memory_kind kind() const { return kind_; }
  /*virtual*/ bool is_active() const { return heap_idx_ >= 0; }

  std::string kind_info() const { return this->kind_info_dispatch(); }

  virtual void destroy(upcxx::entry_barrier eb = entry_barrier::user) = 0;

  virtual ~device() { 
    if(backend::init_count > 0) { // we don't assert on leaks after finalization
      UPCXX_ASSERT_ALWAYS(!is_active(), "An active upcxx::" << detail::to_string(kind_)
                           << " must have destroy() called before destructor.");
    } 
  }
}; // device
} // namespace detail

class gpu_device : public detail::device {
 protected:
  // factored internal state:
  int device_id_;

  // factored static goop:
  template<typename T>
  using pointer = T*;
  using id_type = int;  

  static constexpr id_type invalid_device_id = -1;
  static constexpr id_type auto_device_id = -2;

  template<typename T>
  static constexpr T* null_pointer() { return nullptr; }

  // factored methods:
  id_type device_id() const { return device_id_; }

  gpu_device(detail::internal_only, id_type device_id, memory_kind kind) : 
     device(detail::internal_only(), kind), device_id_(device_id) {}
  gpu_device(gpu_device const&) = delete;
  gpu_device(gpu_device&& other) :
    device(std::move(other)), device_id_(other.device_id_) {
    other.device_id_ = invalid_device_id;
  }
  gpu_device& operator=(gpu_device&& other) {
    if (&other == this) return *this; // see issue 547
    device::operator=(std::move(other));
    device_id_ = other.device_id_;
    other.device_id_ = invalid_device_id;
    return *this;
  }

  // computes Device::default_alignment<T> without a static type T
  static constexpr std::size_t 
  default_alignment_erased(std::size_t sizeof_T, std::size_t alignof_T,
                           std::size_t normal_align) {
    return alignof_T < normal_align ? normal_align : alignof_T;
  }

};

} // namespace upcxx
#endif
