#ifndef _986ac560_8bc4_4d86_acbd_e89d7f742629
#define _986ac560_8bc4_4d86_acbd_e89d7f742629

#include <upcxx/backend/gasnet/runtime_internal.hpp>

namespace upcxx { namespace backend {

  template<typename Device>
  struct device_heap_state; // : public device_heap_state_base<Device>

  template<typename Device>
  struct device_heap_state_base : public backend::heap_state {
    #if UPCXXI_GEX_MK_ANY
      // objects for using GEX memory kinds
      gex_EP_t ep;
      gex_MK_t kind;
      gex_Segment_t segment;
    #endif

    device_heap_state_base() : heap_state(Device::kind) {
      #if UPCXXI_GEX_MK_ANY
        ep =      GEX_EP_INVALID;
        kind =    GEX_MK_INVALID;
        segment = GEX_SEGMENT_INVALID;
      #endif
    }

    static device_heap_state<Device> *get(std::int32_t heap_idx, bool allow_null = false) {
      heap_state *hs = heap_state::get(heap_idx, allow_null);
      if (hs) UPCXX_ASSERT(hs->kind() == Device::kind);
      return static_cast<device_heap_state<Device>*>(hs);
    }
  }; // device_heap_state_base

}} // namespace


#endif
