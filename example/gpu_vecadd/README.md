# GPU Vector Addition Example

This folder contains a simple example of how to initialize and add two vectors
into a third, all of which are in GPU memory. The vectors are segmented across
all ranks used to run the executable, with validation occurring at the root. The
example only intends to demonstrate capabilities of the UPC++ features used,
not an efficient implementation of vector addition.

Three versions of the example can be compiled: one using CUDA kernels, one using 
HIP kernels, and the final one using SYCL kernels. The CUDA version must be run 
on a machine with NVIDIA GPUs. The HIP version can be run either natively on AMD 
GPUs or on NVIDIA GPUs using the HIP-over-CUDA adapter. The SYCL version must 
be run on a machine using Intel GPUs.

An instance of the `device_allocator` class is used to allocate memory on the
device. A compile flag set in the Makefile determines the correct type of device
to use.

You must have at least UPC++ version 2023.3.0 installed to compile the 
SYCL version; for the other two, only version 2021.9.5 is required.
In all cases, the UPC++ library must be configured to include memory kind 
support for the appropriate device. While the flags to configure UPC++ for the
CUDA and HIP examples are easy to determine, the flag for the SYCL example is
`--enable-ze` since it requires Level Zero support. 

To build all code, make sure to first set the `UPCXX_INSTALL` variable. e.g. 

`export UPCXX_INSTALL=<installdir>`

then:

`make cuda_vecadd`

for the CUDA executable. For just the HIP executable:

`make hip_vecadd`

For just the SYCL executable:

`make sycl_vecadd`

To compile both:

`make all`

Run these examples as usual, e.g. 

```bash
upcxx-run -n 4 ./cuda_vecadd
upcxx-run -n 4 ./hip_vecadd
upcxx-run -n 4 ./sycl_vecadd
```

## Important Flags and Environment Variables 

### CUDA version
When targeting CUDA devices it is useful to specify the `NVCCARCH` variable,
since compiling device code for the correct GPU architecture can improve performance.
Unoptimized kernels may still be generated without this flag.

### HIP version
When targeting HIP devices it is useful to specify the `HIPCCARCH` variable.
For AMD MI100 GPUs (like those found on OLCF's Spock), set 
`HIPCCARCH=gfx908`. For AMD MI250X GPUs (like those found on OLCF's 
Crusher), use `gfx90A`. Without specifying that flag HIP kernels might not be
generated.

### SYCL version
A specific SYCL architecture can be specified using the `SYCLARCH` variable.
Intel GPUs which lack native support for double-precision FP arithmetic may 
still be able to run the SYCL version of this example by setting the following
two environment variables:
```bash
export IGC_EnableDPEmulation=1
export OverrideDefaultFP64Settings=1
```

