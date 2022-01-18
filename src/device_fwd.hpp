#ifndef _1c3c7029_0525_47d5_b67d_48d7e2dba80a
#define _1c3c7029_0525_47d5_b67d_48d7e2dba80a

#include <upcxx/intru_queue.hpp>
#include <upcxx/memory_kind.hpp>

#include <utility>

namespace upcxx {
namespace detail { struct device_allocator_base; }
namespace backend {

  // backend::heap_state: base class for managing state
  // associated with a particular dynamically created device heap,
  // and the global table of such states, indexed by heap_idx
  struct heap_state {
    detail::device_allocator_base *alloc_base;

  #if UPCXXI_CUDA_ENABLED && UPCXXI_MAXEPS > 1
    static constexpr int max_heaps = UPCXXI_MAXEPS;
  #else
    static constexpr int max_heaps = 33;
  #endif
    static_assert(max_heaps > 1, "bad value of UPCXXI_MAXEPS");

  #if UPCXXI_GEX_MK_CUDA // || ...
    #define UPCXXI_GEX_MK_ANY 1 // true iff ANY memory kind is using GASNet MK
    static constexpr bool use_mk = true;
  #else
    #undef  UPCXXI_GEX_MK_ANY
    static constexpr bool use_mk = false;
  #endif
  #if (!UPCXXI_CUDA_ENABLED || UPCXXI_GEX_MK_CUDA) // && ...
    #define UPCXXI_GEX_MK_ALL 1 // true iff ALL memory kinds are using GASNet MK
  #else
    #undef  UPCXXI_GEX_MK_ALL
  #endif

    heap_state(memory_kind k) : alloc_base(nullptr), my_kind(k) {}
    memory_kind kind() { return my_kind; }

  protected:
    memory_kind const my_kind; // serves as both tag and magic
    static heap_state *heaps[max_heaps];
    static int heap_count;

    // currently we do not recycle heap_idx when using GASNet memory kinds,
    // until GASNet grows the ability to recycle endpoints
    static constexpr bool recycle = !use_mk;

  public:
    static void init();
    static int alloc_index() {
      UPCXX_ASSERT_ALWAYS(heap_count < max_heaps, "exceeded max device opens: " << max_heaps - 1);
      int idx;
      if (recycle) {
        for (idx=1; idx < max_heaps; idx++) {
          if (!heaps[idx]) break;
        }
      } else {
        idx = heap_count;
      }
      UPCXX_ASSERT_ALWAYS(idx < max_heaps && heaps[idx] == nullptr, "internal error on heap creation");
      heap_count++;
      return idx;
    }
    static void free_index(int heap_idx) {
      UPCXX_ASSERT_ALWAYS(heaps[heap_idx] == nullptr && heap_count > 1, "internal error on heap destruction");
      if (recycle) heap_count--;
    }

    // retrieve reference to heap_state pointer at heap_idx, with bounds-checking
    static inline heap_state *&get(std::int32_t heap_idx, bool allow_null = false) {
      UPCXX_ASSERT(heap_count <= max_heaps, "internal error in backend::heap_state::get");
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
      detail::intru_queue<
        device_cb,
        detail::intru_queue_safety::none,
        &device_cb::intruder
      > cbs;
    } cuda;
  #endif
  };

} } // namespace
#endif
