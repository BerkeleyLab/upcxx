#!/bin/bash

set -e
function cleanup { rm -f conftest.c; }
trap cleanup EXIT

# valid C identifiers not expected to appear by chance
TOKEN1='_rrMKHV81Bsp9aU1A_'
TOKEN2='_7z0dVmWCWoS2H2Ro_'

cat >conftest.c <<_EOF
#include <gasnetex.h>
int x = $TOKEN1+GASNET_MAXEPS+$TOKEN2:
_EOF

if ! [[ $(eval ${GASNET_CC} ${GASNET_CPPFLAGS} ${GASNET_CFLAGS} -E conftest.c) =~ ${TOKEN1}(.*)${TOKEN2} ]]; then
  echo "ERROR: regex match failed probing GASNET_MAXEPS" >&2
  exit 1
fi
TMP="${BASH_REMATCH[1]}"
TMP=${TMP%+*} # Strip "suffix"
TMP=${TMP#*+} # Strip "prefix"
echo "#define UPCXX_MAXEPS $TMP"
