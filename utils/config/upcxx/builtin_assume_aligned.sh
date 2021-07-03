#!/bin/bash

set -e
function cleanup { rm -f conftest.cpp conftest; }
trap cleanup EXIT

cat >conftest.cpp <<_EOF
#include <cstring>
int main() {
    int x = 0;
    int y = 7;
    std::memcpy(__builtin_assume_aligned(&x, sizeof(x)),
                __builtin_assume_aligned(&y, sizeof(y)), sizeof(x));
    return 0;
}
_EOF

if eval ${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} -o conftest conftest.cpp &> /dev/null; then
  echo '#define UPCXXI_HAVE___BUILTIN_ASSUME_ALIGNED 1'
else
  echo '#undef UPCXXI_HAVE___BUILTIN_ASSUME_ALIGNED'
fi
