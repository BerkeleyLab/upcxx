# Programmer's Guide Examples

This directory contains full working example codes corresponding to examples that
appear (sometimes as incomplete snippets) in the UPC++ Programmer's Guide.

## Building examples

To build the example codes, make sure that either the `upcxx` and `upcxx-run`
commands are in your `$PATH`, or alternatively set the `$UPCXX_INSTALL`
environment variable. e.g. :

```bash
export UPCXX_INSTALL=<upcxx-install-dir>
```

Then one can build all the examples using:

```bash
make all
```
or particular examples by name:

```bash
make hello-world
```

One can optionally set environment variables `$UPCXX_THREADMODE`, `$UPCXX_CODEMODE`,
and `$UPCXX_NETWORK` to control the UPC++ backend used for building the examples
or optionally set `$CXXFLAGS` to directly inject options to the `upcxx` wrapper, e.g.:

```bash
make all CXXFLAGS="-g -network=smp"
```

See `upcxx --help` and the top-level README.md for details on these options.

## Running examples

Built examples can be run directly as usual, e.g. :

```bash
upcxx-run -n 4 ./hello-world
```

There is also a convenience Makefile target to run all the examples currently built:

```bash
make run PROCS=8
```


## Instructions to Maintainers

The code in this directory is kept manually synchronized with the code/
subdirectory in the upcxx-prog-guide repo.  Changes made here must be manually
mirrored to that repo and then put into the working copy of the guide
document by running `./putcode.py all guide.md` and committing the result.

