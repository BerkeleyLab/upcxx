#!/bin/bash

set -e
function cleanup { rm -f conftest.c; }
trap cleanup EXIT

# valid C identifiers not expected to appear by chance
TOKEN1='_Q1jX1kQx8ZgbmN1B_before_'
TOKEN2='_MbrNe0CzO7WmW0Kf_after_'

for SYMBOL in \
  GASNET_NATIVE_NP_ALLOC_REQ_MEDIUM \
; do

cat >conftest.c <<_EOF
#include <gasnetex.h>

/* This is sufficient to capture a single valid C identifier or non-negative integer, at preprocess time */
#ifndef _CONCAT
#define _CONCAT_HELPER(a,b) a ## b
#define _CONCAT(a,b) _CONCAT_HELPER(a,b)
#endif
#undef CONFTEST_RESULT
#define CONFTEST_RESULT(x) _CONCAT($TOKEN1,_CONCAT(x,$TOKEN2))

/* Valid values in this probe are undefined or a non-negative integer */
#ifdef $SYMBOL
  CONFTEST_RESULT($SYMBOL)
#else
  #if GEX_SPEC_VERSION_MAJOR > 0 || GEX_SPEC_VERSION_MINOR >= 13
    CONFTEST_RESULT(UNDEF)
  #else /* REMOVE ME: TEMPORARY fallback code */
    #if GASNET_CONDUIT_IBV || GASNET_CONDUIT_ARIES || GASNET_CONDUIT_SMP
      CONFTEST_RESULT(1)
    #else
      CONFTEST_RESULT(UNDEF)
    #endif
  #endif
#endif
_EOF

if ! [[ $(eval ${GASNET_CC} ${GASNET_CPPFLAGS} ${GASNET_CFLAGS} -E conftest.c) =~ ${TOKEN1}([A-Za-z0-9_]+)${TOKEN2} ]]; then
  echo "ERROR: regex match failed probing $SYMBOL" >&2
  exit 1
fi
result="${BASH_REMATCH[1]}"
if [[ $result = UNDEF ]]; then
  echo "#undef UPCXX_$SYMBOL"
elif [[ $result =~ (^[0-9]+$) ]]; then
  echo "#define UPCXX_$SYMBOL $result"
else
  "ERROR: unexpected result '$result' probing $SYMBOL" >&2
  exit 1
fi

done
