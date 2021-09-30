#
# Compiler-specific settings
#
# The following are available (among others)
#   GASNET_{CC,CXX}
#   GASNET_{CC,CXX}_FAMILY
#   GASNET_{CC,CXX}_SUBFAMILY
#   GASNET_{C,CXX}FLAGS
#
# NOTE: sufficiently old GNU Make lacks 'else if*'

#
# UPCXX_STDCXX
# This is the C++ language level option appropriate to the compiler
#
# First the default:
UPCXX_STDCXX := -std=c++11
# Then compiler-specific overrides:
ifeq ($(GASNET_CXX_FAMILY),Intel)
UPCXX_STDCXX := -std=c++14
endif
# Then throw it all away if $CXX already specifies a language level:
ifneq ($(findstring -std=c++,$(GASNET_CXX))$(findstring -std=gnu++,$(GASNET_CXX)),)
UPCXX_STDCXX :=
endif

#
# LIBUPCXX_CFLAGS
# Any CFLAGS specific to compilation of objects in libupcxx
#
LIBUPCXX_CFLAGS := -Wall

# PGI and NVHPC:
# + strip unsupported `-Wall`
ifneq ($(filter PGI NVHPC,$(GASNET_CC_FAMILY)),)
LIBUPCXX_CFLAGS := $(filter-out -Wall,$(LIBUPCXX_CFLAGS))
endif

# Deal w/ (unsupported) nvcc -Xcompiler mess
ifeq ($(GASNET_CC_SUBFAMILY),NVIDIA)
LIBUPCXX_CFLAGS := $(patsubst %,-Xcompiler %,$(LIBUPCXX_CFLAGS))
endif

#
# LIBUPCXX_CXXFLAGS
# Any CXXFLAGS specific to compilation of objects in libupcxx
#
LIBUPCXX_CXXFLAGS := -Wall

# PGI and NVHPC:
# + strip unsupported `-Wall`
# + address issue #286 (bogus warning on future/core.cpp)
ifneq ($(filter PGI NVHPC,$(GASNET_CXX_FAMILY)),)
LIBUPCXX_CXXFLAGS := $(filter-out -Wall,$(LIBUPCXX_CXXFLAGS)) --diag_suppress1427
endif

# Intel:
# + address issue #286 (bogus warning on future/core.cpp)  -Wextra only
ifeq ($(GASNET_CXX_FAMILY),Intel)
ifneq ($(findstring -Wextra,$(GASNET_CXX) $(GASNET_CXXFLAGS)),)
LIBUPCXX_CXXFLAGS += -Wno-invalid-offsetof
endif
endif

# Deal w/ (unsupported) nvcc -Xcompiler mess
ifeq ($(GASNET_CXX_SUBFAMILY),NVIDIA)
LIBUPCXX_CXXFLAGS := $(patsubst %,-Xcompiler %,$(LIBUPCXX_CXXFLAGS))
endif

#
# UPCXX_DEP_GEN
# Incantation to generate a dependency file on stdout.
# The resulting output will name DEPFILE as a target, dependent on all files
# visited in preprocess of SRCFILE.  This ensures the dependency file is kept
# up-to-date, regenerating if any of those visitied files changes.  Therefore,
# the corresponding object or executable only needs to depend on the generated
# dependency file.
#
# Abstract "prototype":
#   $(call UPCXX_DEP_GEN,COMPILER_AND_FLAGS,DEPFILE,SRCFILE,EXTRA_FLAGS)
# Simple example (though general case lacks the common `basename`):
#   $(call UPCXX_DEP_GEN,$(CXX) $(CXXFLAGS),$(basename).d,$(basename).cpp,$(EXTRA_FLAGS))
#
# Note 1: Generation to stdout is used because PGI/NVHPC compilers ignore `-o foo` in
# the presence `-E` and lack support for `-MF`.  Meanwhile all supported
# compilers send `-E` output to stdout by default.
#
# Some explanation of the default flags, extracted from gcc man page:
#   -M
#       Instead of outputting the result of preprocessing, output a rule
#       suitable for make describing the dependencies [...]
#   -MM
#       [Same as -M but excluding headers in system directories, or included
#        from them.  Greatly reduces `stat` calls.]
#   -MT target
#       Change the target of the rule emitted by dependency generation. [...]
#
UPCXX_DEP_GEN_FLAGS = -MM -MT $(2)
UPCXX_DEP_GEN = $(1) -E $(UPCXX_DEP_GEN_FLAGS) $(3) $(4)

# With PGI and NVHPC we use `-M` in lieu of `-MM`.
# The `-MM` flag is just plain broken with this compiler family
# (emits the compiler's own pre-includes as the only dependencies).
ifneq ($(filter PGI NVHPC,$(GASNET_CXX_FAMILY)),)
# PGI and NVHPC:
UPCXX_DEP_GEN_FLAGS = -M -MT $(2)
endif

# Special case for CXX=nvcc (not officially supported)
ifeq ($(GASNET_CXX_SUBFAMILY),NVIDIA)
UPCXX_DEP_GEN = $(1) -E -Xcompiler "$(UPCXX_DEP_GEN_FLAGS)" $(3) $(4)
endif
