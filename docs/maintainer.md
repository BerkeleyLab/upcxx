# Maintaining the UPC\+\+ Library Internals

These instructions are for UPC\+\+ runtime maintainers only.

THIS INTERNAL DOCUMENTATION IS NOT CAREFULLY MAINTAINED AND MAY BE OUT OF DATE.

Software requirements are detailed in [INSTALL.md](../INSTALL.md).  
Because we do not employ autoconf, automake or CMake, the requirements for
maintainers of UPC\+\+ are no different than for the end-users.  However, the
manner in which the tools might be used does differ.

## Workflow

The first thing to do when beginning a session of upcxx hacking is to
configure your build tree.  This can be done as many times as needed, for
instance using distinct build trees for distinct compilers (and sharing the
source tree).  Just run `<upcxx-src-path>/configure ...` in your build
directory (which *is* permitted to be the same as `<upcxx-src-path>`).
This will "capture" the options `--with-cc=...`, `--with-cxx=...`,
`--with-cross=...` and `--with-gasnet=...`.  Unless you re-run `configure`
those four parameters cannot be changed for a given build directory (but
you can have as many build directories as you want/need).

```bash
mkdir <upcxx-build-path>
cd <upcxx-build-path>
<upcxx-source-path>/configure --with-cc=... --with-cxx=... [--with-cross=...]
```

The legacy environment variables `CC`, `CXX`, `CROSS` and `GASNET` are still
available, but their use is deprecated.  Additionally, the legacy variable
`GASNET_CONFIGURE_ARGS` is still honored, but all unrecognized options to
the UPC\+\+ configure script are appended to it (giving them precedence).

[INSTALL.md](../INSTALL.md) has more information on the `configure` options
supported for end-users while "Internal-Only Configuration Options", below,
documents some unsupported ones.

The `configure` script populates `<upcxx-build-dir>/bin` with special `upcxx`,
`upcxx-meta`, `upcxx-run` and `upcxx-info` scripts specific to use in the build-dir.  These
scripts dynamically build any necessary UPC\+\+ and GASNet-EX libraries the
first time they are required, and updates them if any UPC\+\+ or GASNet-EX
source files change.  For `upcxx` the necessary libraries are determined from
the environment as overridden by any flags passed to it.  For `upcxx-run`, the
conduit is extracted from the executable passed to it.  For `upcxx-meta` only
the environment is used.

These tools are expected to be sufficient for most simple development tasks,
including working with a user's bug reproducer (even one with its own Makefile),
without the need to complete an install with two versions of GASNet-EX and
`libupcxx.a` for every detected conduit.

By default, these scripts remind you that you are using their build-dir
versions with the following message on stderr:  
      `INFO: may need to build the required runtime.  Please be patient.`  
This can be suppressed by setting `UPCXX_QUIET=1` in your environment.

In addition to the scripts in `<upcxx-build-dir>/bin`, there are a series
of make targets which honor the following nobs-inspired environment variables
(which can also be specified on the make command line):  

* `DBGSYM={0,1}`
* `ASSERT={0,1}`
* `OPTLEV={0,3}`
* `UPCXX_BACKEND=gasnet_{seq,par}`
* `GASNET_CONDUIT=...`

Note that unlike with `nobs`, the variables `CC`, `CXX`, `CROSS` and `GASNET`
are *not* honored by `make` because their values were "frozen" when
`configure` was run.

The make targets utilizing the variables above:

* `make exe SRC=foo.cpp [EXTRAFLAGS=-Dfoo=bar]`  
  Builds the given test printing the full path of the executable on stdout.  
  Executables are cached, including sensitivity to `EXTRAFLAGS` and to changes
  made to the `SRC` file, its crawled dependencies, and to UPC\+\+ source files.
  Note that `EXTRAFLAGS` does not influence UPC\+\+ or GASNet library builds.
* `make run SRC=foo.cpp [EXTRAFLAGS=-Dfoo=bar] [ARGS='arg1 arg2'] [RANKS=n]`  
  Builds and runs the given test with the optional arguments.  
  Executables are cached just as with `exe`.
* `make exe-clean` and `make exe-clean-all`  
  If you believe caching of executables used by the `exe` and `run` targets is
  malfunctioning, then `make exe-clean` will clear the cache for the library
  variant selected by the current environment, while `make exe-clean-all` clears
  all executable caches.

* `make upcxx`, `make upcxx-run` and `make upcxx-meta`  
  Ensures the required libraries are built (and up-to-date) and prints to stdout
  the full path to an appropriate script specific to the current environment.

* `make upcxx-single`  
  Builds a single instance of `libupcxx.a` along with its required GASNet-EX
  conduit and an associated bottom-level `upcxx-meta`.  Use of this target
  with `-j<N>` can be useful to "bootstrap" the utilities in `bin`, which
  might otherwise build their prerequisites on-demand *without* parallelism.
* `make gasnet-single`  
  Builds a single GASNet-EX conduit.

The `exe` and `run` targets accept both absolute and relative paths for `SRC`.
Additionally, if the `SRC` value appears to be a relative path, but does not
exist, a search is conducted in the `test`, `example` and `bench` directories
within `<upcxx-src-path>`.  So, `make run SRC=hello_upcxx.cpp` and `make run
SRC=issue138.cpp` both "just work" without the need to type anything so
cumbersome as `<upcxx-src-path>/test/regression/issue138.cpp`.

Note that while bash syntax allows the following sort of incantation:  
    `X=$(DBGSYM=1 ASSERT=1 OPTLEV=0 echo $(make exe SRC=hello_gasnet.cpp))`  
passing of the environment variables on the make command line can simplify this
slightly to:  
    `X=$(make exe DBGSYM=1 ASSERT=1 OPTLEV=0 SRC=hello_gasnet.cpp)`

Additional make targets:

* `make upcxx-unconfig`  
  This gives a mechanism to remove all generated `upcxx_config.hpp` and
  `gasnet.{codemode}.mak` files.
  This may be necessary when modifying the GASNet sources or anything that
  could invalidate the generated files.  While modifying the probe scripts
  will trigger regeneration of the header, the special treatment gmake gives
  to files it includes prevents doing the same for the makefile fragments.
  Note that `make clean` is also sufficient.

* `make gasnet [DO_WHAT=sub_target] [NETWORK=xyz] [UPCXX_CODEMODE={opt,debug}]`  
  This is a shortcut for `make -C <CONDUIT_DIR> sub_target`, where the value of
  `<CONDUIT_DIR>` is computed from the `NETWORK` and `UPCXX_CODEMODE`, which
  default to the default network and `debug`, respectively.  The default
  `sub_target` is `all`.  
  The `gasnet` target is intended to serve in support scenarios where one wants
  to build and/or launch GASNet-EX jobs independent of libupcxx.  Therefore,
  the names `NETWORK` and `UPCXX_CODEMODE` were chosen to match the concepts
  used in user-facing documentation.  
  Example: `make gasnet DO_WHAT=run-tests-seq` will build and run all of the
  GASNet tests of the default network in debug mode.

* `make list-networks`  
  This prints a human-readable list of the detected conduits *if* the GASNet
  configure step has run, or the list of all supported conduits otherwise.
  Use of `make gasnet-single list-networks` is sufficient to ensure one
  always gets the list of detected conduits.

* `make config-summary`  
  This prints the GASNet configure summary output *if* the GASNet
  configure step has run. This can provide more details about major GASNet
  feature detections and any configure warnings.

* `make dev-tests` and `make dev-check`  
  These are maintainer versions of `tests` and `check` which operate on nearly
  all tests and examples in the repo in both for both seq and par threadmode,
  and with the supported codemodes (both, unless `--enable-single` mode).
  Tests requiring CUDA are included conditionally.
  See "To add tests or examples", below, for information on how to control
  which tests and examples are included.  Additionally, the variables
  `TEST_EXCLUDE_ALL`, `TEST_EXCLUDE_SEQ` and `TEST_EXCLUDE_PAR` are
  whitespace-separated lists of manual exclusions which can be set in the
  environment or on the make command line.  For instance, since the current
  (as this is written) PGI compilers are crashing on `future.cpp`, one might
  run with the following to skip that specific test:
    `make dev-check TEST_EXCLUDE_ALL=test/future.cpp`
  By default, `dev-test-*` compiles all tests for all detected networks and
  `dev-check-*` compiles *and runs* all tests for only the default network.
  One can set `NETWORKS` to override these defaults.
  As with non-dev targets, one can optionally set `TESTS` and/or `NO_TESTS`
  to a space-delimited list of test name substrings used as a filter to select
  or discard a subset of tests to be compiled/run.
  One can also inject compile options via `EXTRAFLAGS`, eg `EXTRAFLAGS=-Werror`.

* `make dev-{tests,check}-{opt,debug}`  
  These are versions of `dev-tests` and `dev-check` for a single codemode.

* `make dev-run-tests` and `make dev-tests-clean`  
  Runs or removes tests build by `dev-{tests,check}{,-opt,-debug}` targets.

All make targets described here (as well as those documented for the end-user
in [INSTALL.md](../INSTALL.md)) are intended to be parallel-make-safe (Eg `make
-j<N> ...`).  Should you experience a failure with a parallel make, please
report it as a bug.

## Testing Scripts for Maintainers

In addition to the `dev-*` family of make targets, there exists a collection of
scripts which combine them with additional logic including system-specific
options for configure and logic for running tests on batch-scheduled systems.
These are maintained to include representative coverage of the range of
compilers and networks which we document as officially supported.

These are maintained in a distinct git repository:
[upcxx-ci](https://bitbucket.org/berkeleylab/upcxx-ci).

If that repo is cloned *within* the top-level `upcxx` source directory, then
the `upcxx-ci/dev-ci/*` scripts can be run in place.

## GitLab CI Testing

The `upcxx` git repo contains a 1-line `.gitlab-ci.yml` which just includes a
complete configuration file in the same `upcxx-ci` git repo described in the
preceding section.  This is usable only if you have an account on the
[LBL GitLab server](https://socks.lbl.gov/).  Contact UPC++ project management for access.

There is
[documentation](https://bitbucket.org/berkeleylab/upcxx-ci/src/master/gitlab-ci.md)
available describing the various settings to control the CI pipelines.
Additionally, since the UI provided by GitLab for launching CI pipelines is
"minimal" (to be kind), we have a
[frontend](https://upcxx-bugs.lbl.gov/gitlab-ci/ci-form-socks.html) to simplify
composition of GitLab CI pipeline launch requests.
  
## Internal-Only Make Options

* `UPCXX_CODEMODE={debug,opt}`:  
  The default behavior of `make check` and `make tests` is to only build for
  the 'debug' codemode.  One may `make check UPCXX_CODEMODE=opt` to override.

* `UPCXX_VERBOSE` {unset or `1`}:  
  Adding `UPCXX_VERBOSE=1` to `make {check,tests,test_install}` will echo
  compilation commands as they are executed.

* `COLOR` {unset or `1`} on command line only (not environment):  
  Adding `COLOR=1` to `make {check,tests,test_install,run,exe}` or any make
  command building the UPC\+\+ libraries will pass `-fdiagnostics-color=always`
  to compilers which support it.  This can be used to ensure colorized output
  even when piping to a pager or saving the output to a file.

* `RUN_WRAPPER='$(VALGRIND_WRAPPER)'` on command line:
  Make targets that run multiple tests (check, run-tests, etc) will wrap the 
  runs in a valgrind leak check. This greatly slows execution time, and
  also disables the normal test timeout mechanism. 
  Requires a working valgrind tool.
  Additional settings recommended for running valgrind on our dev-tests suite:
  ```text
  env UPCXX_VERBOSE=1 UPCXX_OVERSUBSCRIBED=1 OMP_NUM_THREADS=1 TEST_ARGS_PERSONA_EXAMPLE=100 
  ```

## Internal-Only Configuration Options

This serves as the place to document configure options that aren't hardened
enough to be part of the user-facing docs.

* `--with-mpsc-queue={atomic,biglock}`: The implementation to use for multi-
  producer single-consumer queues in the runtime (`upcxx::detail::intru_queue`).
    * `atomic`: (default) Highest performance: one atomic exchange per enqueue.
    * `biglock`: Naive global-mutex protected linked list. Low performance, but
      least risk with respect to potential bugs in implementation.
  The legacy environment variable `UPCXX_MPSC_QUEUE` is still honored at
  configure-time, but this behavior is deprecated.

* `--enable-single={debug,opt}`:  This limits the scope of make targets to only
  a single GASNet-EX build tree.  This has the side-effect of permitting (but not
  requiring) `--with-gasnet=...` to name an existing external GASNet-EX *build*
  tree (it must otherwise name a *source* tree or tarball).  
  This option is really only intended for use by our CI infrastructure, which
  operates on/with GASNet-EX build trees.  Since attempts to do anything outside
  the "scope" of the single-mode will likely fail in unexpected ways, this mode
  is likely to be more of an annoyance than an advantage in manual use, other
  than when it is necessary to reproduce the CI environment.

* `-v` or `--verbose`:  This option is intended to support debugging of
  `configure` itself.  This should be the first command line option if one
  desires to debug option parsing, since it invokes `set -x` when this option is
  processed.

If you add a new configure setting that alters the behavior of the final UPC++ install,
you should also consider adding the resulting Makefile variable to the list in the
`do-config-summary` target which is used to generate the `upcxx-info` output.

## Guide to Maintenance Tasks

#### To add a UPC\+\+ runtime source file

A list of sources to be compiled into `libupcxx.a` is maintained in
`bld/sources.mak`.  Simply add new library sources to `libupcxx_sources`.

#### To add a UPC\+\+ header file

Header files are "crawled" for dependency information, and at installation a
crawl rooted at `src/upcxx_headers.cpp` is used.  So, in general it is not
necessary to add new header files to any manually-maintained list.  If there are
headers missing from an install, then it is appropriate to update
`src/upcxx_headers.cpp` to ensure then are reached in the crawl.

To generate the unique include guards used in the UPC\+\+ headers, use the
script `utils/uuifdef.sh`.

#### To add a configure probe

Configure probes run after GASNet has been configured can generate content in
`upcxx_config.hpp` or `gasnet.{codemode}.mak`.

Adding a configure probe requires writing a bash script taking a set of
pre-defined environment variables as input, and generating content on `stdout`
to be included in the generated `upcxx_config.hpp` or `gasnet.{debug,opt}.mak`
files.  To be run, a script must be added to one of the variables
`UPCXX_CONFIG_SCRIPTS` or `GASNET_CONFIG_SCRIPTS` in `bld/config.mak`.

More details appear in [utils/config/README.md](../utils/config/README.md).

#### To add tests or examples

By default, `make dev-check` and `make dev-tests` will build all `.cpp` files
found (non-recursively) in directories enumerated in the `test_dirs` variable
defined in `bld/tests.mak` (excluding untracked files when in a git clone).
The variables with a `test_exclude_` prefix will exclude certain `.cpp` files
or limit their build to specific conditions (such as `test_exclude_par` for
seq-only tests).

See the comments in `bld/tests.mak` for all the details on the search and
exclusion logic.

Additionally, `TEST_FLAGS_*` variables in `bld/tests.mak` can be used to
provide test-specific compiler flags, such as for OpenMP.

Additionally, all `.sh` scripts found by the same search and exclusion steps
are run to generate tests.  The name of the test to generate is derived from
the basename of the script and passed as the only argument.  The script name
may optionally have a leading `.` prefix (hiding it from normal directory
listings) which is stripped off by the testing infrastructure during test
generation.  The script file is run using the `$(UPCXX_BASH)` interpreter
selected by `configure`.  The environment provided to the script contains all
of the variables exported in the generated `Makefile` in the top-level build
directory, plus the following:

* `UPCXX_{CODEMODE,THREADMODE,NETWORK}`  
  These setting influence the behavior of `upcxx` and `upcxx-meta`, which are
  both available in `$upcxx_bld/bin`.
* `TEST_FLAGS_[BASENAME]`  
  Just as with the `.cpp` tests, a variable named for the script can be set in
  `bld/tests.mak` (and overridden by the user or CI scripting) to provide any
  relevant "flags" to the script.  Unlike `.cpp` tests, however, a script is
  not passed the flags on the command line, and should instead access the
  environment variable.
* `GASNET_{CXX,CC}_{FAMILY,SUBFAMILY}`  
  These report the family, and sub-family if any, of the compilers.  They can
  be used to provide appropriate conditional logic in addition to use of
  `TEST_FLAGS_[BASENAME]`.  However, it is strongly recommended to allow for
  overrides of any settings/behaviors derived from these variables whenever
  practical.
* `EXTRAFLAGS`  
  This environment variable is provided to allow users and CI scripting to
  add flags to every test compilation.  This is appended uniformly to the
  command lines for building `.cpp` tests, and its strongly recommended that
  the same be done for compilation of UPC\+\+ code in scripts.

In addition to the recommendations given with the environment variables,
above, the following practices are encouraged:

* Note that a given script may be run multiple times concurrently with
  different arguments and environment.  Therefore, care must be taken in
  naming temporary files.  The output file name is guaranteed to be unique
  within the current working directory, and so may safely be used to derive
  temporary file names.
* The exit code of the script is used by the caller to know if the build has
  succeeded or failed.  Ensure it is accurate.  In particular, use of `set -e`
  is recommended to ensure the script terminates upon the first failure of any
  command, unless error recovery is implemented.
* Upon failure, the script should ensure the output file is removed.
* Temporary files should be removed on termination (both normal and abnormal).
* Use of `set -x` is recommended to ensure that `UPCXX_VERBOSE` will capture
  not only the output of commands run in the script, but the commands
  themselves.
* Be aware that the test infrastructure applies the same warning detection
  to the output of build scripts as to compilation of `.cpp` tests.  So,
  appropriate use of warning suppression flags or output filtering should be
  employed.  This is one use of the compiler family variables.

Finally, there is a mechanism for tests which cannot be run directly using
`upcxx-run ... [full-test-name]`.  If a script compiling a test also generates
a matching file with a `.runcmd` suffix then it is used to construct the final
arguments to `upcxx-run`.  In the absence of a `.runcmd` tests are run using

```bash
upcxx-run ... ./[full-test-name] [app-args]
```

When a `.runcmd` exists this becomes approximately
```bash
env RANKS=... NETWORK=...  upcxx-run ... $(./[full-test-name].runcmd [app-args])
```

This passes the application arguments to the `.runcmd`, allowing it to either
consume them or forward them to the test by echoing them.  If the `.runcmd` is a bash
script, one can get the test name using `${0%.runcmd}`.  The settings of `RANKS` and
`NETWORK` in the environment provide other pertinent information.  In addition,
any settings given in `TEST_ENV_[short-test-name]` will also be in the
environment of the `.runcmd`.

The command line echoed by the `.runcmd` must not make assumptions about the
value of `$PATH` when it is run.  In particular, use of `${0%.runcmd}` shown
above is recommend to preserve any directory part in `$0` (which `basename`
would remove, for instance).

See [hello_via_shell.sh](../test/hello_via_shell.sh) for an example script
demonstrating several of the best practices given above.

#### To add tests that are intended to generate a compile error

When source file NAME.cpp has a matching NAME.errc file in the same directory,
that indicates the test is expected to generate a compile error, and will be
considered a failure if compilation succeeds. In addition, the `*.errc` file
contains a list of perl-compatible regular expressions, one per line,
where at least one pattern must match the output generated by the compiler
in order for the test to succeed.  To accept any compile error, use the pattern '.'.
Empty lines and lines starting with '#' are ignored. All whitespace (including
new-lines) in the output is collapsed to a single space before matching,
because some compilers (*cough*, PGI) will wrap lines such that they insert
line breaks and whitespace between words inside a static_assert message.

This also works for `.sh`-suffixed tests.

#### To add tests to `make check` and `make tests`

Keep in mind that these targets are the ones we advise end-users (including
auditors) to run.  Tests that are not stable/reliable should not be added.
If/when it is appropriate to add a new test, it should be added to either
`test_sources_seq` or `test_sources_par` (depending on the backend it should be
built with) in `bld/tests.mak`.

#### Add a new GASNet-EX conduit

UPC\+\+ maintains its own list of supported conduits, allowing this to remain
a subset of GASNet-EX's full list as well as ensuring some targets (like
`tests-clean`) operate even when a build of GASNet-EX is not complete.  To add
to (or remove from) this list, edit the setting of the `ALL_CONDUITS` variable
in both `bld/gasnet.mak` and `configure`.

For conduits we rather not support officially (including MPI and possibly ones
with "experimental" status in GASNet-EX), there is an `UNOFFICIAL_CONDUITS`
variable (again, in both `bld/gasnet.mak` and `configure`).
At the time of writing this, the only impact of inclusion is to
skip the given network(s) in `make tests` and `make test_install`.

#### Testing GASNet-EX changes

Because all make targets include GASNet source files in their dependency
tracking, use of the Workflow described above can also be applied when
developing GASNet-level fixes to problems with UPC\+\+ reproducers.  If
necessary `make echovar VARNAME=GASNET` can be used to determine the GASNet
source directory in use (potentially created by `configure`).

#### To add a new supported compiler family

Currently, logic to check for supported compiler (family and version checks)
still lives in `utils/system-checks.sh`.

However, build-related configuration specific to the compiler family is kept in
`bld/compiler.mak`.  Current configuration variables in that file provide
documentation of their purpose, as well as some good examples of how they can
be set conditionally.

User-facing lists of supported compiler families and versions need to be
updated in both `INSTALL.md` (Under "Supported Platforms") and within
`utils/system-checks.sh` (the `RECOMMEND` variable).
