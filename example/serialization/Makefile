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

TARGETS= \
    upcxx_serialized_fields \
    upcxx_serialized_values \
    upcxx_serialized_fields_recursive \
    upcxx_serialized_values_recursive \
    upcxx_serialization \
    upcxx_serialization_nested \
    serialize_abstract_base \
    unrolled_list

all: $(TARGETS)

%: %.cpp
	$(CXX) $(CXXFLAGS) $@.cpp -o $@

clean:
	rm -f $(TARGETS)


PROCS ?= 4
NODES ?= 
ARGS ?= 
LINE = =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

run: $(TARGETS)
	@for file in $(TARGETS) ; do \
          if test -x $$file ; then \
            echo $(LINE) ; \
            ( set -x ; \
              $(UPCXXRUN) -n $(PROCS) $$file $(ARGS) ; \
            ) ; \
            echo $(LINE) ; \
          fi ; \
         done

.PHONY: all clean force run

