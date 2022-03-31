#!/bin/bash

set -xe

triple="${UPCXX_THREADMODE:=seq}-${UPCXX_CODEMODE:=debug}-${UPCXX_NETWORK:=udp}"
name=$(basename ${0%.sh})
source_dir=$(dirname $0)
testname=$"test-${name}-${triple}"
require_fpic=1
read -r -d '' runscript <<EOF || true
#!${UPCXX_BASH}
echo ${UPCXX_PYTHON:-python} ./${testname}/ccs-test.py
EOF

source ${source_dir}/ccs.shinc

if [ "$(${UPCXX_PYTHON:-python} -c 'import sys; print(sys.version_info.major)')" -lt 3 ]; then
  skip "Requires Python >= 3"
elif [ ! -f "$(${UPCXX_PYTHON:-python} -c 'from sysconfig import get_paths; print(get_paths()["include"])')/Python.h" ]; then
  skip "libpython headers not available"
else
  compile
fi
