# This Makefile demonstrates the recommended way to build simple UPC++ programs.
# Note this uses some GNU make extensions for conciseness.
#
# To use this makefile, set the UPCXX_INSTALL variable to the upcxx install directory, e.g.
# make UPCXX_INSTALL=<myinstalldir> hello-world
# or (for bash)
# export UPCXX_INSTALL=<myinstalldir>; make hello-world

ifeq ($(UPCXX_INSTALL),)
$(warning UPCXX_INSTALL environment variable is not set, assuming upcxx is in the PATH)
UPCXX=upcxx
UPCXXRUN=upcxx-run
else
ifeq ($(wildcard $(UPCXX_INSTALL)/bin/upcxx),)
$(error Please set UPCXX_INSTALL=/path/to/upcxx/install)
else
UPCXX=$(UPCXX_INSTALL)/bin/upcxx
UPCXXRUN=$(UPCXX_INSTALL)/bin/upcxx-run
endif
endif

UPCXX_THREADMODE ?= seq
export UPCXX_THREADMODE
UPCXX_CODEMODE ?= debug
export UPCXX_CODEMODE
CXX = $(UPCXX)

CXXFLAGS = # optional command-line override

# Programs to build, assuming each has a corresponding *.cpp file
EXAMPLES = \
  dmap-async-find-test \
  dmap-async-insert-test \
  dmap-atomics-test \
  dmap-conjoined-test \
  dmap-promises-test \
  dmap-quiescence-test \
  dmap-test \
  hello-world \
  local-team \
  non-contig-example \
  persona-example	\
  persona-example-rputs \
  rb1d \
  rb1d-rpc \
  rb1d-rpcinit \
  reduce-complex-minloc \
  rput-rpc \
  serial-custom \
  serial-fields \
  serial-recursive \
  serial-values \
  team_advanced \
  team_create_sub \
  team_simple \
  view-accumulate \
  view-histogram1 \
  view-histogram2 \
  view-matrix-tasks


CUDA_EXAMPLES = \
  h-d \
  h-d-remote

OPENMP_EXAMPLES = \
  rpc-omp \
  rput-omp

all:    $(EXAMPLES)
cuda:   $(CUDA_EXAMPLES)
openmp: $(OPENMP_EXAMPLES)

# The rule for building any example.
%: %.cpp $(wildcard *.h) $(wildcard *.hpp)
	$(CXX) $@.cpp $(CXXFLAGS) -o $@


# Example-specific variable specializations.
PAR_EXAMPLES = \
  persona-example \
  persona-example-rputs \
  view-matrix-tasks
$(PAR_EXAMPLES): UPCXX_THREADMODE=par

$(OPENMP_EXAMPLES): UPCXX_THREADMODE=par
$(OPENMP_EXAMPLES): CXXFLAGS+=-fopenmp

clean:
	rm -Rf $(EXAMPLES) $(EXAMPLES:=.dSYM)
	rm -Rf $(CUDA_EXAMPLES) $(CUDA_EXAMPLES:=.dSYM)
	rm -Rf $(OPENMP_EXAMPLES) $(OPENMP_EXAMPLES:=.dSYM)

.PHONY: clean all run

PROCS ?= 4
ARGS ?=
LINE = =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

run: 
	@for file in $(EXAMPLES) ; do \
          if test -x $$file ; then \
            echo $(LINE) ; \
            ( set -x ; \
              $(UPCXXRUN) -n $(PROCS) $$file $(ARGS) ; \
            ) ; \
            echo $(LINE) ; \
          fi ; \
         done


