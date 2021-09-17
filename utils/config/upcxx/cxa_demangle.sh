#!/bin/bash

set -e
function cleanup { rm -f conftest.cpp conftest; }
trap cleanup EXIT

cat >conftest.cpp <<_EOF
#include <cxxabi.h>
int main() {
  abi::__cxa_demangle("", 0, 0, 0);
  return 0;
}
_EOF

TEST="(${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} -o conftest conftest.cpp)"
if eval $TEST &> /dev/null; then
  echo '#define UPCXXI_HAVE___CXA_DEMANGLE 1'
else
  echo '#undef  UPCXXI_HAVE___CXA_DEMANGLE'
fi
