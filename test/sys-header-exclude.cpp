#include <upcxx/upcxx.hpp>
// DO NOT ADD ANY OTHER #INCLUDES TO THIS FILE!

// The purpose of this test is to determine whether the upcxx.hpp
// header is pulling in certain system/language library headers.
//
// THIS TEST EVALUATES UNSPECIFIED BEHAVIOR!!
// Actions in this test should not be construed as a guarantee of
// past or future behavior of the UPC++ implementation.
// These behaviors can and do change without notice, this test
// only exists to help maintainers monitor such changes.


// Some compilers or platforms transitively suck in headers we don't ask for,
// so skip testing on such platforms.
// We only need this test to be functional for at least ONE common platform in CI testing
#if !defined(__APPLE__)

#ifdef assert
#error Detected <cassert>
#endif

#ifdef FE_DIVBYZERO
#error Detected <cfenv>
#endif

#ifdef DBL_EPSILON
#error Detected <cfloat>
#endif

#if __cplusplus <= 201703
#ifdef CHAR_BIT
#error Detected <climits>
#endif
#endif

#ifdef FP_NAN
#error Detected <cmath>
#endif

#ifdef SIG_DFL
#error Detected <csignal>
#endif

#if !defined(__clang__) && !defined(__INTEL_COMPILER)
#ifdef va_arg
#error Detected <cstdarg>
#endif
#endif

int test_passed = 0;

int main() { return test_passed; }

#else

int test_skipped = 0;

int main() { return test_skipped; }

#endif


