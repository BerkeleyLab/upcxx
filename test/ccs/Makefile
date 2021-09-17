MKFILE_PATH=$(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SOURCE_DIR:=$(MKFILE_PATH)
UPCXX_BINDIR?=$(MKFILE_PATH)/../../build3/bin
UPCXX_PYTHON?=python3
LINK_ORIGIN?=$$ORIGIN
PYTHON_INCLUDES=-I$(shell $(UPCXX_PYTHON)  -c 'from sysconfig import get_paths; print(get_paths()["include"])')
#PYTHON_INCLUDES=-I$(shell $(UPCXX_PYTHON)-config --includes')
UPCXX=$(UPCXX_BINDIR)/upcxx
UPCXX_RUN?=$(UPCXX_BINDIR)/upcxx-run
UPCXX_META?=$(UPCXX_BINDIR)/upcxx-meta
CXX?=$(CONFIG_CXX)

SHARED_FLAG=-shared
STATIC_FLAG=-static
DL_LIB=-ldl
FPIC_FLAG=-fPIC
WHOLE_ARCHIVE_FLAG?=-Wl,--whole-archive
NO_WHOLE_ARCHIVE_FLAG?=-Wl,--no-whole-archive
PTHREAD_WORKAROUND=$(WHOLE_ARCHIVE_FLAG) -lpthread $(NO_WHOLE_ARCHIVE_FLAG)
RPATH_THIS_DIR=-Wl,-rpath,'$(LINK_ORIGIN)'
RPATH_LIBUPCXX_SO_LIBDIR=-Wl,-rpath,'$(LINK_ORIGIN)/lib'
RPATH_PARENT_DIR=-Wl,-rpath,'$(LINK_ORIGIN)/..'
RPATH_PYTHON_LIBUPCXX_SO_LIBDIR=-Wl,-rpath,'$(LINK_ORIGIN)/../lib:$(LINK_ORIGIN)'
UNAME_S=$(shell uname -s)

networks:=smp udp ibv
codemodes:=opt debug
threadmodes:=seq par

define triple_rules
triple:=$(threadmode)-$(codemode)-$(network)
triple_env:=UPCXX_THREADMODE=$(threadmode) UPCXX_CODEMODE=$(codemode) UPCXX_NETWORK=$(network)
triple_upcxx_run:=$(UPCXX_RUN) -network $(network) -n 2

%-$(triple).o: $(SOURCE_DIR)/%.cxx
	$(triple_env) $$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="libccs-dlopen-test-funcs-$(triple).so" $$< -c -o $$@

%-fpic-$(triple).o: $(SOURCE_DIR)/%.cxx
	$(triple_env) $$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="libccs-dlopen-test-funcs-$(triple).so" $$(FPIC_FLAG) $$< -c -o $$@

libccs-%-$(triple).so: %-fpic-$(triple).o
ifeq ($$(UNAME_S),Darwin)
	$$(CXX) $$(SHARED_FLAG) $$^ -o $$@ -Wl,-install_name,@executable_path/$$@
else
	$$(CXX) $$(SHARED_FLAG) $$^ -o $$@
endif

## Dynamic link: libupcxx in main executable
test-ccs-dynamic-$(triple): dynamic-$(triple).o test-$(triple).o libccs-dynamic-test-funcs-$(triple).so
	$(triple_env) $$(UPCXX) $$^ -o $$@ $$(DL_LIB) $$(RPATH_THIS_DIR)

## Dynamic link: libupcxx in library
# Not supported. libupcxx should really be a separate dynamic library to use within dynamic libraries
# Requires fPIC
libccs-inlib-$(triple).so: test-fpic-$(triple).o libccs-dynamic-test-funcs-$(triple).so
ifeq ($$(UNAME_S),Darwin)
	$(triple_env) $$(UPCXX) $$(SHARED_FLAG) $$^ -o $$@ $$(DL_LIB) $$(RPATH_THIS_DIR) -Wl,-install_name,@executable_path/$$@
else
	$(triple_env) $$(UPCXX) $$(SHARED_FLAG) $$^ -o $$@ $$(DL_LIB) $$(RPATH_THIS_DIR)
endif

# Use CXX so libupcxx.a isn't linked in
test-ccs-inlib-$(triple): upcxx-inlib-fpic-$(triple).o libccs-inlib-$(triple).so
	$$(CXX) $$^ -o $$@ $$(RPATH_THIS_DIR)

## Static link with dlopen: libupcxx in executable
test-ccs-static-dlopen-$(triple): static-$(triple).o
	$(triple_env) $$(UPCXX) $$(STATIC_FLAG) $$^ -o $$@ $$(DL_LIB) $$(PTHREAD_WORKAROUND)

run-ccs-static-dlopen-$(triple): test-ccs-static-dlopen-$(triple) libccs-dlopen-test-funcs-$(triple).so
	LD_LIBRARY_PATH=. $(triple_env) $(triple_upcxx_run) $$<

## Dynamic link: libupcxx.so as separate library
# Not supported, yet
# Requires fPIC
#$$(CXX) $$(SHARED_FLAG) $$(WHOLE_ARCHIVE_FLAG) $$(LIBUPCXX_A) $(gasnet_a) $$(NO_WHOLE_ARCHIVE_FLAG) -o $$@
libupcxx-$(triple).so:
ifeq ($$(UNAME_S),Darwin)
	$$(CXX) $$(SHARED_FLAG) -Wl,-all_load $$$$($(triple_env) $$(UPCXX_META) LIBS) -o $$@ -Wl,-install_name,@executable_path/$$@
else
	$$(CXX) $$(SHARED_FLAG) $$(WHOLE_ARCHIVE_FLAG) $$$$($(triple_env) $$(UPCXX_META) LIBS) $$(NO_WHOLE_ARCHIVE_FLAG) -o $$@
endif

test-ccs-dynamic-dynupcxx-$(triple): dynamic-fpic-$(triple).o test-fpic-$(triple).o libccs-dynamic-test-funcs-$(triple).so libupcxx-$(triple).so
	$$(CXX) $$^ -o $$@ $$(DL_LIB) $$(RPATH_THIS_DIR) $$(RPATH_LIBUPCXX_SO_LIBDIR)

## Python. Dynamically linked into library
# Not supported, yet
# Requires fPIC
python-fpic-$(triple).o: $(SOURCE_DIR)/python.cxx
	$(triple_env) $$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="libccs-dlopen-test-funcs-$(triple).so" $$(FPIC_FLAG) $$(PYTHON_INCLUDES) $$^ -c -o $$@

python-$(triple)/upcxxpytest.so: python-fpic-$(triple).o test-fpic-$(triple).o libccs-dynamic-test-funcs-$(triple).so libupcxx-$(triple).so
	mkdir -p python-$(triple)
	$$(CXX) $$(SHARED_FLAG) $$^ -o $$@ $$(RPATH_PARENT_DIR) $$(RPATH_PYTHON_LIBUPCXX_SO_LIBDIR)

python-$(triple)/ccs-test.py:
	mkdir -p python-$(triple)
	cp $(SOURCE_DIR)/ccs-test.py python-$(triple)/ccs-test.py

test-ccs-python-$(triple): python-$(triple)/upcxxpytest.so python-$(triple)/ccs-test.py

run-ccs-python-$(triple): test-ccs-python-$(triple) libccs-dlopen-test-funcs-$(triple).so
	$(triple_env) $(triple_upcxx_run) $$(UPCXX_PYTHON) ./python-$(triple)/ccs-test.py

## Test asymmetric library loading exceptions
test-ccs-asymmetric%-$(triple): asymmetric%-$(triple).o
	$(triple_env) $(UPCXX) $$^ -o $$@ $$(DL_LIB) $$(RPATH_THIS_DIR)

run-ccs-%-$(triple): test-ccs-%-$(triple) libccs-dlopen-test-funcs-$(triple).so
	$(triple_env) $(triple_upcxx_run) $$<

ccs-tests := ccs-dynamic ccs-inlib ccs-static-dlopen ccs-dynamic-dynupcxx ccs-python ccs-asymmetric1 ccs-asymmetric2

build-ccs-all-$(triple): libccs-dlopen-test-funcs-$(triple).so $(addsuffix -$(triple),$(addprefix test-,$(ccs-tests)))

run-ccs-all-$(triple): $(addsuffix -$(triple),$(addprefix run-,$(ccs-tests)))

.PHONY: test-ccs-python-$(triple) test-ccs-all-$(triple) run-ccs-all-$(triple)
endef

# FIXME: The targets for the last triple don't seem to be generated. 
$(foreach threadmode,$(threadmodes), \
  $(foreach codemode,$(codemodes), \
    $(foreach network,$(networks), \
      $(eval $(triple_rules)) \
    ) \
  ) \
)

