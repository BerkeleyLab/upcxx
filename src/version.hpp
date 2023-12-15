#ifndef _e0688565_5558_4440_97a0_d7ec3897303a
#define _e0688565_5558_4440_97a0_d7ec3897303a

// UPCXX_VERSION release identifier format:
// YYYYMMPPL = [YEAR][MONTH][PATCH]
#define UPCXX_VERSION 20230900L

// UPCXX_SPEC_VERSION release identifier format:
// YYYYMM00L = [YEAR][MONTH]
#define UPCXX_SPEC_VERSION 20230900L

namespace upcxx {
  long release_version();
  long spec_version();
}

// UPCXX_THREADMODE
//   Undefined for SEQ
//   Non-zero for PAR
#undef UPCXX_THREADMODE
#if UPCXXI_BACKEND_GASNET_PAR
  // One must NOT depend on the specific non-zero value
  #define UPCXX_THREADMODE 1
#endif

// UPCXX_CODEMODE
//   Undefined for debug
//   Non-zero for "opt" (production)
#undef UPCXX_CODEMODE
#if !UPCXXI_ASSERT_ENABLED
  // One must NOT depend on the specific non-zero value
  #define UPCXX_CODEMODE 1
#endif

#endif
