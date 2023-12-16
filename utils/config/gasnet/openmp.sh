#!/bin/bash

# Try to find OpenMP support, unless configured using --(disable|without)-openmp
# Such support is *optional* for inclusion in maintainer's tests

set -e
function cleanup { rm -f conftest.cpp conftest; }
trap cleanup EXIT

# Note use of GASNET_CONFIGURE_ARGS is a slight "cheat" to get all
# arguments not consumed by the UPC++ configure script itself:
option=$(
  $UPCXX_GMAKE -C "$UPCXX_TOPBLD" echovar VARNAME=GASNET_CONFIGURE_ARGS |
  egrep -o -e '--?((en|dis)able|with(out)?)-openmp\>' |
  tail -1 ) >& /dev/null
if [[ ${option} =~ -(disable|without)- ]]; then
  echo -e "\n# Optional OpenMP support disabled via configure"
  exit 0
fi

cat >conftest.cpp <<_EOF
#include <omp.h>
#include <iostream>
int main () {
  int numthreads, mythread;
#pragma omp parallel private(numthreads, mythread)
  {
    mythread = omp_get_thread_num();
    numthreads = omp_get_num_threads();
    std::cout << "I am thread " << mythread << " of " << numthreads << std::endl;
  }
  return 0;
}
_EOF

unset openmp_flags
for x in -qopenmp -fopenmp -mp; do
  if eval ${GASNET_CXX} ${GASNET_CXXCPPFLAGS} ${GASNET_CXXFLAGS} $x -o conftest conftest.cpp &> /dev/null; then
    openmp_flags=$x
    break
  fi
done

if [[ -n $openmp_flags ]]; then
  echo -e "\n# Optional OpenMP support detected"
  echo "UPCXX_HAVE_OPENMP = 1"
  echo "UPCXX_OPENMP_FLAGS = $openmp_flags"
elif [[ ${option} =~ -(enable|with)- ]]; then
  echo "ERROR: Requested OpenMP support was not detected" >&2
  exit 1
else
  echo -e "\n# Optional OpenMP support not detected"
fi
