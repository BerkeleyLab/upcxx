#!/bin/bash

set -e
function cleanup { rm -f conftest.c; }
trap cleanup EXIT

cat >conftest.c <<_EOF
#include <gasnet_portable_platform.h>
#if PLATFORM_COMPILER_GNU && PLATFORM_COMPILER_VERSION_GE(7,0,0) && PLATFORM_COMPILER_VERSION_LT(10,0,0)
  // G++ 7, 8 and 9 appear buggy.  See UPC++ issue 400
  static char result[] = "impacted by issue 400"; // seeking this string in preprocessed output
#endif
_EOF

if [[ $(eval ${GASNET_CC} ${GASNET_CPPFLAGS} ${GASNET_CFLAGS} -E conftest.c ) =~ (impacted by issue 400) ]]; then
  echo
  echo '#ifndef UPCXXI_ISSUE400_WORKAROUND'
  echo '#define UPCXXI_ISSUE400_WORKAROUND 1'
  echo '#endif'
fi
