# UPC\+\+ Installation #

This file documents software installation of [UPC++](https://upcxx.lbl.gov).

For information on using UPC++, see: [README.md](README.md)    

## Public Installs

The team which develops and maintains UPC++ also provides public
installs of current UPC++ releases at several HPC centers.  Before you invest
time in installing UPC++ for yourself, please consider checking the 
[online documentation](https://upcxx.lbl.gov/site) which describes
these installs, including site-specific usage instructions regarding compiling
and running on each such system.

## System Requirements

### Supported Platforms

UPC++ makes aggressive use of template meta-programming techniques, and requires
a modern C++ compiler and corresponding standard library implementation.

The current release is known to work on the following configurations:

* Apple macOS/x86\_64 (smp and udp conduits):
    - The most recent Xcode release for each macOS release is generally well-tested
        + It is suspected that any Xcode (ie Apple clang) release 8.0 or newer will work
    - Free Software Foundation g++ (e.g., as installed by Homebrew or Fink)
      version 6.4.0 or newer should also work

    At the time of the 2023.9.0 release of UPC++, we have tested only lightly on
    macOS 13 "Ventura" and macOS 14 "Sonoma".
    We welcome reports of success or failure on macOS 13 and/or 14.

* Linux/x86\_64 with one of the following compilers:
    - g++ 6.4.0 or newer    
    - clang++ 4.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - Intel C++ 17.0.2 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - Intel oneAPI compilers 2021.1.2 or newer (with libstdc++ from g++ 6.4.0 or newer)
    - PGI C++ 19.3 through 20.4 (with libstdc++ from g++ 6.4.0 or newer)
    - NVIDIA HPC SDK (aka nvhpc) 20.9 and newer (with libstdc++ from g++ 6.4.0 or newer)
    - AMD AOCC compilers 2.3.0 or newer (with libstdc++ from g++ 6.4.0 or newer)

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

* Linux/ppc64le (aka IBM POWER little-endian) with one of the following compilers:
    - g++ 6.4.0 or newer
    - clang++ 5.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)    
    - PGI C++ 19.3 through 20.4 (with libstdc++ from g++ 6.4.0 or newer)
    - NVIDIA HPC SDK (aka nvhpc) 20.9 and newer (with libstdc++ from g++ 6.4.0 or newer)

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

* Linux/aarch64 (aka "arm64" or "armv8") with one of the following compilers:
    - g++ 6.4.0 or newer
    - clang++ 4.0.0 or newer (with libstdc++ from g++ 6.4.0 or newer)   

    If `/usr/bin/g++` is older than 6.4.0 (even if using another compiler),
    see [Linux Compiler Notes](#markdown-header-linux-compiler-notes), below.

    Note the GPUDirect drivers necessary for GDR-accelerated memory kinds on
    InfiniBand are not supported on the Linux/aarch64 platform.

* HPE Cray EX with x86\_64 CPUs and one of the following PrgEnv environment
  modules, plus its dependencies (smp and ofi conduits):
    - PrgEnv-gnu with gcc/10.3.0 (or later) loaded.
    - PrgEnv-cray with cce/12.0.0 (or later) loaded.
    - PrgEnv-amd with amd/4.2.0 (or later) loaded.
    - PrgEnv-aocc with aocc/3.1.0 (or later) loaded.
    - PrgEnv-nvidia with nvidia/21.9 (or later) loaded.
    - PrgEnv-nvhpc with nvhpc/21.9 (or later) loaded.
    - PrgEnv-intel with intel/2023.1.0 (or later) loaded.

* **DEPRECATED**  
  Cray XC/x86\_64 with one of the following PrgEnv environment modules and
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
      Testing on this platform with both Xcode and Free Software
      Foundation g++ show functionally complete and correct operation.  
      Nothing platform-specific has been implemented for the mix of
      "performance" and "efficiency" cores, meaning performance could be
      highly variable.  
      At this time we consider it premature to list this platform as
      "supported", and the `configure` script will issue a warning.
    - Vendor-specific `clang++` or `g++` variants.  
      At least Arm Ltd. provides compilers based on their own modifications to
      Clang/LLVM.  Similarly, at least Arm Ltd. and IBM provide forks of `g++`.  
      To the best of our limited current knowledge, these all behave as their
      respective "upstream" compilers, with no additional compiler-specific
      issues.  
      At this time we do not consider these compilers to be officially
      supported due to insufficient periodic automated testing.  
      The presence or absence of a warning from `configure` varies.

### Miscellaneous software requirements:

* Python3 or Python2 version 2.7.5 or newer

* Perl version 5.005 or newer

* GNU Bash 3.2.57 or newer (must be installed, user's shell doesn't matter)

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
5. `make test_install` (optional, but highly recommended)  
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

* [Configuration: HPE Cray EX](#markdown-header-configuration-hpe-cray-ex)
* [Configuration: Linux](#markdown-header-configuration-linux)
* [Configuration: Apple macOS](#markdown-header-configuration-apple-macos)
* [Configuration: Cray XC](#markdown-header-configuration-cray-xc)
* [Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support)
* [Configuration: AMD ROCm/HIP GPU support](#markdown-header-configuration-amd-rocmhip-gpu-support)
* [Configuration: HIP-over-CUDA GPU support](#markdown-header-configuration-hip-over-cuda-gpu-support)
* [Configuration: Intel oneAPI GPU support](#markdown-header-configuration-intel-oneapi-gpu-support)

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

Bash 3.2.57 or newer is required by UPC\+\+ scripts, including `configure`.  By
default, `configure` will try `/bin/sh` and then the first instance of `bash`
found in `$PATH`.  If neither of these is bash 3.2.57 (or newer), or if the one
found is not appropriate to use (for instance not accessible on compute
nodes), one can override the automated selection by invoking `configure` _via_
the desired instance of `bash`:
```bash
/usr/gnu/bin/bash <upcxx-source-path>/configure ...
```
By default, the configure script will attempt to enforce use of C++ and C
compilers which report the same family and version.  If necessary, this
can be disabled using `--enable-allow-compiler-mismatch`.  However,
installation of UPC\+\+ configured in this manner is not supported.

#### 2. Compiling UPC\+\+

```bash
make all
```

This will compile the UPC\+\+ runtime libraries, including the GASNet-EX
communications runtime.  One may run, for instance, `make -j8 all` to build
with eight concurrent processes.  This may significantly reduce the time
required. However parallel make can also obscure error messages, so if you
encounter a failure you should retry without a `-j` option.

Some combinations of network and `configure` options require that `CXX` be
capable of linking MPI applications.  If that requirement exists but is unmet,
then this step will fail with output giving instructions to read the section
[Configuration: Linux](#markdown-header-configuration-linux) in this document,
where this issue is described in more detail.

The output generated at the successful conclusion of this step gives the
default network and a list of available networks.  This is an appropriate time
to verify that the default network is the one you expect to use.  If it is
not, but it is listed as available, you can specify your preferred network
to the later `make install` step _without_ starting over.  However, if your
preferred network is not listed as available, then you will need to return
to the previous (`configure`) step, where additional arguments or environment
modules may be required to enable detection of the appropriate headers and/or
libraries.

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
to this command, with network names (such as `smp`, `udp`, `ibv`, `ofi` or `aries`)
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
`check` steps report the default to be used in the absence of an explicit
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

For systems using "environment modules" an example module file is provided
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

### Configuration: HPE Cray EX

This release of UPC++ includes initial support for the HPE Cray EX platform,
including both the "Slingshot-10" and "Slingshot-11" network interface cards
(NICs) and GPUs from both Nvidia and AMD.  When built in a supported
configuration, this release passes all of the UPC++ test suite.  However, the
performance has not yet been tuned on this platform.

Unlike the Cray XC, the HPE Cray EX is *not* treated as a cross-compilation
target when building UPC++.  However, we strongly advise use of the vendor's
wrapper compilers, `cc` and `CC`.  Additionally, the two NICs require distinct
non-default settings.  The following shows our recommended configure command
with some "<placeholders>" which are explained below.  Note that these assume
use of the default version of GASNet-EX.  If using an earlier release of
GASNet-EX, please consult documentation in a UPC++ release of similar age.

```bash
module load libfabric cray-pmi <GPU_MODULES>
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> \
    --with-cc=cc --with-cxx=CC --with-mpi-cc=cc \
    --with-ofi-provider=<PROVIDER> \
    --with-pmi-runcmd='<RUNCMD>' \
    <GPU_OPTIONS>    
```

The `libfabric` and `cray-pmi` environment modules may or may not be loaded by
default at any given site.  Please ensure they are loaded (as shown above) or
the configure or build steps may fail.

As denoted by the <GPU_MODULES> placeholder, one or more environment modules
may be needed for GPU support.  Example module names (to help locate the
appropriate information in site-specific documentation) include `cudatoolkit`,
`rocm` and `intel_compute_runtime`, though variations on these names exist.
In some cases one may also need a device-specific module, often with a name
starting with `craype-accel-`, to avoid link errors or warnings on every
compile.  Be advised that some sites may bundle the programming model and
device modules into a single module.

There are two NICs options in an HPE Cray EX system, known as "Slingshot-10" and
"Slingshot-11".  They require different libfabric "providers", as indicated by
the `<PROVIDER>` placeholder above:  

  + `--with-ofi-provider=verbs` for Slingshot-10.  
    This is a Mellanox ConnectX-5 (or -6) 100Gbps NIC.  
  + `--with-ofi-provider=cxi` for Slingshot-11.  
    This is an HPE 200Gbps NIC  

If you are uncertain of which NIC is used on a given system, please consult the
site-specific documentation or ask the support staff for assistance.
Another alternative is to pass `--with-ofi-provider=generic`, which requests
provider adaptation be performed during runtime startup, at some cost in
additional communication overhead. This option may be useful for systems
with a mix of Slingshot-10 and Slingshot-11 nodes, although all processes in a
job still need to be using matching hardware and software (including provider)
at runtime.

On _some_ systems with multiple Slingshot NICs, one will need to add
`--with-host-detect=hostname`.  This option is recommended only when actually
required.  If your system _does_ require this setting, then you will see a
message at application run time directing you to use this option, or an
environment-based alternative.

You will also need to select the proper argument to `--with-pmi-runcmd=...`
(the `<RUNCMD>` placeholder, above).

  + If using the Slurm Workload Manager: `--with-pmi-runcmd='srun -n %N -- %C'`
  + For most other cases: `--with-pmi-runcmd='aprun --cc none -n %N %C'`

At the time of this writing we've only tested UPC++ on HPE Cray EX systems with
AMD CPUs.

As mentioned earlier and indicated by the `<GPU_OPTIONS>` placeholder, this
UPC++ release supports GPUs using Nvidia CUDA, AMD ROCm/HIP, and Intel oneAPI
in HPE Cray EX systems.  Please _also_ see the respective sections of this
document for UPC++ configure options needed to enable this support:

* [Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support)
* [Configuration: AMD ROCm/HIP GPU support](#markdown-header-configuration-amd-rocmhip-gpu-support)
* [Configuration: Intel oneAPI GPU support](#markdown-header-configuration-intel-oneapi-gpu-support)

With the Slingshot-11 network, some users have seen application hangs due to
what appears to be "lost" RPCs.  At the time this is written, there are two
possible workarounds for this issue.  Descriptions of both workarounds, along
with the most up-to-date information on this issue in general, can be found in
the corresponding GASNet-EX report:
[bug 4461](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4461).

With the Slingshot-10 network, there are conditions (not yet characterized)
under which RPCs may be corrupted in such a way that their reception results in
a fatal error.  This can manifest with a fatal error containing the text "no
associated AM handler function" or (in a debug build) an "Assertion failure"
message with the expression `isreq == header->isreq`.  At the time this is
written, there is a known workaround for this issue.  A description of the
workaround, along with the most up-to-date information on this issue in
general, can be found in the corresponding GASNet-EX report:
[bug 4517](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4517).

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

### Configuration: Cray XC

** Support for the Cray XC platform is deprecated and will be removed in a future release. **

By default, on a Cray XC the logic in `configure` will automatically detect either
the SLURM or Cray ALPS job scheduler and will cross-configure for the
appropriate package.  If this auto-detection fails, you may need to explicitly
pass the appropriate value for your system:

* `--with-cross=cray-aries-slurm`: Cray XC systems using the SLURM job scheduler (srun)
* `--with-cross=cray-aries-alps`: Cray XC systems using the Cray ALPS job scheduler (aprun)

When Intel compilers are being used (a common default for these systems),
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

### Configuration: CUDA GPU support

#### System Requirements:

UPC++ includes support for RMA communication operations on memory buffers
resident in a CUDA-compatible NVIDIA GPU.  General requirements:

* Modern NVIDIA-branded [CUDA-compatible GPU hardware](https://developer.nvidia.com/cuda-gpus)
* NVIDIA CUDA toolkit v9.0 or later. Available for [download here](https://developer.nvidia.com/cuda-downloads).

#### Additional System Requirements for GDR-accelerated memory kinds:

This version of UPC++ supports GPUDirect RDMA (GDR) acceleration of memory
kinds data transfers on selected platforms using modern NVIDIA-branded GPUs
with NVIDIA- or Mellanox-branded InfiniBand or HPE Slingshot network hardware.  
This support requires one of the following native network conduit
configurations, and the current/default version of GASNet-EX:

* ibv-conduit with recent NVIDIA/Mellanox-branded InfiniBand network hardware
* ofi-conduit on HPE Cray EX with HPE Slingshot-11 (cxi provider)
* ofi-conduit on HPE Cray EX with HPE Slingshot-10 (verbs provider)

Additional requirements:

* Linux OS with x86\_64 or ppc64le CPU (not ARM)
* GPUDirect RDMA drivers installed

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

To activate the UPC++ support for CUDA, pass `--enable-cuda` to the `configure`
script:

```bash
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --enable-cuda
```

This will detect whether the requirements for GDR acceleration are met and
automatically activate that feature. 
For troubleshooting installation of GASNet's GDR support, please see
[docs/memory_kinds.md](https://bitbucket.org/berkeleylab/gasnet/src/master/docs/memory_kinds.md)
in the GASNet distribution.

`configure --enable-cuda` expects to find the NVIDIA `nvcc` compiler wrapper in your `$PATH` and
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

One can validate CUDA support in a given UPC++ install using a command like the following:

```bash
$ upcxx-info | grep CUDA

UPCXX_CUDA:                         1
UPCXX_CUDA_NVCC:                    /path/to/cuda/bin/nvcc
UPCXX_CUDA_CPPFLAGS:                ...CUDA include options...
UPCXX_CUDA_LIBFLAGS:                ...CUDA library options...
  GPUs with NVIDIA CUDA API (cuda-uva)               ON     (enabled)
```

Where the `UPCXX_CUDA: 1` indicates the UPC++ install is CUDA-aware, and in the last line
`ON` indicates that GASNet-EX *may* include GDR acceleration support (actual availability
also depends on network backend selection at application compile time).
   
UPC++ CUDA operation can be validated using the following programs in the source tree:

* `test/copy.cpp` and `test/copy-cover.cpp`: correctness testers for the UPC++ `cuda_device`
* `bench/gpu_microbenchmark.cpp`: performance microbenchmark for `upcxx::copy` using GPU memory
* `make cuda_vecadd` in `example/gpu_vecadd`: demonstration of using UPC++ `cuda_device` to
  orchestrate communication for a program invoking CUDA computational kernels on the GPU.

One can validate use of GDR acceleration in a given UPC++ executable with a command
like the following:

```bash
$ upcxx-run -i a.out | grep CUDA
UPCXXKindCUDA: 202103L
UPCXXCUDAGASNet: 1
UPCXXCUDAEnabled: 1
GASNetMKClassCUDAUVA: 1
```

Where the `UPCXXCUDAGASNet: 1` and `GASNetMKClassCUDAUVA: 1` lines together confirm the 
use of GDR acceleration. If either value is 0 or absent then GDR acceleration is not in use.

#### Known problems with GDR-accelerated memory kinds

There is a known bug in the vendor-provided IB Verbs firmware affecting GDR Gets that
causes crashes inside the IB Verbs network stack during `copy()` operations
targeting small objects in a `cuda_device` segment with affinity to the calling
process on some platforms. This problem can be worked-around by setting
`MLX5_SCATTER_TO_CQE=0`, but this setting has a global negative impact on RMA
Get operations (even those not involving device memory) so should only be used
on affected platforms.  Details are here:

* [bug 4151: IBVerbs SEGV on small Gets to device memory](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4151)

There is also a known bug in the libfabric "verbs provider" (recommended for
use on Slingshot-10 networks) that causes crashes inside libfabric during
`copy()` operations where the source objects reside in a `cuda_device` segment
with affinity to the calling process.  This problem can be worked-around by
setting `FI_VERBS_INLINE_SIZE=0`.  Because this setting disables a valuable
performance optimization, it may increase the latency of all small RMA Puts,
including those from host memory, as well as some RPCs.  Therefore, it is
strongly recommended that you set this variable *only* if your system exhibits
this issue.  Details are here:

* [bug 4494: ofi/verbs SEGV for small Puts from CUDA memory](https://gasnet-bugs.lbl.gov/bugzilla/show_bug.cgi?id=4494)

In addition to the two issues described above, the current implementation of
GDR-accelerated memory kinds enforces a per-process limit of 32 active `cuda_device`
opens over the lifetime of the process. This static limit can be raised at configure time
via `configure --with-maxeps=N`, and is expected to become a more dynamic limit
in a future release.

#### Use of UPC++ memory kinds

See the "Memory Kinds" section in the _UPC++ Programmer's Guide_ for more details on 
using the CUDA support.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: AMD ROCm/HIP GPU support

#### System Requirements:

UPC++ includes support for RMA communication operations on memory buffers
resident in a ROCm/HIP-compatible AMD GPU.  General requirements:

* Modern AMD-branded HIP-compatible GPU hardware
* AMD ROCm drivers version 4.5.0 or later (earlier versions of ROCm MIGHT also
  work, but are not recommended)

#### Additional System Requirements for ROCmRDMA-accelerated memory kinds:

This version of UPC++ supports ROCmRDMA acceleration of memory
kinds data transfers on selected platforms using modern AMD-branded GPUs.
This support requires one of the following native network conduit
configurations, and the current/default version of GASNet-EX:

* ibv-conduit with recent NVIDIA/Mellanox-branded InfiniBand network hardware
* ofi-conduit on HPE Cray EX with HPE Slingshot-11 (cxi provider)
* ofi-conduit on HPE Cray EX with HPE Slingshot-10 (verbs provider)

Additional Requirements:

* Linux OS with x86\_64 or ppc64le CPU (not ARM)
* AMD GPU kernel driver installed

When using ROCmRDMA-accelerated memory kinds, calls to `upcxx::copy` will offload
the data transfer to the network adapter, streaming data directly between the
source and destination memory locations (in host or device memory on any node), 
without staging through additional memory buffers.

For all other platforms, the ROCm/HIP support in this UPC++ release utilizes a
reference implementation which has not been tuned for performance. In
particular, `upcxx::copy` will stage data transfers involving device
memory through intermediate buffers in host memory, and is expected to
underperform relative to solutions using RDMA, ROCmRDMA and similar
zero-copy technologies. Future versions of UPC++ will introduce
native memory kinds acceleration for additional GPU and network variants.

#### `configure` Command for Enabling AMD ROCm/HIP GPU Support

To activate the UPC++ support for AMD ROCm/HIP, pass `--enable-hip` to the `configure`
script:

```bash
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --enable-hip
```

This will detect whether the requirements for ROCmRDMA acceleration are met and
automatically activate that feature. 
For troubleshooting installation of GASNet's ROCmRDMA support, please see
[docs/memory_kinds.md](https://bitbucket.org/berkeleylab/gasnet/src/master/docs/memory_kinds.md)
in the GASNet distribution.

`configure --enable-hip` expects to find the AMD ROCm `hipcc` compiler wrapper
in your `$PATH` and will attempt to infer the correct ROCm/HIP install location for
your system. If this automatic detection fails, then you may need to manually
override the following options to `configure`:

* `--with-hip-home=...`: the install prefix for the ROCm/HIP developer tools 
   Eg `--with-hip-home=/opt/rocm-4.5.0/hip`

* `--with-hip-cppflags=...`: the pre-processor flags needed to find HIP runtime headers
   Eg `--with-hip-cppflags='-I/opt/rocm-4.5.0/hip/include'`

* `--with-hip-libflags=...`: the linker flags needed to link HIP runtime libraries
   Eg `--with-hip-libflags='-L/opt/rocm-4.5.0/hip/lib -lamdhip64'`

Note that you must build UPC++ with the same host compiler toolchain as is used
by `hipcc` when compiling any UPC++ ROCm programs. That is, both UPC++ and your
UPC++ application must be compiled using the same host compiler toolchain.
You can ensure this is the case by either (1) configuring UPC++ with the same
compiler as your system hipcc uses, or (2) using the `--gcc-toolchain=` command line
argument to `hipcc` during application compilation to ensure it uses the same host
compiler as was passed to the UPC++ `configure` script.

#### Validation of ROCm/HIP memory kinds support
   
One can validate HIP/ROCm support in a given UPC++ install using a command like the following:

```bash
$ upcxx-info | grep HIP

UPCXX_HIP:                          1
UPCXX_HIP_CPPFLAGS:                 ...HIP include options...
UPCXX_HIP_LIBFLAGS:                 ...HIP library options...
  GPUs with AMD HIP API (hip)                        ON     (enabled)
```

Where the `UPCXX_HIP: 1` indicates the UPC++ install is HIP-aware, and in the last line
`ON` indicates that GASNet-EX *may* include ROCmRDMA acceleration support (actual availability
also depends on network backend selection at application compile time).

UPC++ ROCm/HIP operation can be validated using the following programs in the source tree:

* `test/copy.cpp` and `test/copy-cover.cpp`: correctness testers for the UPC++ `hip_device`
* `bench/gpu_microbenchmark.cpp`: performance microbenchmark for `upcxx::copy` using GPU memory
* `make hip_vecadd` in `example/gpu_vecadd`: demonstration of using UPC++ `hip_device` to
   orchestrate communication for a program invoking HIP computational kernels on the GPU.

One can validate use of ROCmRDMA acceleration in a given UPC++ executable with a command
like the following:

```bash
$ upcxx-run -i a.out | grep HIP
UPCXXKindHIP: 202203L
UPCXXHIPEnabled: 1
UPCXXHIPGASNet: 1
GASNetMKClassHIP: 1
```

Where the `UPCXXHIPGASNet: 1` and `GASNetMKClassHIP: 1` lines together confirm the 
use of ROCmRDMA acceleration. If either value is 0 or absent then ROCmRDMA acceleration is not in use.

#### Known problems with ROCmRDMA-accelerated memory kinds

The current implementation of 
ROCmRDMA-accelerated memory kinds enforces a per-process limit of 32 active `hip_device`
opens over the lifetime of the process. This static limit can be raised at configure time
via `configure --with-maxeps=N`, and is expected to become a more dynamic limit
in a future release.

#### Use of UPC++ memory kinds

See the "Memory Kinds" section in the _UPC++ Programmer's Guide_ for more details on 
using the UPC++ GPU support.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: HIP-over-CUDA GPU support

#### System Requirements:

AMD ROCm provides an implementation of HIP-over-CUDA allowing HIP code to target 
NVIDIA-branded GPUs. UPC++ can interoperate with this translation layer, 
allowing the use of `upcxx::hip_device` on NVIDIA GPU hardware. This enables RMA 
communication on memory buffers resident in these GPUs just as if
they were AMD GPUs (or if the code being compiled was written in CUDA). This is
an experimental capability, but has been shown to work with the following
configurations:

* AMD ROCm version 5.1.0 and CUDA toolkit version 11.4.0
* AMD ROCm version 5.3.2 and CUDA toolkit version 11.7.0

as well as modern NVIDIA-branded [CUDA-compatible GPU hardware](https://developer.nvidia.com/cuda-gpus).

Additional requirements for GPUDirect RDMA can be found in the section 
[Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support).

#### `configure` Command for Enabling HIP-over-CUDA GPU Support

To activate the UPC++ support for HIP-over-CUDA, pass `--enable-hip` and 
`--with-hip-platform=nvidia` to the `configure` script:

```bash
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --enable-hip --with-hip-platform=nvidia
```

For issues with automatic detection of compiler location or build flags, 
consult the relevant sections of  
[Configuration: CUDA GPU support](#markdown-header-configuration-cuda-gpu-support) and 
[Configuration: AMD ROCm/HIP GPU support](#markdown-header-configuration-amd-rocmhip-gpu-support).

As mentioned in prior sections, both UPC++ and your UPC++ application must be 
compiled using the same host compiler toolchain.

After running `configure`, return to
[Step 2: Compiling UPC\+\+](#markdown-header-2-compiling-upc4343), above.

### Configuration: Intel oneAPI GPU support

UPC++ includes initial EXPERIMENTAL support for RMA communication operations on
memory buffers resident in a oneAPI-compatible Intel GPU, using the 
oneAPI Level-Zero (ZE) interface.

**Intel GPU memory kind support in this release is believed to be functionally correct,
  but has not been tuned for performance. `upcxx::copy()` operations on `ze_device` 
  memory are currently staged through host memory by default and do not yet leverage network-direct RDMA.**

#### System Requirements:

* Modern Intel-branded oneAPI-compatible GPU hardware with appropriate kernel drivers
* Intel Level-Zero development headers (`level-zero-dev` package)

The full Intel oneAPI Toolkits are *NOT* required to build UPC++ and use
`ze_device`, but are likely required by applications that want to use the
GPU for computation.

#### `configure` Command for Enabling Intel oneAPI GPU Support

To activate the UPC++ support for Intel oneAPI Level-Zero,
pass `--enable-ze` to the `configure` script:

```bash
cd <upcxx-source-path>
./configure --prefix=<upcxx-install-path> --enable-ze
```

`configure --enable-ze` attempts to automatically detect the install prefix of
the Level-Zero developer tools and related compilation options for your system.
If this automatic detection fails, then you may need to manually
override one or more of the following options to `configure`:

* `--with-ze-home=...`: the install prefix for the Level Zero developer tools 
   Eg `--with-ze-home=/usr/local/pkg/intel/level-zero/1.9.4 `

* `--with-ze-cppflags=...`: the pre-processor flags needed to find Level Zero headers
   Eg `--with-ze-cppflags='-I/usr/local/pkg/intel/level-zero/1.9.4/include'`

* `--with-ze-libflags=...`: the linker flags needed to link Level Zero runtime libraries
   Eg `--with-ze-libflags='-L/usr/local/pkg/intel/level-zero/1.9.4/lib64 -lze_loader'`

Note that you must build UPC++ with the same host compiler toolchain used for
compiling objects linked to any UPC++ oneAPI programs. That is, both UPC++ and your
UPC++ application must be compiled using the same host compiler toolchain.

#### Validation of oneAPI memory kinds support
   
One can validate `ze_device` support in a given UPC++ install using a command like the following:

```bash
$ upcxx-info | grep ZE

UPCXX_ZE:                          1
UPCXX_ZE_CPPFLAGS:                 ...ZE include options...
UPCXX_ZE_LIBFLAGS:                 ...ZE library options...
```

Where the `UPCXX_ZE: 1` indicates the UPC++ install is ZE-aware.

UPC++ `ze_device` operation can be validated using the following programs in the source tree:

* `test/copy.cpp` and `test/copy-cover.cpp`: correctness testers for the UPC++ `ze_device`
* `bench/gpu_microbenchmark.cpp`: performance microbenchmark for `upcxx::copy` using GPU memory

One can validate a given UPC++ executable includes `ze_device` support with a command
like the following:

```bash
$ upcxx-run -i a.out | grep ZE
UPCXXKindZE: 202303L
UPCXXZEEnabled: 1
UPCXXZEGASNet: 0
```

Where the `UPCXXZEEnabled: 1` line indicates the presence of `ze_device`
support in UPC++, and `UPCXXZEGASNet: 0` indicates the default lack of hardware
acceleration for `ze_device` transfers in the current GASNet release.

#### EXPERIMENTAL accelerated memory kinds for Intel GPUs:

This version of UPC++ includes an **EXPERIMENTAL** prototype-quality implementation
of accelerated memory kinds data transfers on selected platforms using modern
Intel-branded GPUs with HPE Slingshot-11 network hardware.  **This support is
preliminary and has known correctness and functionality limitations**, and 
is thus disabled by default; configure option `--enable-kind-ze` must be
provided to activate this support.

This support requires the following native network conduit
configurations, and the current/default version of GASNet-EX:

* ofi-conduit on HPE Cray EX with HPE Slingshot-11 (cxi provider)

Additional requirements:

* Recent Linux OS with x86\_64 CPU
* Appropriate Intel GPU drivers installed

When using accelerated memory kinds, calls to `upcxx::copy` will offload
the data transfer to the network adapter, streaming data directly between the
source and destination memory locations (in host or device memory on any node), 
without staging through additional memory buffers. Presence of this support
can be validated using the same commands in the previous section, where the
output includes a `UPCXXZEGASNet: 1` line to indicate presence of the support.

In the absence of this experimental support, the Level Zero memory kinds
support in this UPC++ release utilizes a reference implementation which has not
been tuned for performance. In particular, `upcxx::copy` will stage data
transfers involving device memory through intermediate buffers in host memory,
and is expected to underperform relative to solutions using zero-copy technologies.
Future versions of UPC++ and GASNet-EX will expand and enhance the support
for native memory kinds acceleration on Intel GPUs.

#### Use of UPC++ memory kinds

See the "Memory Kinds" section in the _UPC++ Programmer's Guide_ for more details on 
using the UPC++ GPU support.

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
* Options for control of (optional) AMD ROCm/HIP GPU support are documented in the section
  [Configuration: AMD ROCm/HIP GPU support](#markdown-header-configuration-amd-rocmhip-gpu-support)
* Options for control of (optional) Intel oneAPI GPU support are documented in the section
  [Configuration: Intel oneAPI GPU support](#markdown-header-configuration-intel-oneapi-gpu-support)
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
Since these influence the GASNet-EX `configure` script, they are used in the
`make` or `make all` stages of the UPC++ build, not its `configure` stage.

