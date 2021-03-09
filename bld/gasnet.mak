#
# GASNet-related configuration
#

# All conduits supported (officially or not) by UPC++
ALL_CONDUITS = smp udp mpi ibv aries ucx ofi

# Conduits for which testing should be skipped by default
UNOFFICIAL_CONDUITS = mpi ucx ofi

# Map UPCXX configuration variables to GASNET_CODEMODE
# PARAMS: OPTLEV, DBGSYM
ifeq ($(words $(UPCXX_DBGOPT)),1)
  GASNET_CODEMODE ?= $(UPCXX_DBGOPT)
else
  ifeq ($(OPTLEV)$(DBGSYM),01)
    GASNET_CODEMODE ?= debug
  else
    GASNET_CODEMODE ?= opt
  endif
endif

# Map UPCXX configuration variables to GASNET_THREADMODE
# PARAMS: UPCXX_BACKEND
GASNET_THREADMODE = $(UPCXX_BACKEND:gasnet_%=%)

# Generated fragment with additional settings
# This replaces previous dynamic use of GASNET_VAR and GASNET_VAR_VAL
GASNET_CONFIG_FRAGMENT = $(upcxx_bld)/bld/gasnet.$(GASNET_CODEMODE).mak
ifneq ($(wildcard $(GASNET_CONFIG_FRAGMENT)),)
  $(GASNET_CONFIG_FRAGMENT): ; @: # empty rule
  include $(GASNET_CONFIG_FRAGMENT)
endif
