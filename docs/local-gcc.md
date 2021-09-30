## Guide to using a locally-built g++ with UPC++ on Linux

Due to its use of C++11 features, UPC++ requires a fairly modern C++ compiler
and associated standard library.  The minimum requirements are documented 
in [README.md](../README.md).
Unfortunately, not all current Linux distributions come with
compilers that meet this minimum.  By default, Clang++ on Linux uses the
libstdc++ of the default g++ and therefore is not a useful alternative.

If at all possible, we recommend use of a g++ provided
by your Linux distribution.  However, if that is not possible then you may need
to build g++ from sources.  Complete instructions for doing so are beyond the
scope of this document.  This document assumes you have successfully installed
a g++ meeting the documented minimum, and provides additional steps that should be taken to ensure
UPC++ applications find the proper libstdc++ at runtime.

The problem to be addressed is that the selection of the actual libstdc++ to be
used is made at runtime, not when the application is compiled and linked (though
the choice may depend on information provided at link time).  If one depends on
the default behaviors, then it is nearly certain that the system default
libstdc++ will be selected, rather than your newer one, and as a result your
UPC++ application may not run.  This is a problem with how shared libraries are
handled, and not unique to UPC++.  An example of the kind of error one might
see: 

```text
./a.out: /lib64/libstdc++.so.6: version `GLIBCXX_3.4.21' not found (required by ./a.out)
```

The remainder of this document lists some of the many ways the runtime selection
of libstdc++ version can be controlled.  Recommendations are ordered from the
most to least desirable in our opinion.  You typically need only implement one
of these recommendations.  The token 'GXXLIBDIR' will be used to denote the
shared library directory of your compiler installation.  In most cases it will
be the *directory portion* of the output of the following, where g++ should be
the compiler you intend to use:

```text
g++ --print-file-name libstdc++.so
```


### Your options:

#### 1. Configure with an -rpath option.

One can configure UPC++ using `--with-ldflags=-Wl,-rpath=GXXLIBDIR`.  This adds
a linker flag to encode the library directory in the ELF header of the
executable.  This should be the most reliable solution for most scenarios
that use the `upcxx` compiler wrapper or the flags from `upcxx-meta`.

#### 2. Link with an -rpath option.

If you are using neither the `upcxx` compiler wrapper nor the flags from
`upcxx-meta`, but you do have control over the link command line for your
application, then you should include `-Wl,-rpath=GXXLIBDIR` when linking.

#### 3. Add to the system's default library search path

If you have administrator privileges on every node you intend to run on, then
you can add to the default search path.  Read the man page for ldconfig for
information (which may vary across Linux distributions) for where to add
GXXLIBDIR and how to refresh /etc/ld.so.cache.  Since this modifies a property
of the node, it must be done on every compute node where you may run your UPC++
application.

#### 4. Run with LD_LIBRARY_PATH set

If you are comfortable modifying your shell's startup files (e.g.  ~/.bashrc or
~/.tcshrc), you can add GXXLIBDIR to the environment variable
LD_LIBRARY_PATH. Note this must be set by any users executing UPC++ programs,
and affects all programs they execute (which could potentially have negative
effects on other executables).

#### 5. Link with LD_RUN_PATH set

If (and only if) the linker does not receive any -rpath options, then the
environment variable LD_RUN_PATH will be used instead. This has similar
drawbacks to using LD_LIBRARY PATH.

**ADDITIONAL WARNING:** LD_RUN_PATH is only effective in the absence of any
  -rpath linker options, but when used as a linker all common MPI C++ compilers
  add -rpath options to the link command (and some UPC++ backends use GASNet
  conduits that will default to linking with MPI for job spawning).

#### 6. Link the compiler-dependent libraries statically

If all else fails, one can opt-out of dynamic linking of libstdc++ by passing
`-static-libgcc -static-libstdc++` to g++, upcxx, or mpicxx when linking.  This
can greatly increase the size of the executable, but should be 100% reliable.
