#!/bin/bash
set -xe

triple="${UPCXX_THREADMODE:=seq}-${UPCXX_CODEMODE:=debug}-${UPCXX_NETWORK:=udp}"
name=$(basename ${0%.sh})
source_dir=$(dirname $0)
testname=$"test-${name}-${triple}"
read -r -d '' runscript <<EOF || true
#!${UPCXX_BASH}
echo "env LD_LIBRARY_PATH=${testname} DYLD_LIBRARY_PATH=${testname} ${testname}/test-${name}"
EOF

source ${source_dir}/ccs.shinc

have_static="UPCXX_HAVE_STATIC_LIBS_CCS_$(tr '[:lower:]' '[:upper:]' <<<${UPCXX_NETWORK})"

if [ ! -z "${!have_static}" ]; then
  compile
else
  skip "${have_static} not set"
fi
