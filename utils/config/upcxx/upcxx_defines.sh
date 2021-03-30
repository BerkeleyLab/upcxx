#!/bin/bash

echo "#define UPCXX_NETWORK_$(tr '[a-z]' '[A-Z]' <<<$UPCXX_NETWORK) 1"

eval $($UPCXX_GMAKE -C "$UPCXX_TOPBLD" echovar VARNAME=UPCXX_MPSC_QUEUE)
echo "#define ${UPCXX_MPSC_QUEUE} 1"
