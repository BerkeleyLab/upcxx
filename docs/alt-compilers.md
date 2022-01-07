## Guide to C++ standard libraries for non-GNU compilers on Linux

This document describes how to use a non-GNU C++ compiler (such as from
Clang/LLVM, Intel or PGI/Nvidia) with UPC++ on a Linux system where the default
C++ standard library is not recent enough to meet the requirements of UPC++.

Due to its use of C++11 features, UPC++ requires a fairly modern C++ compiler
and associated standard library.  The minimum requirements are documented in
[README.md](../README.md).  When using a non-GNU compiler, the default is most
often to use the C++ standard library of `/usr/bin/g++`.  Unfortunately, not
all current Linux distributions have a sufficiently recent `g++` to meet the
requirements of UPC++.

This document assumes the reader has installed a version of `g++` which _does_
meet the requirements, and desires to use its `libstdc++` with a non-GNU C++
compiler.  Be advised that not all C++ compilers are compatible with all
versions of `libstdc++`.  The most common issues relate to an inability of an
older compiler to parse a newer header.  It is beyond the scope of this
document to provide detailed guidance in this respect.  However, a good rule
of thumb is to avoid use of a `g++` newer than the non-GNU compiler.

The remainder of this document consists of sections each covering one compiler
family: Clang/LLVM, Intel or PGI/Nvidia.  For each compiler family there is a
description of how to query the C++ standard library to be used, and two
approaches to override it:

* A "temporary" override is described in which one must set `$CXX` when
  installing UPC++ *and* ensure a compatible invocation of the C++ compiler
  when compiling any sources to be linked into the same application.
  In general, this is less convenient than the permanent approach, but
  "safer" in that it will not have any effect on other uses of the compiler.

* A "permanent" override is described in which no specialized options are
  required in `$CXX` or when compiling sources to be linked into UPC++
  applications (though in some cases a compiler-specific environment
  variable setting is required at install and compile time).  In general,
  this is simpler to use than the temporary approach, but with the added
  risks that come from globally modifying the behavior of the compiler.

The "override" instructions can only ensure that a C++ compiler will use the
correct header search path and linker search path.  That leaves the matter
of the runtime linker (loader) search path to be dealt with.  The document
[local-gcc.md](local-gcc.md) describes means to address runtime linking, and
the majority of the approaches described there are relevant to non-GNU C++
compilers as well.  The sections below will clarify which.


## Clang/LLVM

### Query

One can determine the C++ standard library to be used by `clang++` with a
simple query.  For example, the following shows a case in which a GCC 7.2.0
installation is used:

```bash
$ clang++ --print-file-name=libstdc++.so
/usr/local/gcc/7.2.0/lib/gcc/x86_64-pc-linux-gnu/7.2.0/../../../../lib64/libstdc++.so
```

Alternatively, the following demonstrates looking for the information in the
verbose compiler output:

```bash
$ clang++ -v -E -x c++ /dev/null 2>&1 | grep '^Selected GCC'
Selected GCC installation: /usr/local/gcc/7.2.0/lib/gcc/x86_64-pc-linux-gnu/7.2.0
```

### Temporary Override

To use a non-default C++ standard library, `clang++` provides the command line
option `--gcc-toolchain`.  Its argument is an installation directory as would
be passed as the `--prefix` when configuring GCC.  For example, if
`/usr/local/gcc/6.4.0/bin/g++` is a suitable `g++` version, then one might
configure UPC++ with `CXX="clang++ --gcc-toolchain=/usr/local/gcc/6.4.0"`.

### Permanent Override

Detailed instructions on configuring and installing Clang/LLVM are beyond the
scope of this document.  Instead, the reader is advised to see LLVM.org's
official [Clang Getting Started](http://clang.llvm.org/get_started.html) page.
Text about C++ standard library headers will mention `-DGCC_INSTALL_PREFIX`,
which is the `cmake` analog to the `--gcc-toolchain` option.  Use of this
option when running `cmake` to configure Clang will establish the default used
in the absence of an explicit `--gcc-toolchain` option.

### Runtime Linker Search Path

All approaches to control of the runtime linker search path given in
[local-gcc.md](local-gcc.md) are also applicable to `clang++`.  This includes
static linking of compiler-dependent libraries, where `clang++` is command line
compatible with `g++`.



## Intel

### Query

One can determine the C++ standard library to be used by `icpc` with a simple
query.  For example, the following shows a case in which a GCC 8.3.0
installation is used:

```bash
$ icpc --print-file-name=libstdc++.so
/usr/local/gcc/8.3.0/lib/gcc/x86_64-pc-linux-gnu/8.3.0/../../../../lib64/libstdc++.so
```

Alternatively, one can look for GCC directories in the header search path
printed as part of the stderr generated with `-v`.  For example, the output
of following may be helpful:

```bash
$ icpc -v -E -x c++ /dev/null 2>&1 | grep '^ .*/include'
 /usr/local/intel/2019/compilers_and_libraries_2019.4.227/linux/pstl/include
 /usr/local/intel/2019/compilers_and_libraries_2019.4.227/linux/compiler/include/icc
 /usr/local/intel/2019/compilers_and_libraries_2019.4.227/linux/compiler/include
 /usr/local/gcc/8.3.0/include/c++/8.3.0
 /usr/local/gcc/8.3.0/include/c++/8.3.0/x86_64-pc-linux-gnu
 /usr/local/gcc/8.3.0/include/c++/8.3.0/backward
 /usr/local/include
 /usr/local/gcc/8.3.0/lib/gcc/x86_64-pc-linux-gnu/8.3.0/include
 /usr/local/gcc/8.3.0/lib/gcc/x86_64-pc-linux-gnu/8.3.0/include-fixed
 /usr/local/gcc/8.3.0/include/
 /usr/include
```

### Temporary Override

To use a non-default C++ standard library, `icpc` provides the `-gxx-name`
command line option.  The argument to this option is the full path to a
suitable `g++`.  It has been observed, however, that some installations
of the Intel compilers use mismatched paths for the C++ headers and libraries
unless `-gcc-name` is passed.  Therefore, we advise use of both options.
For instance, one might configure UPC++ using
`CXX="icpc -gxx-name=/usr/local/gcc/6.4.0/bin/g++ -gcc-name=/usr/local/gcc/6.4.0/bin/gcc"`.

Alternatively, one can consult the vendor-provided documentation for
information on "Using Response Files".  A response file, prefixed by `@`, can
serve as a convenient macro replacement for multiple command line options.
For example, one might set `CXX="icpc @$HOME/etc/icpc_upcxx.txt"` and place
the following in `$HOME/etc/icpc_upcxx.txt`:
```
-gxx-name=/usr/local/gcc/6.4.0/bin/g++
-gcc-name=/usr/local/gcc/6.4.0/bin/gcc
-Wl,-rpath=/usr/local/gcc/6.4.0/lib64
```

### Permanent Override

For detailed instructions on configuration and installation of the Intel
compiler suite, the reader is advised to consult the vendor-provided
documentation, where one should look for information on "Using
Configuration Files".  As described there, one can create an `icpc.cfg`
file containing options to be processes as if they appear on the command
line before any explicit options.  The content is precisely the same
as for a response file, shown immediately above.

If one has administrative privilege, a _global_ `icpc.cfg` can be placed in
the same directory as the `icpc` executable.  However, any user may create a
_private_ `icpc.cfg` file, and set the environment variable `ICPCCFG` to its
location.

### Runtime Linker Search Path

All approaches to control of the runtime linker search path given in
[local-gcc.md](local-gcc.md) are also applicable to `icpc`.  This includes
static linking of compiler-dependent libraries, where `icpc` is command line
compatible with `g++`.

Additionally, as demonstrated in the examples, both response files and
`icpc.cfg` provide a convenient means to pass linker options such as
`-Wl,-rpath=...` implicitly.



## PGI and Nvidia

### Query

One can determine the C++ library to be used by `pgc++` or `nvc++` by examining its
standard include path looking for GCC installation directories.  The following
examples extract relevant information from verbose compiler output, and
indicates that an installation of GCC 9.1.0 is being used.

For PGI versions through 20.4:  
```bash
$ pgc++ -v -E /dev/null 2>&1 | perl -ne 'if(m/stdinc ([^ ]*)/){print($1=~tr/:/\n/r);}'
/usr/local/pgi/linux86-64-llvm/19.7/include-gcc70
/usr/local/pgi/linux86-64-llvm/19.7/include
/usr/local/gcc/9.1.0/lib/gcc/x86_64-pc-linux-gnu/9.1.0/include
/usr/local/include
/usr/local/gcc/9.1.0/include
/usr/local/gcc/9.1.0/lib/gcc/x86_64-pc-linux-gnu/9.1.0/include-fixed
```

For PGI and NVHPC versions 20.9 and newer:  
```bash
$ nvc++ -v -E /dev/null 2>&1 >/dev/null | perl -0x20 -ne 'if(m,^-I/,) {print("$_\n");}'
-I/usr/local/nvidia/hpc_sdk/Linux_x86_64/21.5/compilers/include-gcc70
-I/usr/local/nvidia/hpc_sdk/Linux_x86_64/21.5/compilers/include
-I/usr/local/gcc/9.1.0/include/c++/9.1.0
-I/usr/local/gcc/9.1.0/include/c++/9.1.0/x86_64-pc-linux-gnu
-I/usr/local/gcc/9.1.0/include/c++/9.1.0/backward
-I/usr/local/gcc/9.1.0/lib/gcc/x86_64-pc-linux-gnu/9.1.0/include
-I/usr/local/include
[truncated]
```

If the command above yields an empty result, one can look for the
colon-separated list following `-stdinc` (for PGI <= 20.4) or the
`-I` options (PGI or NVHPC >= 20.9) in the output of the compiler
commands above (the portions before `| perl ...`).

### Temporary Override

NVHPC compilers 21.11 and newer support a `--gcc-toolchain` option to specify a
non-default C++ standard library.  Its argument is an installation directory as
would be passed as the `--prefix` when configuring GCC.  For example, if
`/usr/local/gcc/9.1.0/bin/g++` is a suitable `g++` version, then one might
configure UPC++ with `CXX="nvc++ --gcc-toolchain=/usr/local/gcc/9.1.0"`.

Compilers prior to 21.11 did not provide a simple means to override the GCC
installation from which the C++ standard library and headers are used.
Instead this information is contained in a configuration file, which one
must generate as described in the following "Permanent Override" section.
Given a suitable configuration file, one can provide it to an invocation of
`pgc++` using the `-rc` command line option.  So, one might install UPC++
using `CXX="pgc++ -rc=[full path to config file]"`.

### Permanent Override

For detailed instructions on configuration and installation of the PGI or
NVHPC compiler suite, the reader is advised to consult the vendor-provided
documentation, where one should look for information on the `makelocalrc`
utility.  The basic usage instructions for `makelocalrc` are available by
running it with no arguments.  The following assumes one has read and
understands those instructions.

When running `makelocalrc`, each of the following environment variables must
be unset (not just empty): `CPATH`, `C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH`.
If they _are_ set, then their values become embedded in the header search
path variables in the generated localrc file.  If necessary, one can run

```bash
$ env -u CPATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH makelocalrc [args]
```

By default, `makelocalrc` generates a configuration file suitable for the
`gcc`, `g++` and `g77` (or `gfortran`) in `/usr/bin`.  To override these, one
uses the correspondingly-named command line options.  It is strongly advised
to override all three compilers with versions from a single installation if
possible.  For example, the following would be suitable for the case in which
the desired compilers are found in ones `$PATH`:

```bash
$ makelocalrc [output options] -gcc $(which gcc) -g++ $(which g++) -g77 $(which gfortran)
```

Note that while the option `-g++` has been used in the example above, the
correct option may be either `-g++` or `-gpp`, depending on the compiler version.

A system administrator can install a _global_ configuration file.  However,
any user may create a _private_ configuration file, and either set the
environment variable `PGI_LOCALRC` (<= 20.4) or `NVLOCALRC` (>= 20.9) to its
location or use the `-rc` command line option to pass it at each compiler
invocation.

### Runtime Linker Search Path

Nearly all approaches to control of the runtime linker search path given in
[local-gcc.md](local-gcc.md) are also applicable to `pgc++`.  The exception is
static linking of compiler-dependent libraries, where `pgc++` is not command
line compatible with `g++` and has no equivalent.

We are not aware of any official documentation on the format of a localrc
file.  However, much useful information be found online in the
[NVIDIA Developer Forums](https://forums.developer.nvidia.com/), and especially
in the "Legacy PGI Compilers" section.  Based on information found
there, we can recommend (but cannot warranty) addition of a line such as the
one below to the end of any localrc file.  This instructs the compiler to add
the given directory to the executable's RPATH.

```text
set LOCALCOMPLIB=/usr/local/gcc/6.4.0/lib64;
```

Alternatively, other sources recommend the following:

```text
append USRRPATH=-rpath /usr/local/gcc/6.4.0/lib64;
```
