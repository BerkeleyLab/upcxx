# To use this makefile, set the UPCXX_INSTALL variable to the upcxx install directory, e.g.
# make UPCXX_INSTALL=<myinstalldir> all
# or (for bash)
# export UPCXX_INSTALL=<myinstalldir>; make all
 
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

NVCC ?= nvcc
HIPCC ?= hipcc
# Honor user's NVCCARCH_FLAGS (if any) warning if NVCCARCH is also set.
# Otherwise convert NVCCARCH (if any) to an -arch=... option.
ifneq ($(strip $(NVCCARCH)),)
  ifneq ($(strip $(NVCCARCH_FLAGS)),)
    $(warning Both NVCCARCH and NVCCARCH_FLAGS are set.  Ignoring NVCCARCH.)
  else
    NVCCARCH_FLAGS = -arch=$(NVCCARCH)
  endif
endif

# Honor user's HIPCCARCH_FLAGS (if any), prepending it with --amdgpu-target= if necessary.
ifneq ($(strip $(HIPCCARCH)),)
  ifneq ($(strip $(HIPCCARCH_FLAGS)),)
	$(warning Both HIPCCARCH and HIPCCARCH_FLAGS are set. Ignoring HIPCCARCH.)
    HIPCCARCH = $(HIPCCARCH_FLAGS)
  endif
  ifneq ($(strip $(findstring --amdgpu-target,$(HIPCCARCH))),)
    HIPCCARCH_FLAGS = $(HIPCCARCH)
  else
    HIPCCARCH_FLAGS:=--amdgpu-target=$(HIPCCARCH)
  endif
endif

CXXFLAGS ?= -g # optional command-line override
CUDAFLAGS = $(CXXFLAGS) -DUPCXX_GPU_DEFAULT_DEVICE_CUDA=1
HIPFLAGS = $(CXXFLAGS) -DUPCXX_GPU_DEFAULT_DEVICE_HIP=1

TARGETS = cuda_vecadd hip_vecadd
OBJS = cuda_kernels.o hip_kernels.o
default:
	@echo "Please select one of the following three targets:"
	@echo " - \`make cuda_vecadd\` makes an executable using CUDA kernels for NVIDIA GPUs"
	@echo " - \`make hip_vecadd\`  makes an executable using ROCm/HIP kernels for AMD GPUs"
	@echo " - \`make all\`         makes both executables"

all: $(TARGETS)
clean:
	rm -f $(TARGETS) $(OBJS)
.PHONY: all clean default

%.o: %.cu kernels.hpp
	$(NVCC) $(NVCCFLAGS) $(NVCCARCH_FLAGS) $(CUDAFLAGS) -c $<

cuda_vecadd: vecadd.cxx cuda_kernels.o
	$(UPCXX) $(CUDAFLAGS) $^ -o $@

%.o : %.cxx kernels.hpp
	$(HIPCC) $(HIPCCFLAGS) $(HIPCCARCH_FLAGS) $(HIPFLAGS) -c $<

hip_vecadd: vecadd.cxx hip_kernels.o
	$(UPCXX) $(HIPFLAGS) $^ -o $@
