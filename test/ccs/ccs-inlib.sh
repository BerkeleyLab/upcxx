#!/bin/bash
set -xe

triple="${UPCXX_THREADMODE:=seq}-${UPCXX_CODEMODE:=debug}-${UPCXX_NETWORK:=udp}"
name=$(basename ${0%.sh})
source_dir=$(dirname $0)
testname=$"test-${name}-${triple}"
require_fpic=1

source ${source_dir}/ccs.shinc

compile
