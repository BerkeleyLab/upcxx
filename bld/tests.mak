#
# Lists of test files relative to $(top_srcdir)
#

###
# Section 1: user tests
# Built by 'make tests` and `make check`
###


test_sources_seq = \
	test/hello_upcxx.cpp \
	test/alloc.cpp \
	test/atomics.cpp \
	test/barrier.cpp \
	test/collectives.cpp \
	test/dist_object.cpp \
	test/future.cpp \
	test/global_ptr.cpp \
	test/local_team.cpp \
	test/memory_kinds.cpp \
	test/rpc_barrier.cpp \
	test/rpc_ff_ring.cpp \
	test/rput.cpp \
	test/vis.cpp \
	test/uts/uts_ranks.cpp

test_sources_par = \
	example/prog-guide/persona-example.cpp \
	test/rput_thread.cpp \
	test/view.cpp

test_progs_seq = $(patsubst %.cpp,%,$(patsubst %.sh,%,$(filter-out $(tests_filter_out_seq),$(test_sources_seq))))
test_progs_par = $(patsubst %.cpp,%,$(patsubst %.sh,%,$(filter-out $(tests_filter_out_par),$(test_sources_par))))

###
# Section 2: Maintainer/development tests
# Built by 'make dev-tests` and `make dev-check`
#
# The logic which follows sets variables which controls what is tested.
# The list of tests is constructed as follows:
#  1. A list is made of all `.cpp` files directories listed in `test_dirs`.
#  2. The variable `test_exclude_all` is initialized with matches from step 1
#     which are not valid tests, plus $(TEST_EXCLUDE_ALL) which may be provided
#     in the environment or on the command line, while seq- and par-specific
#     exclusion lists are initialized from $(TEST_EXCLUDE_SEQ) and
#     $(TEST_EXCLUDE_PAR).  These vars are whitespace-separted lists.
#  3. A series of `ifeq/ifneq` conditionals adds to `test_exclude_all`,
#     `test_exclude_seq` and `test_exclude_par`,  based on things like codemode,
#     and cuda-support.  This use of conditionals is designed to be extensible
#     to, for instance, express tests only valid for CUDA+par+debug.
#     NOTE: threadmode is handled via distinct filter variables, rather then
#     conditionals, due to the way seq and par are merged into a single list
#     for use by the "main" testing logic.
#  4. An "second round" of conditionals handles enumeration of known failures.
#     Unlike step #3, this is intended to express the status of the
#     implementation rather than a property of the test.
###

#
# Section 2. Step 1.
# List of directories containing .cpp files (searched NON-recursively):
#
test_dirs = \
	test \
	test/regression \
	test/neg \
	test/uts \
	bench \
	example \
	example/compute-pi \
	example/gpu_vecadd \
	example/prog-guide \
	example/serialization

ifneq ($(UPCXX_FORCE_LEGACY_RELOCATIONS),1)
test_dirs += test/ccs
endif

#
# Section 2. Step 2.
# Exclusion of files with .cpp suffix which are not suited to `make dev-check`
# Some are pieces of a multi-file test or example.
# Others are not UPC++ tests, are intended to fail, or are not intended to be run.
#
test_exclude_all = \
	$(TEST_EXCLUDE_ALL) \
	test/debug-codemode.cpp \
	test/o3-codemode.cpp \
	test/multifile.cpp \
	test/multifile-buddy.cpp \
	test/ccs/ccs-static-dlopen.sh \
	test/uts/uts.cpp \
	example/prog-guide/rb1d-check.cpp

test_exclude_seq = \
	test/par-threadmode.cpp \
	test/regression/issue432.cpp \
	$(TEST_EXCLUDE_SEQ)

test_exclude_par = \
	test/seq-threadmode.cpp \
	$(TEST_EXCLUDE_PAR)

# PATTERNS to identify compile-only tests that are not intended to be run,
# either because they are intended to fail at runtime or don't run test anything useful
#
test_exclude_compile_only = \
	issue105 \
	issue185 \
	issue219 \
	issue224 \
	issue333 \
	issue412 \
	issue428 \
	issue450 \
	issue613b \
	nodiscard \
	promise_multiple_results \
	promise_reused \
	quiescence_failure \
	sys-header-exclude \
	-threadmode

#
# Section 2. Step 3.
# Conditional exclusion
#

# Unconditionally exclude tests which do not use GASNet and thus
# cannot (in general) be run via `make dev-check`.
# TODO: distinguish `dev-tests` from `dev-check` to make this
# exclusion conditional, thus allowing for compile-only tests
test_exclude_all += \
	bench/alloc_burn.cpp \
	test/hello.cpp \
	test/hello_threads.cpp \
	test/uts/uts_omp.cpp \
	test/uts/uts_threads.cpp

# Conditionally exclude tests that require a valid GPU (any kind) at runtime:
test_requires_gpu_device = \
	bench/gpu_microbenchmark.cpp \
	test/bad-segment-alloc.cpp \
	test/regression/issue432.cpp \
	example/prog-guide/h-d-remote.cpp
ifeq ($(strip $(UPCXX_CUDA)$(UPCXX_HIP)$(UPCXX_ZE)),)
test_exclude_all += $(test_requires_gpu_device)
endif

# Conditionally exclude tests that require a valid CUDA-kind device at runtime:
test_requires_cuda_device = \
        test/cuda-context.cpp \
	example/gpu_vecadd/.cuda_vecadd.sh \
	example/prog-guide/h-d.cpp
ifneq ($(UPCXX_CUDA),1)
test_exclude_all += $(test_requires_cuda_device)
endif

# Conditionally exclude tests that require a valid HIP-kind device at runtime:
test_requires_hip_device = \
	example/gpu_vecadd/.hip_vecadd.sh 
ifneq ($(UPCXX_HIP),1)
test_exclude_all += $(test_requires_hip_device)
endif

# Conditionally exclude tests that require a valid ZE-kind device at runtime:
test_requires_ze_device = \
	example/gpu_vecadd/.sycl_vecadd.sh \
        test/ze_device.cpp 
ifneq ($(UPCXX_ZE),1)
test_exclude_all += $(test_requires_ze_device)
endif

# Conditionally exclude tests that require OpenMP:
ifeq ($(strip $(UPCXX_HAVE_OPENMP)),)
test_exclude_all += \
	example/prog-guide/rput-omp.cpp \
	example/prog-guide/rpc-omp.cpp \
	test/uts/uts_omp_ranks.cpp
else
# Note use of export to ensure shell can use these
export TEST_FLAGS_RPUT_OMP =      $(UPCXX_OPENMP_FLAGS)
export TEST_FLAGS_RPC_OMP =       $(UPCXX_OPENMP_FLAGS)
export TEST_FLAGS_UTS_OMP_RANKS = $(UPCXX_OPENMP_FLAGS)
export OMP_NUM_THREADS ?= 4
endif

# Conditionally exclude tests that require C++17:
ifeq ($(strip $(UPCXX_HAVE_CXX17)),)
test_exclude_all += \
	test/regression/issue469.cpp
else
export TEST_FLAGS_ISSUE469=-std=c++17
endif

# Conditionally exclude based on UPCXX_CODEMODE
ifeq ($(strip $(UPCXX_CODEMODE)),debug)
# Opt-only tests (to exclude when CODEMODE=debug)
test_exclude_all += \
	# CURRENTLY NONE
else
# Debug-only tests (to exclude when CODEMODE=opt)
test_exclude_all += \
	# CURRENTLY NONE
endif

# Conditionally exclude based on UPCXX_THREADMODE
# NOTE use of distinct variables rather than `ifeq`
# SEQ-only tests:
test_exclude_par += \
	test/regression/issue133.cpp
# PAR-only tests:
test_exclude_seq += \
	test/hello_threads.cpp \
	test/rput_thread.cpp \
	test/regression/issue142.cpp \
	test/regression/issue168.cpp \
	test/uts/uts_hybrid.cpp \
	test/uts/uts_omp_ranks.cpp \
	example/prog-guide/rput-omp.cpp \
	example/prog-guide/rpc-omp.cpp \
	example/prog-guide/persona-example.cpp \
	example/prog-guide/persona-example-rputs.cpp \
	example/prog-guide/view-matrix-tasks.cpp

#
# Section 2. Step 4.
# Exclusion of tests which are "valid tests" but known to fail.
#

test_exclude_fail_all = \
	test/regression/issue242.cpp 

test_exclude_fail_seq =

test_exclude_fail_par =

# 
# Section 3. 
# Flags to add to specific tests
# Note use of export to ensure shell can use these
#
export TEST_FLAGS_ISSUE138=-DMINIMAL

TEST_FLAGS_MEMBEROF_PGI=--diag_suppress1427
TEST_FLAGS_MEMBEROF_GNU=-Wno-invalid-offsetof
TEST_FLAGS_MEMBEROF_Clang=-Wno-invalid-offsetof
export TEST_FLAGS_MEMBEROF=$(TEST_FLAGS_MEMBEROF_$(GASNET_CXX_FAMILY))

TEST_FLAGS_ISSUE547_Clang=-Wno-self-move
TEST_FLAGS_ISSUE547_GNU=-Wno-self-move
export TEST_FLAGS_ISSUE547=$(TEST_FLAGS_ISSUE547_$(GASNET_CXX_FAMILY))

# default "fast floating point mode" in recent oneAPI compilers leads to nuisance warnings
TEST_FLAGS_RPUT_RPC_ClangINTEL=-Wno-tautological-constant-compare
export TEST_FLAGS_RPUT_RPC=$(TEST_FLAGS_RPUT_RPC_$(GASNET_CXX_FAMILY)$(GASNET_CXX_SUBFAMILY))

ifeq ($(strip $(UPCXX_PLATFORM_HAS_ISSUE_390)),1)
# issue #390: the following tests are known to ICE PGI floor version when debugging symbols are enabled
# this compiler lacks a '-g0' option, so we use our home-grown alternative to strip off -g
test_pgi_debug_symbols_broken = \
	GPU_MICROBENCHMARK \
	RPC_CTOR_TRACE \
	NODISCARD \
	MEMBEROF \
	MISC_PERF \
	COPY_COVER \
	ISSUE138
endif
$(foreach test,$(test_pgi_debug_symbols_broken),$(eval export TEST_FLAGS_$(test):=$(TEST_FLAGS_$(test)) -purge-option=-g))

ifeq ($(strip $(UPCXX_PLATFORM_IBV_CUDA_HAS_BUG_4150)),1)
  # Compile-time measure(s) to avoid known failures attributable to GASNet bug 4150
  export TEST_FLAGS_COPY_COVER:=$(TEST_FLAGS_COPY_COVER) -DSKIP_KILL
endif

ifeq ($(strip $(UPCXX_PLATFORM_CUDA_HAS_BUG_4396)),1)
  # Compile-time measure(s) to avoid known failures attributable to GASNet bug 4396
  export TEST_FLAGS_CUDA_CONTEXT:=$(TEST_FLAGS_CUDA_CONTEXT) -DSKIP_DEVICE_FREE
endif

# Some tests use std::thread in both SEQ and PAR
test_seq_threaded = \
	VIEW \
	LPC_BARRIER \
	LPC_CTOR_TRACE \
	LPC_STRESS
$(foreach test,$(test_seq_threaded), \
  $(eval export TEST_FLAGS_$(test):=$(TEST_FLAGS_$(test)) $(TEST_THREADED_FLAGS)))

# 
# Section 4. 
# Environment variables and command-line arguments to affect test runs.
# Note use of export to ensure shell can use these
# Also note these settings currently do NOT support env values or individual args with 
# embedded spaces or other special shell characters. Don't do that.

# Tweak benchmarks for efficient coverage, these parameters are too small for good measurements
export TEST_ENV_PUT_FLOOD=fixed_iters=10
export TEST_ARGS_GPU_MICROBENCHMARK='-t 1 -w 1'
export TEST_ARGS_MISC_PERF='1000'
export TEST_ARGS_RPC_PERF='100 10 1048576'

# Suppress zero-length RMA warning from tests making such calls intentionally
test_zero_length_rma = \
        VIS \
        VIS_STRESS \
        RPUT_RPC_CX
$(foreach test,$(test_zero_length_rma), \
  $(eval export TEST_ENV_$(test):=$(TEST_ENV_$(test)) UPCXX_WARN_EMPTY_RMA=0))

#
# Section 5.
# Known failures such as:
# export TEST_KCF_FOO_par_ANY_ANY='Reason foo.cpp fails to compile in par mode'
# export TEST_KRF_BAR_ANY_ANY_ofi='Reason bar.cpp fails to run on ofi-conduit'
# export TEST_KRF_BAZ_seq_opt_ANY='Reason test-baz-seq-opt-* fais to run'
# export TEST_KRF_QUX='Reason qux.cpp always fais to run'
#
# Note that currently these work only with the `dev-*` targets,
# but not with `make check` or `make tests; make run-tests`.
#

ifeq ($(UPCXX_VALGRIND),1)
export TEST_KRF_ISSUE478='Issue 536: Unfulfilled promise leaks memory if it has a dependent future created by then() or when_all()'
endif

#
# End of configuration
#

# exclude untracked files, if any
ifneq ($(wildcard $(upcxx_src)/.git),)
test_exclude_all += $(shell cd $(upcxx_src) && git ls-files --others -- $(test_dirs) | grep -e '\.cpp$$' -e '\.sh$$')
endif

# compose the pieces above
tests_raw = $(subst $(upcxx_src)/,,$(foreach dir,$(test_dirs), \
                                             $(wildcard $(upcxx_src)/$(dir)/*.cpp $(upcxx_src)/$(dir)/*.sh $(upcxx_src)/$(dir)/.*.sh)))
tests_filter_out_seq = $(test_exclude_all) $(test_exclude_seq) $(test_exclude_fail_all) $(test_exclude_fail_seq)
tests_filter_out_par = $(test_exclude_all) $(test_exclude_par) $(test_exclude_fail_all) $(test_exclude_fail_par)
test_sources_dev_seq = $(filter-out $(tests_filter_out_seq),$(tests_raw))
test_sources_dev_par = $(filter-out $(tests_filter_out_par),$(tests_raw))
test_progs_dev_seq = $(shell echo $(filter-out $(tests_filter_out_seq),$(tests_raw)) | $(PERL) -pe 's@/\.?([^/\s]+)\.(sh|cpp)(\s|$$)@/\1 @g')
test_progs_dev_par = $(shell echo $(filter-out $(tests_filter_out_par),$(tests_raw)) | $(PERL) -pe 's@/\.?([^/\s]+)\.(sh|cpp)(\s|$$)@/\1 @g')

