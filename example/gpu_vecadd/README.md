# GPU Vector Addition Example

This folder contains a simple example of how to initialize and add two vectors
into a third, all of which are in GPU memory. The vectors are segmented across
all ranks used to run the executable, with validation occurring at the root. The
example only intends to demonstrate capabilities of the UPC++ features used,
not an efficient implementation of vector addition.

Two versions of the example can be compiled: one using CUDA kernels and the 
other using HIP kernels. The CUDA version must be run on a machine with NVIDIA
GPUs, while the HIP version can be run either natively on AMD GPUs or on NVIDIA
GPUs using the HIP-over-CUDA adapter.

An instance of the `device_allocator` class is used to allocate memory on the
device. A compile flag set in the Makefile determines the correct type of device
to use.

You must have at least UPC++ version 2021.9.5 installed to compile these
examples. To build all code, make sure to first set the `UPCXX_INSTALL`
variable. e.g. 

`export UPCXX_INSTALL=<installdir>`

then:

`make cuda_vecadd`

for the CUDA executable. For just the HIP executable:

`make hip_vecadd`

To compile both:

`make all`

Run these examples as usual, e.g. 

`upcxx-run -n 4 ./cuda_vecadd`
`upcxx-run -n 4 ./hip_vecadd`

When targeting CUDA devices it is useful to specify the `NVCCARCH` variable,
since compiling device code for the correct GPU architecture can improve performance.
There is also a `HIPCCARCH` that can be used for properly targeting a
HIP device. For AMD MI100 GPUs (like those found on OLCF's Spock), set 
`HIPCCARCH=gfx908`. For AMD MI250X GPUs (like those found on OLCF's 
Crusher), use `gfx90A`. Without specifying that flag HIP kernels might not be
generated, although that problem does not seem to exist for CUDA executables.
