#ifndef _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1
#define _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1

#if UPCXXI_CUDA_ENABLED || UPCXXI_HIP_ENABLED // || ...
#define UPCXXI_MANY_KINDS 1 // true iff this build supports device allocation
#else
#undef  UPCXXI_MANY_KINDS
#endif

#if 1 < UPCXXI_CUDA_ENABLED + UPCXXI_HIP_ENABLED // + ...
#define UPCXXI_MANY_DEVICE_KINDS 1 // true iff this build supports allocation on more than one device kind
#else
#undef  UPCXXI_MANY_DEVICE_KINDS
#endif

#include <cstdint>
#include <string>

namespace upcxx {
  enum class memory_kind : std::uint8_t {
    host=0,
    cuda_device=1,
    hip_device=2,
    any = 3 // should remain last
  };

  namespace detail {
    inline std::string to_string(memory_kind kind) {
      switch (kind) {
        case memory_kind::host:        return "host";
        case memory_kind::cuda_device: return "cuda_device";
        case memory_kind::hip_device:  return "hip_device";
        case memory_kind::any:         return "any";
        default:                       
          return std::string("unknown(") + std::to_string((int)kind) + ")"; 
      }
    }
  }
}
#endif
