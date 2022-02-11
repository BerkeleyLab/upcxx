#ifndef _1c3c7029_0525_47d5_b67d_48d7e2dba80a
#define _1c3c7029_0525_47d5_b67d_48d7e2dba80a

#include <upcxx/intru_queue.hpp>
#include <upcxx/memory_kind.hpp>

#include <utility>

#if UPCXXI_GEX_MK_CUDA \
 || UPCXXI_GEX_MK_HIP // || ...
  #define UPCXXI_GEX_MK_ANY 1 // true iff ANY memory kind is using GASNet MK
#else
  #undef  UPCXXI_GEX_MK_ANY
#endif
#if (!UPCXXI_CUDA_ENABLED || UPCXXI_GEX_MK_CUDA) \
 && (!UPCXXI_HIP_ENABLED  || UPCXXI_GEX_MK_HIP) // && ...
  #define UPCXXI_GEX_MK_ALL 1 // true iff ALL memory kinds are using GASNet MK
#else
  #undef  UPCXXI_GEX_MK_ALL
#endif

namespace upcxx {
namespace detail { struct device_allocator_base; }
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
    heap_state(memory_kind k) : alloc_base(nullptr), my_kind(k) {}
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
  };

} } // namespace
#endif
