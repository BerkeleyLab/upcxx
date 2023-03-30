#!/bin/bash

echo "#define UPCXX_NETWORK_$(tr '[a-z]' '[A-Z]' <<<$UPCXX_NETWORK) 1"

eval $($UPCXX_GMAKE -C "$UPCXX_TOPBLD" echovar VARNAME=UPCXX_MPSC_QUEUE)
echo "#define ${UPCXX_MPSC_QUEUE} 1"

for feature in DISCONTIG FORCE_LEGACY_RELOCATIONS ; do
  var=UPCXX_$feature
  sym=UPCXXI_$feature
  eval $($UPCXX_GMAKE -C "$UPCXX_TOPBLD" echovar VARNAME=$var)
  if (( ${!var} )); then
    echo "#define $sym 1"
  else
    echo "#undef  $sym"
  fi
done

# The following line builds a bash array of words from the saved configure
# command, using 'eval' to honor the quoting which was applied to arguments
# requiring such.
eval "cmd=( $(grep '# Configure command:' "$UPCXX_TOPBLD/Makefile" | cut -d: -f2-) )"
# Following line uses `set -x` to ask the shell to perform command tracing,
# which has the side effect of nicely quoting everything.  We use the ':' no-op
# command since we only want the argument quoting, and '${cmd[@]:1}' drops the
# '[path_to]configure' portion (which might contain spaces in an extreme case).
# An empty PS4 replaces the leading '+' which is default in such tracing.
# `exec 2>&1` merges stdout and stderr w/o a redirect appearing in the trace.
args=$(PS4=; exec 2>&1; set -x; : "${cmd[@]:1}")
args=${args:2}        # strips the leading ": "
args=${args//\"/\\\"} # escapes double-quotes
echo "#define UPCXXI_CONFIGURE_ARGS \"$args\""
