#!/bin/bash
set -e # exit on first error

EXE="$1"
SRC="$upcxx_src/test/hello_upcxx.cpp"
OBJ="$EXE.o"  # temporary file needs a unique name

# Upon normal termination, remove the temporary file
trap "rm -f $OBJ" EXIT

# Upon abnormal termination, remove the temporary file and the EXE
trap "rm -f $OBJ $EXE" ERR

# Import all the compiler and flags variables
eval $($upcxx_bld/bin/upcxx-meta DUMP)

set -x  # Start tracing the actual build commands

# Compile and link according to the standard recipe for use of `upcxx-meta`,
# with the addition of `$EXTRAFLAGS` to the compile step.
$CXX $CPPFLAGS $CXXFLAGS $EXTRAFLAGS -c $SRC -o $OBJ
$CXX $LDFLAGS $OBJ $LIBS -o $EXE
