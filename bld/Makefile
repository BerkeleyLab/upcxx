##
# Work-horse Makefile
# Intended only to be invoked indirectly by Makefle.rules
##

# Paranoia
ifeq ($(upcxx_src),)
$(error This Makefile is not intended to be used directly)
endif
ifeq ($(upcxx_bld),)
$(error This Makefile is not intended to be used directly)
endif

export SHELL = $(UPCXX_BASH)

# From configure:
top_srcdir   = $(upcxx_src)
top_builddir = $(upcxx_bld)

#defaults:
ASSERT ?= 1
OPTLEV ?= 0
DBGSYM ?= 1
UPCXX_BACKEND ?= gasnet_seq
GASNET_CONDUIT ?= $(UPCXX_DEFAULT_NETWORK)

# Derived
builddir = $(top_builddir)/bld
makefiles = \
	$(top_srcdir)/bld/Makefile.rules     \
	$(top_srcdir)/bld/Makefile           \
	$(top_srcdir)/bld/compiler.mak       \
	$(top_srcdir)/bld/sources.mak        \
	$(top_builddir)/Makefile

# libupcxx configuration space
ALL_ASSERTS  = 0 1
ALL_OPTLEVS  = 0 3   # GASNet's DEBUG and NDEBUG, respectively.  Other values reserved for future use.
ALL_DBGSYMS  = 0 1
ALL_BACKENDS = gasnet_seq gasnet_par

$(upcxx_src)/bld/%.mak: ; @: # empty rule

# GASNet configuration space
include $(top_srcdir)/bld/gasnet.mak

#
# Encode upcxx build directory name.  Used via
#   $(call UPCXXDIR_FN,$(ASSERT),$(OPTLEV),$(DBGSYM),$(UPCXX_BACKEND),$(GASNET_CONDUIT))
# or similar.
#
UPCXXDIR_FN = upcxx.assert$(1).optlev$(2).dbgsym$(3).$(4).$(5)

# UPCXXDIR_FN for current values of all params
UPCXXDIR = $(call UPCXXDIR_FN,$(ASSERT),$(OPTLEV),$(DBGSYM),$(UPCXX_BACKEND),$(GASNET_CONDUIT))

# Similar for GASNet build directory
GASNETDIR = gasnet.$(GASNET_CODEMODE)

## Support for --enable-single=...{opt,debug}
UPCXX_DBGOPT ?= debug opt
FOREACH_DBGOPT = $(addprefix $(1)-,$(UPCXX_DBGOPT))

dummy:
	@echo NO DEFAULT TARGET
force:

.PHONY: dummy force

## Human-readable GASNet conduit list on stdout, for use in error reporting
# If GASNet configure has not yet run, list supported conduits
list-networks: force
	@if [[ -n '$(CONDUITS)' ]]; then           \
	   echo "Available networks: $(CONDUITS)"; \
	 else                                      \
	   echo "Supported networks include a system-specific subset of: $(filter-out $(UNOFFICIAL_CONDUITS),$(ALL_CONDUITS))"; \
	 fi
.PHONY: list-networks


##
## Targets for lazy/incremental execution of GASNet configure and compile steps
##
.PHONY: do-gasnet-configure gasnet-single

#
# On-demand configure of a GASNet build directory (opt or debug)
# PARAMS: OPTLEV, DBGSYM, GASNET, UPCXX_CROSS
#
configure_opt=--disable-debug
configure_debug=--enable-debug
do-gasnet-configure: force
	@dir=$(GASNETDIR);                                                            \
	 if test \! -f "$$dir/gasnet_config.h" ; then                                 \
	   echo "Configuring $(GASNET_CODEMODE)-mode build of GASNet";                \
	   export GASNET_SUPPRESS_DEBUG_WARNING=1 ;                                   \
	   if test -n "$(UPCXX_CROSS)"; then                                                \
	     cmd="SRCDIR=$(GASNET) $(GASNET)/other/contrib/cross-configure-$(UPCXX_CROSS)"; \
	   else                                                                       \
	     cmd="$(GASNET)/configure";                                               \
	   fi;                                                                        \
	   cmd="$$cmd $$GASNET_CONFIGURE_ARGS";                                       \
	   cmd="$$cmd --disable-parsync --enable-seq --enable-par";                   \
	   cmd="$$cmd --enable-pthreads --disable-segment-everything";                \
	   cmd="$$cmd --enable-$(GASNET_CONDUIT) $(configure_$(GASNET_CODEMODE))";    \
	   cmd="$$cmd --with-cc='$(CONFIG_CC)' --with-cxx='$(CONFIG_CXX)'";           \
	   echo "$$cmd";                                                              \
	   mkdir -p "$$dir" && cd "$$dir" &&                                          \
	     eval "$$cmd" 2>&1 | tee config.txt; exit $${PIPESTATUS[0]};              \
	 fi
	@if test \! -f "$(GASNET_CONFIG_FRAGMENT)"; then                              \
	   $(MAKE) -C $(GASNETDIR)/$(GASNET_CONDUIT)-conduit                          \
			$(GASNET_CONDUIT)-$(GASNET_THREADMODE).mak &&                 \
	   $(MAKE) do-gasnet-mak UPCXX_FRAGMENTS="$(builddir)/$(GASNETDIR)";          \
	 fi

#
# On-demand compile of a single instance of libgasnet
# PARAMS: OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT, GASNET, UPCXX_CROSS
#

# If given a GASNet build directory in --single mode, treat it as read-only
ifeq ($(strip $(GASNET_TYPE)),build)
gasnet-single: ; @: # empty rule
else
gasnet-single: MANUAL_CFLAGS   += $(UPCXX_COLOR_CFLAGS)
gasnet-single: MANUAL_CXXFLAGS += $(UPCXX_COLOR_CXXFLAGS)
gasnet-single: do-gasnet-configure
	@$(MAKE) -C "$(GASNETDIR)/$(GASNET_CONDUIT)-conduit" $(GASNET_THREADMODE) \
	    MANUAL_CFLAGS='$(MANUAL_CFLAGS)' MANUAL_CXXFLAGS='$(MANUAL_CXXFLAGS)'
	@if test $(GASNET_CONDUIT) = udp; then \
	    $(MAKE) -C "$(GASNETDIR)/other/amudp" amudprun \
	        MANUAL_CFLAGS='$(MANUAL_CFLAGS)' MANUAL_CXXFLAGS='$(MANUAL_CXXFLAGS)'; \
	 fi
endif

#
# On-demand compile of a single instance of libgasnet by full path
#
all_libgasnet = $(foreach codemode,   opt debug,             \
                $(foreach conduit,    $(ALL_CONDUITS),       \
                $(foreach threadmode, seq par,               \
                  gasnet.$(codemode)/$(conduit)-conduit/libgasnet-$(conduit)-$(threadmode).a)))
$(all_libgasnet): force
	@tmp=`echo $@ | tr .- //`;              \
	 codemode=`echo $$tmp | cut -d/ -f2`;   \
	 conduit=`echo $$tmp | cut -d/ -f3`;    \
	 threadmode=`echo $$tmp | cut -d/ -f7`; \
	 $(MAKE) gasnet-single GASNET_CODEMODE=$$codemode GASNET_CONDUIT=$$conduit GASNET_THREADMODE=$$threadmode

##
## Indirect targets to expose parallelism
## Pattern rules don't work as desired for the dependencies.
##

.PHONY: do-gasnet-configure-opt do-gasnet-configure-debug
.PHONY: do-gasnet-all-opt       do-gasnet-all-debug
.PHONY: do-gasnet-install-opt   do-gasnet-install-debug
.PHONY: do-gasnet-clean-opt     do-gasnet-clean-debug

do-gasnet-configure-opt: force
	@$(MAKE) do-gasnet-configure GASNET_CODEMODE=opt
do-gasnet-configure-debug: force
	@$(MAKE) do-gasnet-configure GASNET_CODEMODE=debug
do-gasnet-all-opt: do-gasnet-configure-opt
	@$(MAKE) -C gasnet.opt all
do-gasnet-all-debug: do-gasnet-configure-debug
	@$(MAKE) -C gasnet.debug all
do-gasnet-install-opt: do-gasnet-all-opt
	@$(MAKE) -C gasnet.opt install prefix=$(prefix)/gasnet.opt
do-gasnet-install-debug: do-gasnet-all-debug
	@$(MAKE) -C gasnet.debug install prefix=$(prefix)/gasnet.debug
do-gasnet-clean-opt: force
	@if [[ -d gasnet.opt ]]; then $(MAKE) -C gasnet.opt clean; fi
do-gasnet-clean-debug: force
	@if [[ -d gasnet.debug ]]; then $(MAKE) -C gasnet.debug clean; fi

##
## Standard (not incremental) targets for GASNet (opt and debug)
##
.PHONY: gasnet-configure gasnet-all gasnet-install gasnet-clean gasnet-distclean

gasnet-configure: $(call FOREACH_DBGOPT,do-gasnet-configure)
gasnet-all:       $(call FOREACH_DBGOPT,do-gasnet-all)
gasnet-install:   $(call FOREACH_DBGOPT,do-gasnet-install)
gasnet-clean:     $(call FOREACH_DBGOPT,do-gasnet-clean)

ifeq ($(strip $(GASNET_TYPE)),build)
gasnet-distclean: force
	rm -f gasnet.$(UPCXX_DBGOPT)  # symlink to an external build tree
else
gasnet-distclean: force
	rm -Rf gasnet.opt gasnet.debug
endif

##
## Targets for generated scripts
## Uses define/endef, GNU Make's nearest equivalent of a heredoc
##

# Body of a bottom-level upcxx-meta (build and install)
do-upcxx-meta: force
	@file="$(DESTDIR)$(DST)/bin/upcxx-meta";        \
	 mkdir -p "$(DESTDIR)$(DST)/bin" || exit $$?;   \
	 rm -f $$file || exit $$?;                      \
	 echo "$$upcxx_meta_body" > $$file || exit $$?; \
	 chmod 755 $$file
.PHONY: do-upcxx-meta
# In --enable-single mode we favor CONFIG_{CC,CXX} over the GASNET_ values:
ifeq ($(strip $(UPCXX_DBGOPT)),$(firstword $(UPCXX_DBGOPT)))
UPCXX_META_COMPILERS := CONFIG
else
UPCXX_META_COMPILERS := GASNET
endif
RUNTIME_LIBS = $(GASNET_LIBS)
ifeq ($(UPCXX_CUDA),1)
RUNTIME_LIBS += $(UPCXX_CUDA_LIBFLAGS)
endif
# NOTE: the upcxx-meta file format below is rigid due to automated parsing
# do NOT add or remove blank lines
define upcxx_meta_body
#!/bin/sh
CC='$($(UPCXX_META_COMPILERS)_CC)'
CFLAGS='$(GASNET_CFLAGS)'
CPPFLAGS="$(UPCXX_DEFINES) -I$(DST)/include $(addprefix -I, $(wildcard $(DST)/gen_include)) $(GASNET_CXXCPPFLAGS) $$_UPCXX_EXTRA_CPPFLAGS"
CXX='$($(UPCXX_META_COMPILERS)_CXX)'
CXXFLAGS='$(UPCXX_STDCXX) $(GASNET_CXXFLAGS)'
GASNET_CONDUIT='$(GASNET_CONDUIT)'
GASNET_PREFIX='$(UPCXX_META_GASNET)'
LDFLAGS='$(GASNET_LDFLAGS)'
LIBS="-L$(DST)/lib -lupcxx $(RUNTIME_LIBS) $$_UPCXX_EXTRA_LIBS"
PPFLAGS="$$CPPFLAGS"
LIBFLAGS="$$LIBS"
GASNET_INSTALL="$$GASNET_PREFIX"

[ -n "$$1" ] && eval [ -n \"\$$$$1\" ] && eval echo \"\$$$$1\"
endef

# Body of a upcxx-run wrapper in a build dir
do-upcxx-run: force
	@file='$(DST)/bin/upcxx-run';                  \
	 mkdir -p "$(DST)/bin" || exit $$?;            \
	 rm -f "$$file" || exit $$?;                   \
	 echo "$$upcxx_run_body" > $$file || exit $$?; \
	 chmod 755 $$file
.PHONY: do-upcxx-run
define upcxx_run_body
#!$(UPCXX_BASH)
export GASNET_PREFIX='$(builddir)/$(GASNETDIR)'
exec $${UPCXX_PYTHON-$(UPCXX_PYTHON)} '$(top_srcdir)/utils/upcxx-run' "$$@"
endef

# Body of a upcxx wrapper in a build dir
# TODO: remove (with warning) any conflicting network,
# threadmode or codemode options instead of overriding
do-upcxx-script: force
	@file='$(DST)/bin/upcxx';                         \
	 mkdir -p "$(DST)/bin" || exit $$?;               \
	 rm -f "$$file" || exit $$?;                      \
	 echo "$$upcxx_script_body" > $$file || exit $$?; \
	 chmod 755 $$file
.PHONY: do-upcxx-script
define upcxx_script_body
#!$(UPCXX_BASH)
export UPCXX_META='$(builddir)/$(UPCXXDIR)/bin/upcxx-meta'
source '$(top_srcdir)/utils/upcxx.sh' "$$@" \
   -network=$(GASNET_CONDUIT) \
   -codemode=$(GASNET_CODEMODE) \
   -threadmode=$(GASNET_THREADMODE)
endef

# Don't let these templates polute the environment except when needed
ifneq ($(DST),)
export upcxx_meta_body
export upcxx_run_body
export upcxx_script_body
endif

##
## Targets for incremental builds of libupcxx
##

ifneq ($(UPCXX_FRAGMENTS),)
include $(UPCXX_FRAGMENTS)/$(GASNET_CONDUIT)-conduit/$(GASNET_CONDUIT)-$(GASNET_THREADMODE).mak
endif

include $(top_srcdir)/bld/compiler.mak
include $(top_srcdir)/bld/sources.mak

UPCXX_DEFINES := -DUPCXX_ASSERT_ENABLED=$(ASSERT) -DUPCXX_BACKEND=1
ifeq ($(strip $(UPCXX_BACKEND)),gasnet_seq)
UPCXX_DEFINES += -DUPCXX_BACKEND_GASNET_SEQ=1
else
UPCXX_DEFINES += -DUPCXX_BACKEND_GASNET_PAR=1
endif
ifeq ($(UPCXX_CUDA),1)
UPCXX_DEFINES += -DUPCXX_CUDA_ENABLED=1 $(UPCXX_CUDA_CPPFLAGS)
endif

# In developer builds, embed git-describe output in runtime.o
ifneq ($(wildcard $(top_srcdir)/.git),)
# NOTE: --git-dir breaks --dirty
UPCXX_GIT_VERSION := $(shell cd "$(top_srcdir)" && git describe --dirty --long --always 2>/dev/null | head -n 1)
ifneq ($(strip $(UPCXX_GIT_VERSION)),)
upcxx_git_version_file = backend/gasnet/watermark.cpp # consumes UPCXX_GIT_VERSION
upcxx_git_version_obj = $(notdir $(basename $(upcxx_git_version_file))).o
$(basename $(upcxx_git_version_obj))_EXTRA_FLAGS = -DUPCXX_GIT_VERSION='$(UPCXX_GIT_VERSION)'
.INTERMEDIATE: $(upcxx_git_version_obj)
endif
endif

LIBUPCXX_C_COMPILE   := $(GASNET_CC)  $(LIBUPCXX_CFLAGS) \
                        $(GASNET_CPPFLAGS)    $(GASNET_CFLAGS) \
                        $(UPCXX_DEFINES) \
                        -I$(UPCXX_DIR)/include -I$(UPCXX_DIR)/gen_include
LIBUPCXX_CXX_COMPILE := $(GASNET_CXX) $(LIBUPCXX_CXXFLAGS) \
                        $(GASNET_CXXCPPFLAGS) $(GASNET_CXXFLAGS) \
                        $(UPCXX_STDCXX) $(UPCXX_DEFINES) \
                        -I$(UPCXX_DIR)/include -I$(UPCXX_DIR)/gen_include

%.o: %.c   %.dep; $(LIBUPCXX_C_COMPILE)   $($*_EXTRA_FLAGS) $(UPCXX_COLOR_CFLAGS)   -c $< -o $@
%.o: %.cpp %.dep; $(LIBUPCXX_CXX_COMPILE) $($*_EXTRA_FLAGS) $(UPCXX_COLOR_CXXFLAGS) -c $< -o $@

#
# Dependency tracking:
#

# Rules to generate a dependency file
# One cannot 'include' such a file if any depdendencies are broken/removed
%.d: %.c  ; $(call UPCXX_DEP_GEN,$(LIBUPCXX_C_COMPILE),  $@,$<,$($*_EXTRA_FLAGS)) > $@
%.d: %.cpp; $(call UPCXX_DEP_GEN,$(LIBUPCXX_CXX_COMPILE),$@,$<,$($*_EXTRA_FLAGS)) > $@

# do-libupcxx-deps:
# Generate up-to-date .dep files as copies of .d files
# Care is taken to rebuild .d if missing, out-of-date OR its dependencies are broken
libupcxx_deps = $(addsuffix .d_,$(notdir $(basename $(libupcxx_sources))) upcxx_headers)
do-libupcxx-deps: $(libupcxx_deps)
$(libupcxx_deps): force
	@dot_dep=$(@:.d_=.dep) dot_d=$(@:.d_=.d) do_build=0;       \
	 if [[ ! -f $$dot_dep ]]; then                             \
	   do_build=1;                                             \
	 elif ! $(MAKE) -s -f $(builddir)/Makefile $$dot_d; then   \
	   echo "WARNING: forced rebuild of broken $$dot_d";       \
	   rm -f $$dot_d;                                          \
	   do_build=1;                                             \
	 fi;                                                       \
	 if (( $$do_build )); then                                 \
	   $(MAKE) -f $(builddir)/Makefile $$dot_d UPCXX_NO_DEPS=1 || exit $$?; \
	 fi;                                                       \
	 if [[ $$dot_d -nt $$dot_dep ]]; then                      \
	   cp $$dot_d tmp$$$$ && mv tmp$$$$ $$dot_dep || exit $$?; \
	 fi

# Include "clean" dependency files
ifeq ($(UPCXX_NO_DEPS),)
ifneq ($(wildcard *.dep),)
include $(wildcard *.dep)
endif
endif

#
# Generated configuration header (.hpp) and fragment (.mak)
# TODO: dependencies to rebuild when something (what?) changes in GASNet
#

include $(top_srcdir)/bld/config.mak

UPCXX_CONFIG_HEADER = gen_include/upcxx/upcxx_config.hpp

UPCXX_CONFIG_SCRIPTS_FULL = \
	$(addprefix $(top_srcdir)/utils/config/upcxx/, $(UPCXX_CONFIG_SCRIPTS))

$(UPCXX_CONFIG_HEADER): $(UPCXX_CONFIG_SCRIPTS_FULL)
	@mkdir -p $(dir $@)
	@export $(UPCXX_CONFIG_VARS)                          \
	        $(foreach var,$(UPCXX_CONFIG_FRAG_VARS),GASNET_$(var)='$(GASNET_$(var))'); \
	 ( echo '#ifndef _UPCXX_CONFIG_HPP';                  \
	   echo '#define _UPCXX_CONFIG_HPP 1';                \
	   echo ;                                             \
	   for script in $(UPCXX_CONFIG_SCRIPTS_FULL); do     \
	     $(SHELL) $$script;                               \
	     if [[ $$? != 0 ]]; then                          \
	       echo "Error running $$script" >& 2;            \
	       rm -f $@;                                      \
	       exit 1;                                        \
	     fi;                                              \
	   done;                                              \
	   echo ;                                             \
	   echo '#endif // _UPCXX_CONFIG_HPP';                \
	 ) >| $@

GASNET_CONFIG_SCRIPTS_FULL = \
	$(addprefix $(top_srcdir)/utils/config/gasnet/, $(GASNET_CONFIG_SCRIPTS))

do-gasnet-mak: force
	@export $(UPCXX_CONFIG_VARS)                          \
	        $(foreach var,$(UPCXX_CONFIG_FRAG_VARS),GASNET_$(var)='$(GASNET_$(var))'); \
	 cd $(UPCXX_FRAGMENTS);                               \
	 ( echo '# This file is auto-generated. Do not edit.';\
	   echo ;                                             \
	   for script in $(GASNET_CONFIG_SCRIPTS_FULL); do    \
	     $(SHELL) $$script;                               \
	     if [[ $$? != 0 ]]; then                          \
	       echo "Error running $$script" >& 2;            \
	       rm -f $(GASNET_CONFIG_FRAGMENT);               \
	       exit 1;                                        \
	     fi;                                              \
	   done;                                              \
	 ) >| $(GASNET_CONFIG_FRAGMENT)
.PHONY: do-gasnet-mak

# Panic button to zap generated upcxx_config.hpp files
.PHONY: upcxx-unconfig
upcxx-unconfig: force
	rm -f $(addsuffix /$(UPCXX_CONFIG_HEADER),        \
	        $(foreach assert,  $(ALL_ASSERTS),        \
	        $(foreach optlev,  $(ALL_OPTLEVS),        \
	        $(foreach dbgsym,  $(ALL_DBGSYMS),        \
	        $(foreach backend, $(ALL_BACKENDS),       \
	        $(foreach conduit, $(ALL_CONDUITS),       \
	          $(call UPCXXDIR_FN,$(assert),$(optlev),$(dbgsym),$(backend),$(conduit))))))))
	rm -f gasnet.opt.mak gasnet.dbg.mak

#
# Build upcxx library from its sources, with dependency tracking
# Also populates a bin/ with scripts suited for use in build tree
#
VPATH = $(addprefix $(top_srcdir)/src/, $(sort $(dir $(libupcxx_sources))))
libupcxx_objs = $(addsuffix .o,$(notdir $(basename $(libupcxx_sources))))
$(libupcxx_objs): $(makefiles)
libupcxx.a: $(libupcxx_objs)
	ar rcs $@ $?
do-libupcxx: force
	@$(MAKE) -C $(UPCXX_DIR)/ -f "$(builddir)/Makefile" $(UPCXX_CONFIG_HEADER)
	@$(MAKE) -C $(UPCXX_DIR)/lib -f "$(builddir)/Makefile" do-libupcxx-deps
	@$(MAKE) -C $(UPCXX_DIR)/lib -f "$(builddir)/Makefile" libupcxx.a upcxx_headers.d
	@$(MAKE) DST=$(UPCXX_DIR) UPCXX_META_GASNET='$(builddir)/$(GASNETDIR)' \
	         do-upcxx-meta do-upcxx-run do-upcxx-script

#
# On-demand compile of a single instance of libupcxx and required libgasnet
# PARAMS: ASSERT, OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT, GASNET, UPCXX_CROSS
#
do-upcxx-single: force
	@dir=$(UPCXXDIR);                                     \
	 mkdir -p $$dir/lib && mkdir -p $$dir/include &&      \
	 rm -f $$dir/include/upcxx &&                         \
	 ln -s "$(top_srcdir)/src" $$dir/include/upcxx &&     \
	 $(MAKE) do-libupcxx UPCXX_DIR="$(builddir)/$$dir"
upcxx-single: gasnet-single
	@$(MAKE) do-upcxx-single UPCXX_FRAGMENTS="$(builddir)/$(GASNETDIR)"

.PHONY: do-upcxx-single upcxx-single

#
# On-demand compile of a single instance of libupcxx by fullpath
#
all_libupcxx = $(foreach assert,  $(ALL_ASSERTS),        \
               $(foreach optlev,  $(ALL_OPTLEVS),        \
               $(foreach dbgsym,  $(ALL_DBGSYMS),        \
               $(foreach backend, $(ALL_BACKENDS),       \
               $(foreach conduit, $(ALL_CONDUITS),       \
	         $(call UPCXXDIR_FN,$(assert),$(optlev),$(dbgsym),$(backend),$(conduit))/libupcxx.a)))))
$(all_libupcxx): force
	@tmp=`echo $@ | tr . /`; \
	 assert=`expr "$$tmp" : '.*/assert\([0-1]\)/'`; \
	 optlev=`expr "$$tmp" : '.*/optlev\([0-3]\)/'`; \
	 dbgsym=`expr "$$tmp" : '.*/dbgsym\([0-1]\)/'`; \
	 backend=`echo $$tmp | cut -d/ -f5`; \
	 conduit=`echo $$tmp | cut -d/ -f6`; \
	 $(MAKE) upcxx-single ASSERT=$$assert OPTLEV=$$optlev DBGSYM=$$dbgsym \
                              UPCXX_BACKEND=$$backend GASNET_CONDUIT=$$conduit

##
## Standard (not incremental) targets for upcxx install
## Note that these just cover UPCXX_CODEMODE of opt or debug
##
.PHONY: upcxx-all upcxx-install upcxx-clean upcxx-distclean
.PHONY: do-upcxx-all do-upcxx-install-one do-upcxx-install-all

#
# Helpers to iterate over conduits and backends
#

# Normal headers for public install are automatically crawled from upcxx_headers.cpp
# This variable lists headers that should be installed but can't be crawled from there
# (for example because they aren't guaranteed to pass preprocess at install time)

UPCXX_NOCRAWL_INSTALL_HEADERS = # none

do-upcxx-all: force
	@$(MAKE) do-gasnet-configure
	@targets='';                                                           \
	 for conduit in $(CONDUITS); do                                        \
	   for backend in $(ALL_BACKENDS); do                                  \
	     targets="$$targets $(call UPCXXDIR_FN,$(ASSERT),$(OPTLEV),$(DBGSYM),$$backend,$$conduit)/libupcxx.a";\
	   done;                                                               \
	 done;                                                                 \
	 $(MAKE) $$targets
do-upcxx-install-headers: force
	@cd "$(UPCXXDIR)/include/upcxx/" &&                               \
	 depfile="$(builddir)/$(UPCXXDIR)/lib/upcxx_headers.d"            \
	 dst="$(DESTDIR)$(DST)/include/upcxx" src="$$(pwd -P)" &&         \
	 for file in $$(find . -name '*.hpp'); do                         \
	   file="$${file##./}";                                           \
	   if fgrep -e "/bld/$(UPCXXDIR)/include/upcxx/$$file"            \
		    -e "$$src/$$file" "$$depfile" >/dev/null ||           \
	      fgrep -e " $$file " >/dev/null                              \
	            <<< " $(UPCXX_NOCRAWL_INSTALL_HEADERS) "; then        \
	     mkdir -p "$$dst/$$(dirname $$file)" || exit $$?;             \
	     cp $$file "$$dst/$$file" || exit $$?;                        \
	   fi;                                                            \
	 done;                                                            \
	 cp "$(builddir)/$(UPCXXDIR)/gen_include/upcxx/"*.hpp "$$dst/"
do-upcxx-install-lib: force
	@mkdir -p "$(DESTDIR)$(DST)/lib" || exit $$?;                     \
	 cp "$(UPCXXDIR)/lib/libupcxx.a" "$(DESTDIR)$(DST)/lib/"
do-upcxx-install-one: force
	@$(MAKE) do-upcxx-install-headers do-upcxx-install-lib \
	         do-upcxx-meta UPCXX_META_GASNET='$(prefix)/$(GASNETDIR)'
do-upcxx-install-all: force
	@for conduit in $(CONDUITS); do                                      \
	   for backend in $(ALL_BACKENDS); do                                \
	     $(MAKE) do-upcxx-install-one                                    \
	       DST="$(prefix)/upcxx.$(UPCXX_CODEMODE).$$backend.$$conduit"   \
	       UPCXX_BACKEND=$$backend GASNET_CONDUIT=$$conduit || exit $$?; \
	   done; \
	 done

#
# Opt and debug targets for upcxx
#

.PHONY: do-upcxx-all-opt       do-upcxx-all-debug
.PHONY: do-upcxx-install-opt   do-upcxx-install-debug
.PHONY: do-upcxx-clean-opt     do-upcxx-clean-debug

do-upcxx-all-opt: force
	@$(MAKE) do-upcxx-all ASSERT=0 OPTLEV=3 DBGSYM=0
do-upcxx-all-debug: force
	@$(MAKE) do-upcxx-all ASSERT=1 OPTLEV=0 DBGSYM=1
do-upcxx-install-opt: do-upcxx-all-opt
	@$(MAKE) do-upcxx-install-all \
	         ASSERT=0 OPTLEV=3 DBGSYM=0 UPCXX_CODEMODE=opt    \
	         UPCXX_FRAGMENTS="$(DESTDIR)$(prefix)/gasnet.opt/include"
do-upcxx-install-debug: do-upcxx-all-debug
	@$(MAKE) do-upcxx-install-all \
	         ASSERT=1 OPTLEV=0 DBGSYM=1 UPCXX_CODEMODE=debug \
	         UPCXX_FRAGMENTS="$(DESTDIR)$(prefix)/gasnet.debug/include"
do-upcxx-clean-opt: force
	rm -Rf $(foreach backend, $(ALL_BACKENDS),       \
               $(foreach conduit, $(ALL_CONDUITS),       \
	         $(call UPCXXDIR_FN,0,3,0,$(backend),$(conduit))))
	rm -f gasnet.opt.mak
do-upcxx-clean-debug: force
	rm -Rf $(foreach backend, $(ALL_BACKENDS),       \
               $(foreach conduit, $(ALL_CONDUITS),       \
	         $(call UPCXXDIR_FN,1,0,1,$(backend),$(conduit))))
	rm -f gasnet.debug.mak

#
# Standard all, install, clean and distclean
# There is no configure
#

upcxx-all:     $(call FOREACH_DBGOPT,do-upcxx-all)
upcxx-install: $(call FOREACH_DBGOPT,do-upcxx-install)
	@if [[ '$(UPCXX_VALGRIND)' = 1 ]] ; then \
	   set -e ; set -x ; \
	   mkdir -p "$(DESTDIR)$(prefix)/lib/valgrind" ; \
	   cp -f "$(upcxx_src)/utils/valgrind/upcxx.supp" "$(DESTDIR)$(prefix)/lib/valgrind" ; \
	 fi
	@$(MAKE) -f $(upcxx_src)/bld/Makefile.tests do-post-install-test-script
upcxx-clean:   $(call FOREACH_DBGOPT,do-upcxx-clean)
upcxx-distclean: do-upcxx-clean-opt do-upcxx-clean-debug # Always both

##
## Secondary (opt and debug) top-level targets
##

all-opt all-debug install-opt install-debug clean-opt clean-debug: force
	@$(MAKE) do-gasnet-$@
	@$(MAKE) do-upcxx-$@

##
## Primary top-level targets
##

all install clean distclean: force
	@$(MAKE) gasnet-$@
	@$(MAKE) upcxx-$@

##
## "make exe SRC='foo.cpp' [EXTRAFLAGS=-Dfoo=bar]"
## Generated full path to resulting executable on stdout
##
## Correctly supports absolute and relative paths
## PARAMS: ASSERT, OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT
## OPTIONAL PARAMS: EXTRAFLAGS
##

# Hash absolute path of $(SRC), plus any "influential" variables to form the executable name
# The library variant is encoded in $(UPCXXDIR)
UPCXX_EXE_CSUM_VARS = EXTRAFLAGS MANUAL_DEFINES MANUAL_CFLAGS MANUAL_CXXFLAGS MANUAL_LDFLAGS MANUAL_LIBS
UPCXX_EXE_CSUM_VALS = $(foreach var,$(UPCXX_EXE_CSUM_VARS),$($(var)))
SRCPATH = $(shell cd $(STARTDIR) && cd $(dir $(SRC)) && pwd)/$(notdir $(SRC))
UPCXX_EXE_CSUM = $(firstword $(shell $(UPCXX_CSUMCMD) <<< '$(SRCPATH) $(UPCXX_EXE_CSUM_VALS)'))
UPCXX_EXE_STEM = $(builddir)/$(UPCXXDIR)/exe/$(UPCXX_EXE_CSUM)
UPCXX_EXE      = $(UPCXX_EXE_STEM)$(EXESUFFIX)

do-exe: force
	@mkdir -p $(UPCXXDIR)/exe
	@if [[ -z '$(UPCXX_EXE_CSUM)' ]]; then \
	   echo "Developer targets 'exe' and 'run' are unavailable (missing or broken checksum utility)"; \
	   exit 1;  \
	 fi
	@$(MAKE) upcxx-single
	@$(MAKE) -C "$(STARTDIR)" -f $(top_srcdir)/bld/Makefile-exe.mak \
		GASNET_CC_FAMILY='$(GASNET_CC_FAMILY)' GASNET_CC_SUBFAMILY='$(GASNET_CC_SUBFAMILY)' \
		GASNET_CXX_FAMILY='$(GASNET_CXX_FAMILY)' GASNET_CXX_SUBFAMILY='$(GASNET_CXX_SUBFAMILY)' \
		UPCXX_DIR='$(builddir)/$(UPCXXDIR)' UPCXX_EXE='$(UPCXX_EXE)'

# NOTE: only the final $(UPCXX_EXE) may go to stdout
exe: force
	@$(MAKE) do-exe >&2
	@echo -n '$(UPCXX_EXE)'

# Clean exe files for current library variant
## PARAMS: ASSERT, OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT
exe-clean: force; @rm -Rf $(UPCXXDIR)/exe

# Clean exe files for all library variants
exe-clean-all: force
	rm -Rf $(foreach assert,  $(ALL_ASSERTS),        \
               $(foreach optlev,  $(ALL_OPTLEVS),        \
               $(foreach dbgsym,  $(ALL_DBGSYMS),        \
               $(foreach backend, $(ALL_BACKENDS),       \
               $(foreach conduit, $(ALL_CONDUITS),       \
	         $(call UPCXXDIR_FN,$(assert),$(optlev),$(dbgsym),$(backend),$(conduit))/exe)))))

.PHONY: do-exe exe exe-clean exe-clean-all

##
## "make run SRC='bar.cpp' [EXTRAFLAGS=-Dfoo=bar] [ARGS='arg1 arg2'] [RANKS=n]
##
## Correctly supports absolute and relative paths
## PARAMS: ASSERT, OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT
## OPTIONAL PARAMS: RANKS, ARGS, EXTRAFLAGS
##
RANKS ?= 4

run: force
	@$(MAKE) do-exe
	@export GASNET_PREFIX=$(builddir)/$(GASNETDIR); \
	 eval $(top_srcdir)/utils/upcxx-run -n $(RANKS) $(UPCXX_EXE) $(ARGS)

.PHONY: run

##
## Wrappers
## Generates full path to requested wrapper on stdout
##
## PARAMS: ASSERT, OPTLEV, DBGSYM, UPCXX_BACKEND, GASNET_CONDUIT
##

upcxx-meta upcxx: force
	@[[ -n "$(UPCXX_SKIP_REBUILD)" ]] || $(MAKE) $(UPCXXDIR)/libupcxx.a >&2
	@echo -n '$(builddir)/$(UPCXXDIR)/bin/$@'

upcxx-run: force
	@[[ -n "$(UPCXX_SKIP_REBUILD)" ]] || $(MAKE) gasnet-single >&2
	@[[ -n "$(UPCXX_SKIP_REBUILD)" ]] || $(MAKE) DST='$(builddir)/$(UPCXXDIR)' do-upcxx-run >&2
	@echo -n '$(builddir)/$(UPCXXDIR)/bin/$@'

.PHONY: upcxx-meta upcxx-run upcxx
