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

probe_macro GASNETT_NEVER_INLINE "GASNETT_NEVER_INLINE(/*fnname*/,/*declarator*/)" UPCXX_NOINLINE

probe_macro GASNET_MAXEPS GASNET_MAXEPS UPCXX_MAXEPS

probe_macro GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM UPCXX_NATIVE_NP_ALLOC_REQ_MEDIUM 1

probe_macro GASNET_HIDDEN_AM_CONCURRENCY_LEVEL GASNET_HIDDEN_AM_CONCURRENCY_LEVEL UPCXX_HIDDEN_AM_CONCURRENCY_LEVEL 1

