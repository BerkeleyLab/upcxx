#!/bin/bash

set -e
function cleanup { rm -f conftest.cpp; }
trap cleanup EXIT

# valid C identifiers not expected to appear by chance
TOKEN1='_rrMKHV81Bsp9aU1A_'
TOKEN2='_7z0dVmWCWoS2H2Ro_'

cat >conftest.cpp <<_EOF
#include <gasnetex.h>
#include <gasnet_tools.h>
$TOKEN1+GASNETT_NEVER_INLINE(/*fnname*/,/*declarator*/)+$TOKEN2
_EOF

if ! [[ $(eval ${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} -E conftest.cpp) =~ ${TOKEN1}(.*)${TOKEN2} ]]; then
  echo "ERROR: regex match failed probing GASNETT_NEVER_INLINE" >&2
  exit 1
fi
TMP="${BASH_REMATCH[1]}"
TMP=${TMP%+*} # Strip "suffix"
TMP=${TMP#*+} # Strip "prefix"
echo "#define UPCXX_NOINLINE $TMP"
