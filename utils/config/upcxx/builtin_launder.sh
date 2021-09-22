#!/bin/bash

set -e
function cleanup { rm -f conftest.cpp conftest; }
trap cleanup EXIT

cat >conftest.cpp <<_EOF
#include <new>
struct A { int x; };
struct B {
    const int z;
    B() : z(-1) {}
};
int main() {
    B b;
    const int i = b.z;
    A *a = ::new(&b) A;
    a->x = 3;
    const int j = __builtin_launder(reinterpret_cast<A*>(&b))->x;
    #if __INTEL_COMPILER
    #error Intel compiler does not appear to properly support __builtin_launder
    #error see issue 481 and PR 358 for details
    return 1;
    #endif
    return !(i == -1 && j == 3);
}
_EOF

TEST="(${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} -o conftest conftest.cpp && (! test -z ${UPCXX_CROSS} || ./conftest))"
if eval $TEST &> /dev/null; then
  echo '#define UPCXXI_HAVE___BUILTIN_LAUNDER 1'
else
  echo '#undef UPCXXI_HAVE___BUILTIN_LAUNDER'
fi
