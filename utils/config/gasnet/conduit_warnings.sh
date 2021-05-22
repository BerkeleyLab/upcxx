#!/bin/bash
# The purpose of this script is to generate any extra output to
# be associated with the report of default and available conduits.

set -e
function cleanup { rm -f conftest.in conftest.txt; }
trap cleanup EXIT

# Extract a single value from config.status
function config_status {
   rm -f conftest.in conftest.txt
   echo "@$1@" > conftest.in
   env CONFIG_COMMANDS= CONFIG_LINKS= CONFIG_HEADERS= \
       CONFIG_FILES=conftest.txt:conftest.in \
       ./config.status >/dev/null && cat conftest.txt
}

case $GASNET_CONDUIT in
  ibv)
    unset probed
    ibv_devinfo=$(config_status IBV_guess_prog)
    [[ -z $ibv_devinfo ]] && ibv_devinfo=$(type -p ibv_devinfo 2>/dev/null || true)
    if [[ -n $ibv_devinfo ]]; then
      unset state link rc is_opa only_opa
      # Next two lines ensure
      # 1) `while` is NOT a subshell and thus can set variables for output and can exit
      # 2) rc will contain the exit code of the command after termination of `while`
      exec 3< <( set +e; $ibv_devinfo 2>/dev/null ; echo -n $? )
      while read -u 3 -r key val rest || { rc=$key && break; }; do
        # Looking for a "port:" section with proper "state:" and "link_layer:" values.
        # We exclude matching such ports for an "hfi1_" (OPA) HCA and track whether
        # or not ibv_devinfo has enumerated *only* OPA HCAs.
        case "$key$val" in
                  hca_id:hfi1_*)  is_opa=1 only_opa=${only_opa:-1}; continue ;;
                       hca_id:*)  is_opa=0 only_opa=0; continue ;;
                         port:*)  unset state link; continue ;;
              state:PORT_ACTIVE)  state=1 ;;
          link_layer:InfiniBand)  link=1 ;;
                              *)  continue ;;
        esac
        (( state && link && !is_opa )) && exit 0 # Success
      done
      (( ! rc )) && probed=1 # else ibv_devinfo didn't run
    fi
    if (( probed )); then
      # We ran ibv_devinfo w/o error, but it reported no active (non-OPA) IB ports
      if (( only_opa )); then
        echo 'UPCXX_CONDUIT_WARNINGS = WARNING for ibv-conduit: only Omni-Path HCAs found - ibv is probably the wrong conduit (mpi-conduit is recommended)'
      else
        echo 'UPCXX_CONDUIT_WARNINGS = WARNING for ibv-conduit: no active InfiniBand ports found on this host'
      fi
      echo
    else
      echo 'UPCXX_CONDUIT_WARNINGS = NOTICE for ibv-conduit: unable to probe InfiniBand ports'
    fi
  ;;
esac

exit 0
