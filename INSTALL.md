# UPC\+\+ Installation #

This file documents software installation of [UPC++](https://upcxx.lbl.gov).

For information on using UPC++, see: [README.md](README.md)    

## System Requirements

### Supported Platforms

UPC++ makes aggressive use of template meta-programming techniques, and requires
a modern C++11/14 compiler and corresponding STL implementation.

The current release is known to work on the following configurations:

* Apple macOS/x86\_64 (smp and udp conduits):
    - The most recent Xcode release for each macOS release is well-tested
        + It is suspected that any Xcode (ie Apple clang) release 8.0 or newer will work
    - Free Software Foundation g++ (e.g., as installed by Homebrew or Fink)
      version 6.4.0 or newer should also work

* Linux/x86\_64 with one of the following compilers:
    - g++ 6.4.0 or newer    
    - clang++ 4.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - Intel C++ 17.0.2 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - PGI C++ 19.1 or newer (with libstdc++ from g++ 6.4.0 or newer), except that
      PGI (aka NVIDIA HPC SDK) 20.7 and newer are NOT currently supported  

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

* Linux/ppc64le (aka IBM POWER little-endian) with one of the following compilers:
    - g++ 6.4.0 or newer
    - clang++ 5.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - PGI C++ 18.10 or newer (with libstdc++ from g++ 6.4.0 or newer), except that
      PGI (aka NVIDIA HPC SDK) 20.7 and newer are NOT currently supported  

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

* Linux/aarch64 (aka "arm64" or "armv8") with one of the following compilers:
    - g++ 6.4.0 or newer
    - clang++ 4.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)   

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

    Note the GPUDirect drivers necessary for GDR-accelerated memory kinds on
    InfiniBand are not supported on the Linux/aarch64 platform.

* Cray XC/x86\_64 with one of the following PrgEnv environment modules and
  its dependencies (smp and aries conduits):
    - PrgEnv-gnu with gcc/7.1.0 (or later) loaded.
    - PrgEnv-intel with intel/18.0.1 and gcc/7.1.0 (or later) loaded.
    - PrgEnv-cray with cce/9.0.0 (or later) loaded.
      Note that does not include support for "cce/9.x.y-classic".

    ALCF's PrgEnv-llvm is also supported on the Cray XC.  Unlike Cray's
    PrgEnv-\* modules, PrgEnv-llvm is versioned to match the llvm toolchain
    it includes, rather than the Cray PE version.  UPC++ has been tested
    against PrgEnv-llvm/4.0 (clang++ 4.0) and newer.  When using PrgEnv-llvm,
    it is recommended to `module unload xalt` to avoid a large volume of
    verbose linker output in this configuration.  Mixing with OpenMP in this
    configuration is not currently supported.  (smp and aries conduits).

* NOT officially supported:  
    - Apple macOS/aarch64 (aka "Apple M1" and "Apple Silicon")  
      Initial testing on this platform with both Xcode and Free Software
      Foundation g++ show functionally complete and correct operation.  
      Nothing platform-specific has been implemented for the mix of
      "performance" and "efficiency" cores, meaning performance could be
      highly variable.  
      At this time we consider it premature to list this platform as
      "supported", and the `configure` script will issue a warning.
    - Vendor-specific `clang++` or `g++` variants.  
      At least Arm Ltd., Intel and AMD provide compilers based on their own
      modifications to Clang/LLVM.  Similarly, at least Arm Ltd. and IBM
      provide forks of `g++`.  
      To the best of our limited current knowledge, these all behave as their
      respective "upstream" compilers, with no additional compiler-specific
      issues.  
      At this time we do not consider these compilers to be officially
      supported due to insufficient periodic automated testing.  
      The presence or absence of a warning from `configure` varies.
    - NVIDIA HPC SDK compilers (aka PGI 20.7 and newer)  
      The NVIDIA-branded host compilers (`pgc++` or `nvc++`) are NOT currently
      supported due to critical bugs.
      This refers to the host compilers previously branded as PGI, and should
      not be confused with `nvcc`, the CUDA compiler driver.

### Miscellaneous software requirements:

* Python3 or Python2 version 2.7.5 or newer

* Perl version 5.005 or newer

* GNU Bash 3.2 or newer (must be installed, user's shell doesn't matter)

* GNU Make 3.80 or newer

* The following standard Unix tools: 'awk', 'sed', 'env', 'basename', 'dirname'

### Linux Compiler Notes:

* If /usr/bin/g++ is older than 6.4.0 (even if using a different C++
  compiler for UPC++) please read [docs/local-gcc.md](docs/local-gcc.md).

* If using a non-GNU compiler with /usr/bin/g++ older than 6.4.0, please also
  read [docs/alt-compilers.md](docs/alt-compilers.md).

## Installation Instructions

The recipe for building and installing UPC\+\+ is the same as many packages
using the GNU Autoconf and Automake infrastructure (though UPC\+\+ does not
use either).  The high-level steps are as follows:

1. `configure`  
     Configures UPC\+\+ with key settings such as the installation location
2. `make all`  
     Compiles the UPC\+\+ package
3. `make check` (optional, but recommended)  
     Verifies the correctness of the UPC\+\+ build prior to its installation
4. `make install`  
     Installs the UPC\+\+ package to the user-specified location
5. `make test_install` (optionally, but highly recommended)  
     Verifies the installed package
6. Post-install recommendations

The following numbered sections provide detailed descriptions of each step above.
Following those are sections with platform-specific instructions.

#### 1. Configuring UPC\+\+

```bash
cd <upcxx-source-dir>
./configure  --prefix=<upcxx-install-path>
```

Or, to have distinct source and build trees (for instance to compile multiple
configurations from a common source directory):
```bash
mkdir <upcxx-build-path>
cd <upcxx-build-path>
<upcxx-source-path>/configure  --prefix=<upcxx-install-path>
```

This will configure the UPC\+\+ library to be installed to the given
`<upcxx-install-path>` directory. Users are recommended to use paths to
non-existent or empty directories as the installation path so that
uninstallation is as trivial as `rm -rf <upcxx-install-path>`.

Depending on the platform, additional command-line arguments may be necessary
when invoking `configure`. For guidance, see the platform-specific instructions
in the following sections, below:

* [Configuration: Cray XC](#markdown-header-configuration-cray-xc)
* [Configuration: Linux](#markdown-header-configuration-linux)
* [Configuration: Apple macOS](#markdown-header-configuration-apple-macos)
* [Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support)

Running `<upcxx-source-path>/configure --help` will provide general
information on the available configuration options, and similar information is
provided in the [Advanced Configuration](#markdown-header-advanced-configuration)
section below.

If you are using a source tarball release downloaded from the website, it
should include an embedded copy of GASNet-EX and `configure` will default to
using that.  However if you are using a git clone or other repo snapshot of
UPC++, then `configure` may default to downloading the GASNet-EX communication
library, in which case an Internet connection is needed at configuration time.

GNU Make 3.80 or newer is required to build UPC\+\+.  If neither `make` nor
`gmake` in your `$PATH` meets this requirement, you may use `--with-gmake=...`
to specify the full path to an appropriate version.  You may need to
substitute `gmake`, or your configured value, for `make` where it appears in
the following steps.  The final output from `configure` will provide the
appropriate commands.

Python3 or Python2 (version 2.7.5 or later) is required by UPC\+\+.  By
default, `configure` searches `$PATH` for several common Python interpreter
names.  If that does not produce a suitable interpreter, you may override
this using `--with-python=...` to specify a python interpreter.  If you
provide a full path, the value is used as given.  Otherwise, the `$PATH` at
configure-time is searched to produce a full path.  Either way, the resulting
full path to the python interpreter will be used in the installed `upcxx-run`
script, rather than a runtime search of `$PATH`.  Therefore, the interpreter
specified must be available in a batch-job environment where applicable.

Bash 3.2 or newer is required by UPC\+\+ scripts, including `configure`.  By
default, `configure` will try `/bin/sh` and then the first instance of `bash`
found in `$PATH`.  If neither of these is bash 3.2 (or newer), or if the one
found is not appropriate to use (for instance not accessible on compute
nodes), one can override the automated selection by invoking `configure` _via_
the desired instance of `bash`:
```bash
/usr/gnu/bin/bash <upcxx-source-path>/configure ...
```

#### 2. Compiling UPC\+\+

```bash
make all
```

This will compile the UPC\+\+ runtime libraries, including the GASNet-EX
communications runtime.  One may run, for instance, `make -j8 all` to build
with eight concurrent processes.  This may significantly reduce the time
required. However parallel make can also obscure error messages, so if you
encounter a failure you should retry without a `-j` option.

#### 3. Testing the UPC\+\+ build (optional)

Though it is not required, we recommend testing the completeness and correctness
of the UPC\+\+ build before proceeding to the installation step.  In general
the environment used to compile UPC\+\+ tests and run them may not be the
same (most notably, on batch-scheduled and/or cross-compiled platforms).
The following command assumes it is invoked in an environment suitable for *both*,
if such is available:

```bash
make check
```

This compiles all available tests for the default network and then runs them.
One can override the default network by appending `NETWORKS=net1,net2`
to this command, with network names (such as `smp`, `udp`, `ibv` or `aries`)
substituted for the `netN` placeholders.

Setting of `NETWORKS` to restrict what is tested may be necessary, for
instance, if GASNet-EX detected libraries for a network not physically present
in your system.  This will often occur for InfiniBand (which GASNet-EX
identifies as `ibv`) due to presence of the associated libraries on many Linux
distributions.  One may, if desired, return to the configure step and pass
`--disable-ibv` (or other undesired network) to remove support for a given
network from the build of UPC\+\+.

By default the test-running step runs each test with a 5 minute time limit
(assuming the `timeout` command from GNU coreutils appears in `$PATH`).
If any tests terminate with `FAILED (exitcode=124): probable timeout`, this
indicates a timeout (which might happen in environments with very slow hardware
or slow job launch). The simplest workaround in such cases is to set
`TIMEOUT=false` to disable the timeout. Alternatively, one can set envvar
`UPCXX_RUN_TIME_LIMIT` to a value in seconds to enforce a longer timeout.

Variables `TESTS` and `NO_TESTS` can optionally be set to a space-delimited
list of test name substrings used as a filter to select or discard a subset
of tests to be compiled/run. Variable `EXTRAFLAGS` can optionally inject 
upcxx compile options, eg `EXTRAFLAGS=-Werror`.

If it is not possible to both compile and run parallel applications in the
same environment, then one may apply the following two steps in place of
`make check`:

1. In an environment suited to compilation, run `make tests-clean tests`.
This will remove any test executables left over from previous attempts, and
then compiles all tests for all available networks.  One may restrict this to
a subset of the available networks by appending a setting for `NETWORKS`,
as described above for `make check`.

2. In an environment suited to execution of parallel applications, run
`make run-tests`.  As in the first step, one may set `NETWORKS` on the `make`
command line to limit the tests run to some subset of the tests built above.

#### 4. Installing the compiled UPC\+\+ package

```bash
make install [NETWORK=net]
```

This will install the UPC\+\+ runtime libraries and accompanying utilities to
the location specified via `--prefix=...` at configuration time.  If that
value is not the desired installation location, then `make install
prefix=<desired-install-directory>` may be used to override the value given at
configure time.

One may optionally pass `NETWORK=net` (replacing `net` by a supported network
name) to specify the default network (overriding `--with-default-network=...`
specified at configure time, if any).  Output at the end of the `all` and
`check` steps report the default to be used in the absence of an explict
setting, and the available networks.

#### 5. Testing the install UPC\+\+ package (optional)

```bash
make tests-clean test_install
```

This optional command removes any test executables left over from previous
attempts, and then builds a simple "Hello, World" test for each supported
network using the *installed* UPC\+\+ libraries and compiler wrapper.

At the end of the output will be instructions for running these tests if
desired.

#### 6. Post-install recommendations

After step 5 (or step 4, if skipping step 5) one may safely remove the
directory `<upcxx-source-path>` (and `<upcxx-build-path>`, if used) since they
are not needed by the installed package.

One may use the utilities `upcxx` (compiler wrapper), `upcxx-run` (launch
wrapper) and `upcxx-meta` (UPC\+\+ metadata utility) by their full path in
`<upcxx-install-path>/bin`.  However, it is common to append that directory to
one's `$PATH` environment variable (the best means to do so are beyond this
scope of this document).

Additionally, one may wish to set the environment variable `$UPCXX_INSTALL`
to `<upcxx-install-path>`, as this is assumed by several UPC\+\+ examples.

For systems using "environment modules" an example modulefile is provided
as `<upcxx-install-path>/share/modulefiles/upcxx/<upcxx-version>`.  This
sets both `$PATH` and `$UPCXX_INSTALL` as recommended above.  Consult
the documentation for the environment modules package on how to use this file.

For users of CMake 3.6 or newer, `<upcxx-install-path>/share/cmake/UPCXX`
contains a `UPCXXConfig.cmake`.  Consult CMake documentation for instructions
on use of this file.

Finally, `<upcxx-install-path>/bin/test-upcxx-install.sh` is a script which can
be run to replicate the verification performed by `make test_install` _without_
`<upcxx-source-path>` and/or `<upcxx-build-path>`.  This could be useful, for
instance, to verify permissions for a user other than the one performing the
installation.

### Configuration: Cray XC

By default, on a Cray XC logic in `configure` will automatically detect either
the SLURM or Cray ALPS job scheduler and will cross-configure for the
appropriate one.  If this auto-detection fails, you may need to explicitly
pass the appropriate value for your system:

* `--with-cross=cray-aries-slurm`: Cray XC systems using the SLURM job scheduler (srun)
* `--with-cross=cray-aries-alps`: Cray XC systems using the Cray ALPS job scheduler (aprun)

When Intel compilers are being used (usually the default for these systems),
`g++` in `$PATH` must be version 7.1.0 or newer.  If the default is too old,
then you may need to explicitly load a `gcc` environment module, e.g.:

```bash
module load gcc/7.1.0
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --with-cross=cray-aries-slurm
```

If using PrgEnv-cray, then version 9.0 or newer of the Cray compilers is
required.  This means the cce/9.0.0 or later environment module must be
loaded, and not "cce/9.0.0-classic" (the "-classic" Cray compilers are not
supported).

The `configure` script will use the `cc` and `CC` compiler aliases of the Cray
programming environment loaded.  It is *not* necessary to specify these
explicitly using `--with-cc` or `--with-cxx`.

Currently only Intel-based Cray XC systems have been tested, including Xeon
and Xeon Phi (aka "KNL").  Note that UPC++ has not yet been tested on an
ARM-based Cray XC.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: Linux

The `configure` command above will work as-is. The default compilers used will
be gcc/g++. The `--with-cc=...` and `--with-cxx=...` options may specify
alternatives to override this behavior.  Additional options providing finer
control over how UPC\+\+ is configured can be found in the
[Advanced Configuration](#markdown-header-advanced-configuration) section below.

By default ibv-conduit (InfiniBand support) will use MPI for job spawning if a
working `mpicc` is found in your `$PATH` when UPC\+\+ is built.  The same is
true for MPI, OFI and UCX conduits, if these have been enabled.  To ensure that
UPC\+\+ applications will link when one of these conduits are used, one of three
options must be chosen.  Failure to do so will typically result in an error
message at UPC\+\+ build time, directing you to this documentation.

Option 1. The most direct solution is to configure using `--with-cxx=mpicxx` (or
similar) to ensure correct linking of UPC\+\+ applications which use MPI for job
spawning.  When one *is* using MPI for job spawning, it is important that
GASNet's MPI support use a corresponding/compatible `mpicc` and `mpirun`.  In
the common case, the un-prefixed `mpicc` and `mpirun` in `$PATH` are compatible
(ie. same vendor/version/ABI) with the provided `--with-cxx=mpicxx`, in which
case nothing more should be required.  Otherwise, one may need to additionally
pass options like `--with-mpi-cc='/path/to/compatible/mpicc -options'` and/or
`--with-mpirun-cmd='/path/to/compatible/mpirun -np %N %C'`.  
Please see GASNet's mpi-conduit documentation for details.

Option 2. If any of these networks are enabled but are not necessary, one can
configure using `--disable-[network]` to disable it.  One may wish to select
this option if there is no corresponding network hardware or no interest in
using the given network API.  The case of missing hardware can often occur for
IBV when Linux distros install the corresponding development packages as
dependencies of other packages.

Option 3. If one does not require MPI for job spawning (because SSH- or
PMI-based spawning in GASNet are sufficient), then one may configure using
`--disable-mpi-compat` to eliminate the link-time dependence on MPI.
Note that this particular option does NOT work for mpi-conduit.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: Apple macOS

On macOS, the default network is "smp": multiple processes running on a single
host, communicating over shared memory.  One may specify a different default
using `--with-default-network=...` at configure time.  However, you will also
have the opportunity to make such a selection at the `make install` step.

On macOS, UPC++ defaults to using the Apple LLVM clang compiler that is part
of the Xcode Command Line Tools.

The Xcode Command Line Tools need to be installed *before* invoking `configure`,
i.e.:

```bash
xcode-select --install
```

Alternatively, the `--with-cc=...` and `--with-cxx=...` options to `configure`
may be used to specify different compilers.

In order to use a debugger on macOS, we advise you to enable "Developer
Mode".  This is a system setting, not directly related to UPC\+\+.
Developer Mode may already be enabled, for instance if one granted Xcode
permission when it asked to enable it.  If not, then an Administrator must
run `DevToolsSecurity -enable` in Terminal.  This mode allows *all* users to
use development tools, including the `lldb` debugger.  If that is not
desirable, then use of debuggers will be limited to members of the
`_developer` group.  An internet search for `macos _developer group` will
provide additional information.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: CUDA GPU support

#### System Requirements:

UPC++ includes support for RMA communication operations on memory buffers
resident in a CUDA-compatible NVIDIA GPU.  Specific requirements:

* Modern NVIDIA-branded [CUDA-compatible GPU hardware](https://developer.nvidia.com/cuda-gpus)
* NVIDIA CUDA toolkit v9.0 or later. Available for [download here](https://developer.nvidia.com/cuda-downloads).

#### Additional System Requirements for GDR-accelerated memory kinds:

This version of UPC++ supports GPUDirect RDMA (GDR) acceleration of memory kinds
on selected platforms using modern NVIDIA-branded GPUs and Mellanox-branded InfiniBand
network hardware, when using the native ibv-conduit. Additional requirements:

* Linux OS with x86-64 or ppc64le CPU (not ARM)
* Recent Mellanox-branded InfiniBand network hardware
* GPUDirect RDMA drivers installed
* ibv-conduit built from the current version of GASNet-EX (the default for this release)

When using GDR-accelerated memory kinds, calls to `upcxx::copy` will offload
the data transfer to the network adapter, streaming data directly between the
source and destination memory locations (in host or device memory on any node), 
without staging through additional memory buffers.

For all other platforms, the CUDA support in this UPC++ release utilizes a
reference implementation which has not been tuned for performance. In
particular, `upcxx::copy` will stage data transfers involving device
memory through intermediate buffers in host memory, and is expected to
underperform relative to solutions using RDMA, GPUDirect and similar
zero-copy technologies. Future versions of UPC++ will introduce
native memory kinds acceleration for additional GPU and network variants.

#### `configure` Command for Enabling CUDA GPU Support

To activate the UPC++ support for CUDA, pass `--with-cuda` to the `configure`
script:

```bash
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --with-cuda
```

This will detect whether the requirements for GDR acceleration are met and
automatically activate that feature. 
For troubleshooting installation of GASNet's GDR support, please see
[docs/memory_kinds.md](https://bitbucket.org/berkeleylab/gasnet/src/gex-2020.11.0-memory_kinds/docs/memory_kinds.md)
in the GASNet memory_kinds distribution.

`configure --with-cuda` expects to find the NVIDIA `nvcc` compiler wrapper in your `$PATH` and
will attempt to extract the correct build settings for your system.  If this
automatic extraction fails (resulting in preprocessor or linker errors
mentioning CUDA), then you may need to manually override the following
options to `configure`:

* `--with-nvcc=...`: the full path to the `nvcc` compiler wrapper from the CUDA toolkit. 
   Eg `--with-nvcc=/Developer/NVIDIA/CUDA-10.0/bin/nvcc`
* `--with-cuda-cppflags=...`: preprocessor flags to add for locating the CUDA toolkit headers.
   Eg `--with-cuda-cppflags='-I/Developer/NVIDIA/CUDA-10.0/include'`
* `--with-cuda-libflags=...`: linker flags to use for linking CUDA executables.
   Eg `--with-cuda-libflags='-Xlinker -force_load -Xlinker /Developer/NVIDIA/CUDA-10.0/lib/libcudart_static.a -L/Developer/NVIDIA/CUDA-10.0/lib -lcudadevrt -Xlinker -rpath -Xlinker /usr/local/cuda/lib -Xlinker -framework -Xlinker CoreFoundation -framework CUDA'`

Note that you must build UPC++ with the same host compiler toolchain as is used
by `nvcc` when compiling any UPC++ CUDA programs. That is, both UPC++ and your
UPC++ application must be compiled using the same host compiler toolchain.
You can ensure this is the case by either (1) configuring UPC++ with the same
compiler as your system nvcc uses, or (2) using the `-ccbin` command line
argument to `nvcc` during application compilation to ensure it uses the same host
compiler as was passed to the UPC++ `configure` script.

#### Validation of CUDA memory kinds support
   
UPC++ CUDA operation can be validated using the following programs in the source tree:

* `test/copy.cpp` and `test/copy-cover.cpp`: correctness testers for the UPC++ `cuda_device`
* `bench/cuda_microbenchmark.cpp`: performance microbenchmark for `upcxx::copy` using GPU memory
* `example/cuda_vecadd`: demonstration of using UPC++ `cuda_device` to orchestrate
  communication for a program invoking CUDA computational kernels on the GPU.

One can validate use of GDR acceleration in a given UPC++ executable with a command
like the following:

```bash
$ upcxx-run -i a.out | grep CUDA
UPCXXCUDAGASNet: 1
UPCXXCUDAEnabled: 1
GASNetMKClassCUDAUVA: 1
```

Where the `UPCXXCUDAGASNet: 1` and `GASNetMKClassCUDAUVA: 1` lines together confirm the 
use of GDR acceleration. If either value is 0 or absent then GDR acceleration is not in use.

#### Known problems with GDR-accelerated memory kinds

There are several known defects in the current GASNet GDR Put implementation, arising from
a mismatch between the vendor's overly weak memory model for GDR transfers and 
traditional RMA Put completion semantics. This UPC++ version includes a workaround
for these defects that automatically converts Put-like `upcxx::copy` operations into
use of wire-level GDR Gets from the target rank. This workaround is automatically
enabled for runs using multi-rail InfiniBand or PSHM shared-memory bypass which are
known to be affected.  The workaround can also be explicitly controlled by
setting envvar `UPCXX_BUG4148_WORKAROUND` to 0 or 1.  For details on this GDR
defect, see the following GASNet bug report:

* [bug 4148: GDR and multi-rail or PSHM](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4148)

There is additionally an issue with incorrectly early source completion of GDR Puts:

* [bug 4150: GDR Put source completion](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4150)

if encountered, this problem can be avoided by explicitly setting `UPCXX_BUG4148_WORKAROUND=1`
to activate the same workaround (which is effective for both problems).

Finally, there is a known bug in the Mellanox IB Verbs firmware affecting GDR Gets that
causes crashes inside the IB Verbs network stack during small gets into device memory on some
platforms. This problem can be worked-around by setting `MLX5_SCATTER_TO_CQE=0`, 
but this setting has a global negative impact on RMA Get operations (even those
not involving device memory) so should only be used on affected platforms.
Details are here:

* [bug 4151: IBVerbs SEGV on small Gets to device memory](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4151)

In addition to the issues described above, the current implementation of 
GDR-accelerated memory kinds enforces a per-process limit of 32 active `cuda_device`
opens over the lifetime of the process. This static limit can be raised at configure time
via `configure --with-maxeps=N`, and is expected to become a more dynamic limit
in a future release.

#### Use of UPC++ memory kinds

See the "Memory Kinds" section in the _UPC++ Programmer's Guide_ for more details on 
using the CUDA support.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

## Advanced Configuration

The `configure` script tries to pick sensible defaults for the platform it is
running on, but its behavior can be controlled using the following command line
options:

* `--prefix=...`: The location at which UPC\+\+ is to be installed.  The
  default is `/usr/local/upcxx`.
* `--with-cc=...` and `--with-cxx=...`: The C and C\+\+ compilers to use.
* `--with-cross=...`: The cross-configure settings script to pull from the
  GASNet-EX source tree (`<gasnet>/other/contrib/cross-configure-${VALUE}`).
* `--without-cross`: Disable automatic cross-compilation, for instance to
  compile for the front-end of a Cray XC system.
* `--with-default-network=...`: Sets the default network to be used by the
  `upcxx` compiler wrapper.  Valid values are listed under "UPC\+\+ Backends" in
  [README.md](README.md).  The default is `aries` when cross-compiling for a
  Cray XC, and (currently) `smp` for all other systems.  Users with high-speed
  networks, such as InfiniBand (`ibv`), are encouraged to set this parameter
  to a value appropriate for their system.
* `--with-gasnet=...`: Provides the GASNet-EX source tree from which UPC\+\+
  will configure and build its own copies of GASNet-EX. This can be a path to a
  tarball, URL to a tarball, or path to a full source tree. If provided, this
  must correspond to a recent and compatible version of GASNet-EX (NOT GASNet-1).
  Defaults to an embedded copy of GASNet-EX, or the GASNet-EX download URL.
* `--with-gmake=...`: GNU Make command to use; must be 3.80 or newer.  The
  default behavior is to search `$PATH` for a `make` or `gmake` which meets this
  minimum version requirement.
* `--with-python=...`: Python interpreter to use; must be Python3 or Python2
  version 2.7.5 or newer.  The default behavior is to search `$PATH` for a
  suitable interpreter when `upcxx-run` is executed.  This option results in the
  use of a full path to the Python interpreter in `upcxx-run`.
* Options for control of (optional) CUDA support are documented in the section
  [Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support)
* Options not recognized by the UPC\+\+ `configure` script will be passed to
  the GASNet-EX `configure`.  For instance, `--with-mpirun-cmd=...` might be
  required to setup MPI-based launch of ibv-conduit applications.  Please read
  the GASNet-EX documentation for more information on this and many other
  options available to configure GASNet-EX.  Additionally, passing the option
  `--help=recursive` to the UPC\+\+ configure script will produce GASNet-EX's
  configure help message.

In addition to these explicit configure options, there are several environment
variables which can implicitly affect the configuration of GASNet-EX.  The most
common of these are listed at the end of the output of `configure --help`.

