# UPC\+\+: a PGAS library for C\+\+ #

## Overview

[UPC++](https://upcxx.lbl.gov) is a parallel programming library for developing
C++ applications with the Partitioned Global Address Space (PGAS) model.

UPC++ has three main objectives:

* Provide an object-oriented PGAS programming model in the context of the
  popular C++ language

* Expose useful asynchronous parallel programming idioms unavailable in
  traditional SPMD models, such as remote function invocation and
  continuation-based operation completion, to support complex scientific
  applications
 
* Offer an easy on-ramp to PGAS programming through interoperability with other
  existing parallel programming systems (e.g., MPI, OpenMP, CUDA, ROCm/HIP)

# UPC++ Documentation

The rest of this document provides basic information for command-line
use of the UPC++ software implementation.

Other topics are covered in the following documents:

* Installing the UPC++ software, see: [INSTALL.md](INSTALL.md)
* Tutorial on programming with UPC++, see: [UPC++ Programmer's Guide](docs/guide.pdf)
* Formal details on UPC++ semantics, see: [UPC++ Specification](docs/spec.pdf)
* Software change history of UPC++, see: [ChangeLog.md](ChangeLog.md)
* Debugging UPC++ programs, see: [docs/debugging.md](docs/debugging.md)
* Using UPC++ and MPI in the same program, see: [docs/mpi-hybrid.md](docs/mpi-hybrid.md).
* Using UPC++ and UPC in the same program, see: [docs/upc-hybrid.md](docs/upc-hybrid.md).
* Using UPC++ with oversubscribed cores, see: [docs/oversubscription.md](docs/oversubscription.md)
* Implementation-defined behavior, see: [docs/implementation-defined.md](docs/implementation-defined.md) 
* Using UPC++ with dynamic libraries, see: [docs/ccs-rpc.md](docs/ccs-rpc.md)
* Copyright notice and licensing agreement, see: [LICENSE.txt](LICENSE.txt)

Usage information for public installs of UPC\+\+ at certain computing centers
is available [online](https://upcxx.lbl.gov/site).

To report problems or request features: [issue tracker](https://upcxx-bugs.lbl.gov).

# Compiling Against UPC\+\+ on the Command Line

With UPC\+\+ installed, the easiest way to build a UPC++ application from the
command line is to use the `upcxx` compiler wrapper, installed in 
`<upcxx-install-path>/bin/upcxx`. The arguments to this wrapper work
just like the C++ compiler used to install UPC++ (analogous to the
`mpicxx` compiler wrapper often provided for MPI/C++ programs).

For example, to build an application consisting of `my-app1.cpp` and
`my-app2.cpp`:

```bash
export PATH="<upcxx-install-path>/bin/:$PATH"
upcxx -O -c my-app1.cpp my-app2.cpp
upcxx -O -o my-app my-app1.o my-app2.o -lm
```

Be sure that all commands used to build one executable consistently pass either
a -O option to select the optimized/production version of UPC++ (for
performance runs), or a -g option to select the debugging version of UPC++
(for tracking down bugs in your application).

To select a non-default network backend or thread-safe version of the library, 
you'll need to pass the -network= or -threadmode= options, or set the
`UPCXX_NETWORK` or `UPCXX_THREADMODE` variables prior to invoking compilation.
See the 'UPC++ Backends' section below.

## Compiling Against UPC\+\+ in Makefiles

The simplest way to build UPC++ programs from a Makefile is to use the 
`upcxx` compiler wrapper documented in the section above to replace your
normal C++ compiler command.

If your Makefile structure prevents this and/or requires extraction of the 
underlying compiler flags to build against UPC++, your build process can 
query this information by invoking the
`<upcxx-install-path>/bin/upcxx-meta <what>` script, where `<what>` indicates
which form of flags are desired. Valid values are:

* `CXX`: The C++ compiler used to install UPC++, which must also be used for
  building application code.
* `CPPFLAGS`: Preprocessor flags which will put the upcxx headers in the
  compiler's search path and define macros required by those headers.
* `CXXFLAGS`: Compiler flags which set debug/optimization settings, and
  set the minimum C++ language level required by the UPC++ headers.
* `LDFLAGS`: Linker flags usually belonging at the front of the link command
  line (before the list of object files).
* `LIBS`: Linker flags belonging at the end of the link command line. These
  will make libupcxx and its dependencies available to the linker.

For example, to build an application consisting of `my-app1.cpp` and
`my-app2.cpp` using extracted arguments:

```bash
meta="<upcxx-install-path>/bin/upcxx-meta"
$($meta CXX) $($meta CPPFLAGS) $($meta CXXFLAGS) -c my-app1.cpp
$($meta CXX) $($meta CPPFLAGS) $($meta CXXFLAGS) -c my-app2.cpp
$($meta CXX) $($meta LDFLAGS) -o my-app my-app1.o my-app2.o -lm $($meta LIBS)
```

For an example of a Makefile which builds UPC++ applications, look at
[example/prog-guide/Makefile](example/prog-guide/Makefile). This directory also
has code for running all the examples given in the programmer's guide. To use
that `Makefile`, first set the `UPCXX_INSTALL` shell variable to the
`<upcxx-install-path>`.

## Using UPC++ with CMake

A UPCXX CMake package is provided in the installation directory. To use it
in a CMake project, append the UPC++ installation directory to the
`CMAKE_PREFIX_PATH` variable 
(`cmake ... -DCMAKE_PREFIX_PATH=/path/to/upcxx/install/prefix ...`), 
then use `find_package(UPCXX)` in the
CMakeLists.txt file of the project.

If it is able to find a compatible UPC++ installation, the CMake package
will define a `UPCXX:upcxx target` (as well as a `UPCXX_LIBRARIES`
variable for legacy projects) that can be added as dependency to
your project.

## UPC\+\+ Backends

UPC\+\+ provides multiple "backends" offering the user flexibility to choose the
means by which the parallel communication facilities are implemented. Those
backends are characterized by three dimensions: conduit, thread-mode, and
code-mode. All objects in a given executable must agree on the backend in
use. The conduit and thread-mode parameters map directly to the GASNet
concepts of the same name (for more explanation, see below). Code-mode selects
between highly optimized code and highly debuggable code. The `upcxx-meta`
script will assume sensible defaults for these parameters based on the
installation configuration. The following environment variables can be set to
influence which backend `upcxx-meta` selects:

* `UPCXX_NETWORK=<see below>`: The GASNet network backend ("conduit") to use
  for communication (the default and available values are system-dependent).
  Supported values include:
    * `aries` is the high-performance Cray XC network (deprecated).
    * `ibv` is the high-performance InfiniBand network.
    * `smp` is the high-performance choice for single-node multi-core runs.
    * `ofi` the libfabric backend, recommended on Omni-Path networks and HPE Cray EX systems.
    * `ucx` the experimental UCX backend for certain InfiniBand systems.
    * `udp` is a portable low-performance alternative for testing and debugging.
    * `mpi` is a portable low-performance alternative for testing and debugging. 

* `UPCXX_THREADMODE=[seq|par]`: The value `par` selects the thread-safe version
  of the library which permits any upcxx function to be called from any thread,
  within the parameters set by the specification. The value `seq` adds
  thread-safety restrictions on the majority of upcxx routines (mostly that
  communication can only be initiated by a single thread) at the benefit of
  lower library-internal overhead. See [docs/implementation-defined.md](docs/implementation-defined.md)
  for detailed requirements. The default value is always `seq`.
  
* `UPCXX_CODEMODE=[opt|debug]`: `opt` is for highly compiler-optimized
  code and is recommended for performance measurement and production runs.
  `debug` produces unoptimized code, includes extra error checking
  assertions to validate the correctness of application calls to UPC++, 
  and is annotated with the symbol tables needed by debuggers; 
  `debug` mode is recommended for all development/debugging of applications.
  The default value is `opt`.

# Running UPC\+\+ Programs

To run a parallel UPC\+\+ application, use the `upcxx-run` launcher provided in
the installation.

```bash
<upcxx-install-path>/bin/upcxx-run -n <ranks> <exe> <args...>
```

This will run the executable and arguments `<exe> <args...>` in a parallel
context with `<ranks>` number of UPC\+\+ processes.

Upon startup, each UPC\+\+ process creates a fixed-size shared memory heap that will never grow. By
default, this heap is 128 MB per process. This can be adjusted by passing a `-shared-heap` parameter
to `upcxx-run`, which takes a suffix of KB, MB or GB; e.g. to reserve 1GB per process, call:

```bash
<upcxx-install-path>/bin/upcxx-run -shared-heap 1G -n <ranks> <exe> <args...>
```

There are several additional options that can be passed to `upcxx-run`. 
See `upcxx-run -h` for a complete list of options.

--------------------------------------------------------------------------
The canonical version of this document is located here:
    https://upcxx.lbl.gov/wiki/README.md

For more information, please visit the [UPC++ home page](https://upcxx.lbl.gov)

