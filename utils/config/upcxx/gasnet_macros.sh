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
  if [[ $result = $UNDEF && $4 ]]; then
    echo "#undef $3 // $1 not defined"
  elif [[ $result = $UNDEF && !$4 ]]; then
    echo "Missing required definition of $1" >&2
    exit 1
  else
    echo "#define $3 $result"
  fi
}

probe_macro GASNETT_NEVER_INLINE "GASNETT_NEVER_INLINE(/*fnname*/,/*declarator*/)" UPCXX_ATTRIB_NOINLINE
probe_macro GASNETT_NORETURN GASNETT_NORETURN UPCXX_ATTRIB_NORETURN
probe_macro GASNETT_PURE     GASNETT_PURE     UPCXX_ATTRIB_PURE
probe_macro GASNETT_CONST    GASNETT_CONST    UPCXX_ATTRIB_CONST
probe_macro GASNETT_HOT      GASNETT_HOT      UPCXX_ATTRIB_HOT
probe_macro GASNETT_COLD     GASNETT_COLD     UPCXX_ATTRIB_COLD

probe_macro GASNET_MAXEPS GASNET_MAXEPS UPCXX_MAXEPS

probe_macro GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM UPCXX_NATIVE_NP_ALLOC_REQ_MEDIUM 1

probe_macro GASNET_HIDDEN_AM_CONCURRENCY_LEVEL GASNET_HIDDEN_AM_CONCURRENCY_LEVEL UPCXX_HIDDEN_AM_CONCURRENCY_LEVEL 1

probe_macro gasnett_spinloop_hint "gasnett_spinloop_hint()" "UPCXX_SPINLOOP_HINT()"
# must use bypass gasnett_builtin here to avoid a header dependence on gasneti_assert:
probe_macro gasneti_builtin_unreachable "gasneti_builtin_unreachable()" "UPCXX_UNREACHABLE()"

probe_macro GASNETT_PREDICT_TRUE  "GASNETT_PREDICT_TRUE(expr)"  "UPCXX_PREDICT_TRUE(expr)"
probe_macro GASNETT_PREDICT_FALSE "GASNETT_PREDICT_FALSE(expr)" "UPCXX_PREDICT_FALSE(expr)"

cat <<_EOF

// replacements for if statement, with branch prediction annotation
#define UPCXX_IF_PT(expr) if (UPCXX_PREDICT_TRUE(expr))
#define UPCXX_IF_PF(expr) if (UPCXX_PREDICT_FALSE(expr))

_EOF

