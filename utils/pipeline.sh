#!/bin/bash

# This script is used for BitBucket pipelines CI

# Optional Inputs: (shell or pipeline variables)
#  CI_CONFIGURE_ARGS : extra arguments for the configure line
#  CI_MAKE : make tool to use
#  CI_MAKE_PARALLEL : argument to set build parallelism, eg "-j4"
#  CI_DEV_CHECK : "1" to enable extra dev-mode tests
#  CI_RUN_TESTS : "0" to disable running of tests
#  CI_NETWORKS : override default network "smp udp"
#  CI_RANKS : override default rank count
#  CI_PREFIX : override default install prefix
#  CI_SRCDIR : override location of source tree (default .)
#  CI_BLDDIR : override location of source tree (default .)
#  CI_EXTRAFLAGS : extra flags for test compile (default "-Werror")

if test -n "$BITBUCKET_COMMIT" ; then
echo "----------------------------------------------"
echo "CI info:"
echo "Repo       "$BITBUCKET_REPO_FULL_NAME ;
echo "Branch     "$BITBUCKET_BRANCH ;
echo "Tag        "$BITBUCKET_TAG ;
echo "Commit     "$BITBUCKET_COMMIT ;
echo "PR         "$BITBUCKET_PR_ID ;
fi
echo "----------------------------------------------"
echo "Inputs:"
echo "CI_CONFIGURE_ARGS=$CI_CONFIGURE_ARGS"
CI_DEV_CHECK="${CI_DEV_CHECK:-0}"
echo "CI_DEV_CHECK=$CI_DEV_CHECK"
CI_RUN_TESTS="${CI_RUN_TESTS:-1}"
echo "CI_RUN_TESTS=$CI_RUN_TESTS"
CI_TESTS="${CI_TESTS:-}"
echo "CI_TESTS=$CI_TESTS"
CI_NO_TESTS="${CI_NO_TESTS:-}"
echo "CI_NO_TESTS=$CI_NO_TESTS"
CI_MAKE=${CI_MAKE:-make}
echo "CI_MAKE=$CI_MAKE"
CI_MAKE_PARALLEL=${CI_MAKE_PARALLEL:--j8}
echo "CI_MAKE_PARALLEL=$CI_MAKE_PARALLEL"
CI_NETWORKS="${CI_NETWORKS:-smp udp}"
echo "CI_NETWORKS=$CI_NETWORKS"
CI_RANKS="${CI_RANKS:-4}"
echo "CI_RANKS=$CI_RANKS"
CI_SRCDIR="${CI_SRCDIR:-.}"
CI_SRCDIR=$(cd "$CI_SRCDIR" && pwd -P || echo "$CI_SRCDIR")
echo "CI_SRCDIR=$CI_SRCDIR"
CI_BLDDIR="${CI_BLDDIR:-.}"
CI_BLDDIR=$(mkdir -p "$CI_BLDDIR" && cd "$CI_BLDDIR" && pwd -P || echo "$CI_BLDDIR")
echo "CI_BLDDIR=$CI_BLDDIR"
CI_PREFIX="${CI_PREFIX:-$CI_BLDDIR/inst}"
echo "CI_PREFIX=$CI_PREFIX"
CI_EXTRAFLAGS="${CI_EXTRAFLAGS:--Werror}"
echo "CI_EXTRAFLAGS=$CI_EXTRAFLAGS"
echo "----------------------------------------------"

MAKE="$CI_MAKE $CI_MAKE_PARALLEL"
if (( "$CI_DEV_CHECK" )) ; then
  DEV="dev-"
else
  DEV=""
fi
if [[ -n "$CI_TESTS" ]] ; then
  CI_TESTS="TESTS=$CI_TESTS"
else
  CI_TESTS="DUMMY=1"
fi
if [[ -n "$CI_NO_TESTS" ]] ; then
  CI_NO_TESTS="NO_TESTS=$CI_NO_TESTS"
else
  CI_NO_TESTS="DUMMY=1"
fi

set -e
set -x

cd "$CI_BLDDIR"
rm -f .pipefail

time "$CI_SRCDIR/configure" --prefix=$CI_PREFIX $CI_CONFIGURE_ARGS

time $MAKE all

time $MAKE install 

time $MAKE test_install || touch .pipe-fail

time $MAKE ${DEV}tests NETWORKS="$CI_NETWORKS" "$CI_TESTS" "$CI_NO_TESTS" EXTRAFLAGS="$CI_EXTRAFLAGS" || touch .pipe-fail      # compile tests

if ! (( "$CI_DEV_CHECK" )) ; then # build negative compile tests, even when not doing full dev-check
  time $MAKE dev-tests-debug NETWORKS=smp TESTS=neg- "$CI_NO_TESTS" EXTRAFLAGS="$CI_EXTRAFLAGS" || touch .pipe-fail
fi

if (( "$CI_RUN_TESTS" )) ; then
  # variables controlling (potentially simulated) distributed behavior
  export GASNET_SPAWNFN=${GASNET_SPAWNFN:-L}
  export GASNET_SUPERNODE_MAXSIZE=${GASNET_SUPERNODE_MAXSIZE:-2}

  # Run for each network as a separate test session, in the user-provided order
  # Deliberately avoid parallel make for runners to ensure we don't risk memory exhaustion
  for network in $CI_NETWORKS ; do 
    time $CI_MAKE ${DEV}run-tests RANKS=$CI_RANKS NETWORKS=$network "$CI_TESTS" "$CI_NO_TESTS" || touch .pipe-fail  
  done
fi

test ! -f .pipe-fail # propagate delayed failure
exit $?
