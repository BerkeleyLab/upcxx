# This Makefile demonstrates the recommended way to build simple UPC++ programs.
# Note this uses some GNU make extensions for conciseness.
#
# To use this makefile, set the UPCXX_INSTALL variable to the upcxx install directory, e.g.
# make UPCXX_INSTALL=<myinstalldir>
# or (for bash)
# export UPCXX_INSTALL=<myinstalldir>; make

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
  compute-pi \
  compute-pi-multi-examples 

all: $(EXAMPLES)

# The rule for building any example.
%: %.cpp $(wildcard *.h) $(wildcard *.hpp)
	$(CXX) $@.cpp $(CXXFLAGS) -o $@

clean:
	rm -f $(EXAMPLES)

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


