#ifndef _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1
#define _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1

#define UPCXXI_MANY_KINDS (0 || UPCXXI_CUDA_ENABLED)

#include <cstdint>
#include <string>

namespace upcxx {
  enum class memory_kind : std::uint8_t {
    host=0,
    cuda_device=1,
    any = 2 // should remain last
  };

  namespace detail {
    inline std::string to_string(memory_kind kind) {
      switch (kind) {
        case memory_kind::host:        return "host";
        case memory_kind::cuda_device: return "cuda_device";
        case memory_kind::any:         return "any";
        default:                       
          return std::string("unknown(") + std::to_string((int)kind) + ")"; 
      }
    }
  }
}
#endif
