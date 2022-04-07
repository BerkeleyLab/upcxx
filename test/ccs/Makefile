MKFILE_PATH=$(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SOURCE_DIR:=$(MKFILE_PATH)
UPCXX_BINDIR?=$(MKFILE_PATH)/../../build3/bin
UPCXX_PYTHON?=python3
LINK_ORIGIN?=$$ORIGIN
PYTHON_INCLUDES=-I$(shell $(UPCXX_PYTHON)  -c 'from sysconfig import get_paths; print(get_paths()["include"])')
PYTHON_LIBRARY_MAC=$(shell $(UPCXX_PYTHON) -c 'from distutils import sysconfig; print("%s/libpython%s.dylib" % (sysconfig.get_config_var("LIBDIR"), sysconfig.get_config_var("VERSION")))')
#PYTHON_INCLUDES=-I$(shell $(UPCXX_PYTHON)-config --includes')
UPCXX=$(UPCXX_BINDIR)/upcxx
UPCXX_RUN?=$(UPCXX_BINDIR)/upcxx-run
UPCXX_META?=$(UPCXX_BINDIR)/upcxx-meta
CXX=$(CONFIG_CXX)

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

ifeq ($(UNAME_S),Darwin)
CCS_DLOPEN_LIB="@loader_path/libccs-dlopen-test-funcs.so"
else
CCS_DLOPEN_LIB="libccs-dlopen-test-funcs.so"
endif

%.o: $(SOURCE_DIR)/%.cxx
	$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="$(CCS_DLOPEN_LIB)" $< -c -o $@

%-fpic.o: $(SOURCE_DIR)/%.cxx
	$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="$(CCS_DLOPEN_LIB)" $(FPIC_FLAG) $< -c -o $@

libccs-%.so: %-fpic.o
ifeq ($(UNAME_S),Darwin)
	$(CXX) $(SHARED_FLAG) $^ -o $@ -Wl,-install_name,@loader_path/$@
else
	$(CXX) $(SHARED_FLAG) $^ -o $@
endif

## Dynamic link: libupcxx in main executable
test-ccs-dynamic: dynamic.o test.o libccs-dynamic-test-funcs.so
	$(UPCXX) $^ -o $@ $(DL_LIB) $(RPATH_THIS_DIR)

## Dynamic link: libupcxx in library
# Not supported. libupcxx should really be a separate dynamic library to use within dynamic libraries
# Requires fPIC
libccs-inlib.so: test-fpic.o libccs-dynamic-test-funcs.so
ifeq ($(UNAME_S),Darwin)
	$(UPCXX) $(SHARED_FLAG) $^ -o $@ $(DL_LIB) $(RPATH_THIS_DIR) -Wl,-install_name,@loader_path/$@
else
	$(UPCXX) $(SHARED_FLAG) $^ -o $@ $(DL_LIB) $(RPATH_THIS_DIR)
endif

# Use CXX so libupcxx.a isn't linked in
test-ccs-inlib: upcxx-inlib-fpic.o libccs-inlib.so
ifeq ($(UNAME_S),Darwin)
	$(CXX) $^ -o $@ $(RPATH_THIS_DIR)
else
	$(CXX) $^ -o $@ $(RPATH_THIS_DIR) -Wl,--rpath-link,.
endif

## Static link with dlopen: libupcxx in executable
test-ccs-static-dlopen: static.o
	$(UPCXX) $(STATIC_FLAG) $^ -o $@ $(DL_LIB) $(PTHREAD_WORKAROUND)

run-ccs-static-dlopen: test-ccs-static-dlopen libccs-dlopen-test-funcs.so
	LD_LIBRARY_PATH=. $(UPCXX_RUN) $<

## Dynamic link: libupcxx.so as separate library
# Not supported, yet
# Requires fPIC
libupcxx.so:
ifeq ($(UNAME_S),Darwin)
	$(CXX) $(SHARED_FLAG) -Wl,-all_load $(shell $(UPCXX_META) LIBS) -o $@ -Wl,-install_name,@loader_path/$@
else
	$(CXX) $(SHARED_FLAG) $(WHOLE_ARCHIVE_FLAG) $(shell $(UPCXX_META) LIBS) $(NO_WHOLE_ARCHIVE_FLAG) -o $@
endif

test-ccs-dynamic-dynupcxx: dynamic-fpic.o test-fpic.o libccs-dynamic-test-funcs.so libupcxx.so
	$(CXX) $^ -o $@ $(DL_LIB) $(RPATH_THIS_DIR) $(RPATH_LIBUPCXX_SO_LIBDIR)

## Test asymmetric library loading exceptions
test-ccs-asymmetric%: asymmetric%.o
	$(UPCXX) $^ -o $@ $(DL_LIB) $(RPATH_THIS_DIR)

run-ccs-%: test-ccs-% libccs-dlopen-test-funcs.so
	$(UPCXX_RUN) $<


## Python. Dynamically linked into library
# Not supported, yet
# Requires fPIC
python-fpic.o: $(SOURCE_DIR)/python.cxx
	$(UPCXX) -DUPCXX_CCS_RPC=1 -DCCS_DLOPEN_LIB="libccs-dlopen-test-funcs.so" $(FPIC_FLAG) $(PYTHON_INCLUDES) $^ -c -o $@

upcxxpytest.so: python-fpic.o test-fpic.o libccs-dynamic-test-funcs.so libupcxx.so
ifeq ($(UNAME_S),Darwin)
	$(CXX) $(SHARED_FLAG) $^ -o $@ $(RPATH_THIS_DIR) $(RPATH_LIBUPCXX_SO_LIBDIR) $(PYTHON_LIBRARY_MAC)
else
	$(CXX) $(SHARED_FLAG) $^ -o $@ $(RPATH_THIS_DIR) $(RPATH_LIBUPCXX_SO_LIBDIR)
endif

ccs-test.py:
	cp $(SOURCE_DIR)/ccs-test.py ccs-test.py

test-ccs-python: upcxxpytest.so ccs-test.py

run-ccs-python: test-ccs-python libccs-dlopen-test-funcs.so
	$(UPCXX_RUN) $(UPCXX_PYTHON) ./ccs-test.py

ccs-tests := ccs-dynamic ccs-inlib ccs-static-dlopen ccs-dynamic-dynupcxx ccs-python ccs-asymmetric1 ccs-asymmetric2

build-ccs-all: libccs-dlopen-test-funcs.so $(addprefix test-,$(ccs-tests))

run-ccs-all: $(addprefix run-,$(ccs-tests))

.PHONY: test-ccs-python test-ccs-all run-ccs-all
