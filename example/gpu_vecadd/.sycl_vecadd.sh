#!/bin/bash
# This script exists for integration with the UPC++ maintainer test system
set -e # exit on first error

TEST=sycl_vecadd
EXE="$1"
SRCDIR="$upcxx_src/example/gpu_vecadd"
cp -a $SRCDIR ${EXE}-tmp
TMPDIR=$(cd ${EXE}-tmp && pwd)

# Upon normal termination, remove the temporary directory
trap "rm -Rf $TMPDIR" EXIT

# Upon abnormal termination, remove all generated files
trap "rm -f $TMPDIR $EXE" ERR

set -x  # Start tracing the actual build commands

$MAKE -C $TMPDIR clean UPCXX_INSTALL=$upcxx_bld
$MAKE -C $TMPDIR $TEST UPCXX_INSTALL=$upcxx_bld

mv $TMPDIR/$TEST $EXE

exit 0
