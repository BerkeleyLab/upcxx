#ifndef _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1
#define _0d062c0a_ca33_4b3f_b70f_278c00e3a1f1

#define UPCXXI_MANY_KINDS (0 || UPCXXI_CUDA_ENABLED)

#include <cstdint>

namespace upcxx {
  enum class memory_kind : std::uint16_t {
    host=1,
    cuda_device=2,
    any = 1 | 2
  };
}
#endif
