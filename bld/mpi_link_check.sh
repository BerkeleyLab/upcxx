#!/bin/bash
# This script is a sanity check to see that CXX behaves as
# an MPI linker if and only if it is required to.

set -e
function cleanup { rm -f conftest1.c conftest1.o conftest2.cpp conftest; }
trap cleanup EXIT

eval $($GMAKE echovar VARNAME=CONDUITS)
mpi_conduit=''
mpi_compat=''
for conduit in $CONDUITS; do
  if [[ $conduit = 'mpi' ]]; then
    mpi_conduit=' mpi'
  elif grep '^GASNET_LD_REQUIRES_MPI *= *1' ${conduit}-conduit/conduit.mak &>/dev/null; then
    mpi_compat+=" $conduit"
  fi
done
if [[ -z $mpi_compat &&
      ! $(egrep -o -e "--?(en|dis)able-mpi([ \"]|=[^ \"]*)" <<<"$GASNET_CONFIGURE_ARGS " | tail -1) \
            =~ (--?enable-mpi[ \"]) ]]; then
  # No mpi-spawner users and mpi-conduit not explicitly enabled
  echo "INFO: MPI C++ link test skipped" >&2
  exit 0
fi

eval $($GMAKE echovar VARNAME=CXX)
eval $($GMAKE echovar VARNAME=MPI_CC)
eval $($GMAKE echovar VARNAME=MPI_LIBS)
eval $($GMAKE echovar VARNAME=MPI_CFLAGS)

function fail {
  echo "ERROR: MPI $* test failed (required by:$mpi_compat$mpi_conduit)" >&2
  echo 'ERROR: In most cases configuring UPC++ using `--with-cxx=mpicxx` (or similar)' >&2
  echo "ERROR: will resolve this issue.  However, 'Configuration: Linux' in INSTALL.md" >&2
  echo 'ERROR: includes additional options and more information.' >&2
  exit 1
}

# Step 1. Using $MPI_CC, compile an MPI/C object.
cat >conftest1.c <<_EOF
#include <mpi.h>
int foo(int arg) {
  MPI_Init((void*)0,(void*)0);
  MPI_Finalize();
  return 0;
}
_EOF
$(eval ${MPI_CC} ${MPI_CFLAGS} -c conftest1.c -o conftest1.o >/dev/null ) || fail 'C compile'

# Step 2. Using $CXX, compile a C++ object and link with the MPI/C object from Step 1.
cat >conftest2.cpp <<_EOF
#include <iostream>
extern "C" int foo(int arg);
int main(int argc, char **argv) {
  std::cout << foo(123);
  return 0;
}
_EOF
$(eval ${CXX} conftest2.cpp conftest1.o ${MPI_LIBS} -o conftest >/dev/null ) || fail 'C++ link'

echo "INFO: MPI C++ link test passed" >&2
exit 0 # pedantic
