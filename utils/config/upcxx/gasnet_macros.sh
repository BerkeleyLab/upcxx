#!/bin/bash

set -e
function cleanup { rm -f conftest.cpp; }
trap cleanup EXIT

# valid C identifiers not expected to appear by chance
TOKEN1='_t1rMKHV81Bsp9aU1A_'
TOKEN2='_t2z0dVmWCWoS2H2Ro_'
UNDEF='____UNDEF____'

# probe_macro("gasnet_macro_name", "gasnet_macro_invocation", "upcxx_token_name", allow_undef)
# probe a macro which we REQUIRE to be #defined by GASNet,
# and #define the expansion it into upcxx_token_name
function probe_macro {

  cat >conftest.cpp <<_EOF
#include <gasnetex.h>
#include <gasnet_tools.h>
#include <gasnet_portable_platform.h>
#include <gasnet_mk.h>

#ifdef $1
  $TOKEN1+$2+$TOKEN2
#else
  $TOKEN1+$UNDEF+$TOKEN2
#endif
_EOF

  if ! [[ $(eval ${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} -E conftest.cpp) =~ ${TOKEN1}(.*)${TOKEN2} ]]; then
    echo "ERROR: regex match failed probing $1" >&2
    exit 1
  fi
  result="${BASH_REMATCH[1]}"
  result=${result%+*} # Strip "suffix"
  result=${result#*+} # Strip "prefix"
  if [[ $UPCXX_VERBOSE ]] ; then
    echo "// probe_macro($1, $2, $3, $4) => ($result)"
  fi
  barevar=${3%%(*}
  if [[ $result = $UNDEF && $4 ]]; then
    echo "#undef $barevar // $1 not defined"
    eval unset $barevar
  elif [[ $result = $UNDEF && !$4 ]]; then
    echo "Missing required definition of $1" >&2
    exit 1
  else
    echo "#define $3 $result"
    eval $barevar=\"$result\"
  fi
}

probe_macro GASNETT_NEVER_INLINE "GASNETT_NEVER_INLINE(/*fnname*/,/*declarator*/)" UPCXXI_ATTRIB_NOINLINE
cat <<_EOF
#if defined(HIP_INCLUDE_HIP_AMD_DETAIL_HOST_DEFINES_H) && defined(__noinline__)
  /* issue 550: workaround ROCm HIP headers breaking the GNU __noinline__ attribute */
  #undef  UPCXXI_ATTRIB_NOINLINE
  #define UPCXXI_ATTRIB_NOINLINE
#endif
_EOF
probe_macro GASNETT_NORETURN GASNETT_NORETURN UPCXXI_ATTRIB_NORETURN
probe_macro GASNETT_PURE     GASNETT_PURE     UPCXXI_ATTRIB_PURE
probe_macro GASNETT_CONST    GASNETT_CONST    UPCXXI_ATTRIB_CONST

probe_macro GASNET_MAXEPS GASNET_MAXEPS UPCXXI_MAXEPS
if [[ $UPCXXI_MAXEPS -gt 1 ]] ; then
  probe_macro GASNET_HAVE_MK_CLASS_CUDA_UVA GASNET_HAVE_MK_CLASS_CUDA_UVA UPCXXI_GEX_MK_CUDA 1
  probe_macro GASNET_HAVE_MK_CLASS_HIP      GASNET_HAVE_MK_CLASS_HIP      UPCXXI_GEX_MK_HIP  1
  probe_macro GASNET_HAVE_MK_CLASS_ZE       GASNET_HAVE_MK_CLASS_ZE       UPCXXI_GEX_MK_ZE   1
else
  echo "#undef UPCXXI_GEX_MK_CUDA"
  echo "#undef UPCXXI_GEX_MK_HIP"
  echo "#undef UPCXXI_GEX_MK_ZE"
fi

probe_macro GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM UPCXXI_NATIVE_NP_ALLOC_REQ_MEDIUM 1

# see issue 495
#probe_macro GASNET_HIDDEN_AM_CONCURRENCY_LEVEL GASNET_HIDDEN_AM_CONCURRENCY_LEVEL UPCXXI_HIDDEN_AM_CONCURRENCY_LEVEL 1

probe_macro gasnett_spinloop_hint "gasnett_spinloop_hint()" "UPCXXI_SPINLOOP_HINT()"
# must use bypass gasnett_builtin here to avoid a header dependence on gasneti_assert:
probe_macro gasneti_builtin_unreachable "gasneti_builtin_unreachable()" "UPCXXI_UNREACHABLE()"

probe_macro GASNETT_PREDICT_TRUE  "GASNETT_PREDICT_TRUE(expr)"  "UPCXXI_PREDICT_TRUE(expr)"
probe_macro GASNETT_PREDICT_FALSE "GASNETT_PREDICT_FALSE(expr)" "UPCXXI_PREDICT_FALSE(expr)"

if [[ $UPCXX_ASSERT = 0 ]]; then
  # conditionally define UPCXXI_ASSUME iff GASNet assertions are off and gasnett_assume exists (2021.9.0+)
  # otherwise we define it to UPCXX_ASSERT below
  probe_macro gasnett_assume "gasnett_assume(expr)" "UPCXXI_ASSUME(expr)" 1
fi

# probe platform identification macros
# TODO: OS_CNL and OS_WSL have been renamed in recent GASNet and subsumed into OS_LINUX.
# The two flavor variants can safely be removed after we require UPCXXI_GEX_RELEASE_VERSION >= 2022.9.0
for feature in ARCH_X86_64 ARCH_POWERPC ARCH_AARCH64 ARCH_BIG_ENDIAN OS_LINUX OS_FREEBSD OS_NETBSD OS_OPENBSD OS_DARWIN OS_CNL OS_WSL ; do
  name="PLATFORM_$feature"
  probe_macro $name $name "UPCXXI_$name" 1
done

cat <<_EOF

// ASSUME: States simple expression cond is always true, as an annotation directive to guide compiler analysis.
// Becomes an assertion in DEBUG mode and an analysis directive (when available) in NDEBUG mode.
// This notably differs from typical assertions in that the expression must remain valid in NDEBUG mode
// (because it is not preprocessed away), and furthermore may or may not be evaluated at runtime.
// To ensure portability and performance, cond should NOT contain any function calls or side-effects.
#ifndef UPCXXI_ASSUME
#define UPCXXI_ASSUME UPCXX_ASSERT
#endif

// replacements for if statement, with branch prediction annotation
#define UPCXXI_IF_PT(expr) if (UPCXXI_PREDICT_TRUE(expr))
#define UPCXXI_IF_PF(expr) if (UPCXXI_PREDICT_FALSE(expr))

_EOF

