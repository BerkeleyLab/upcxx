## ChangeLog

This is the ChangeLog for public releases of [UPC++](https://upcxx.lbl.gov).

For information on using UPC++, see: [README.md](README.md)    
For information on installing UPC++, see: [INSTALL.md](INSTALL.md)

### 2023.12.15: Release 2023.9.0

General features/enhancements: (see specification and programmer's guide for full details)

* NEW: Experimental accelerated memory kinds support for Intel GPUs with HPE Slingshot-11
    - See [INSTALL.md](INSTALL.md) for more information
* Reduce CPU overheads along the round-trip LPC return path in `persona::lpc()`
* Reduce CPU overheads for some small `copy()` operations involving CUDA/HIP GPUs
* New `*_device::uuid()` query for GPU hardware UUID
* Add human-readable memory sizes to shared heap exception messages
* New `sycl_vecadd` target in `examples/gpu_vecadd` performs vector addition on Level Zero devices

Infrastructure changes:

* The value of `UPCXX_CCS_MAX_SEGMENTS` must fall between the number of segments
  loaded at init time (plus some unspecified padding) and 32767. Values outside this
  range are silently raised or lowered to meet this requirement.
* Support for an additional compiler family on HPE Cray EX systems:
    - Intel oneAPI compilers via PrgEnv-intel
    - See [INSTALL.md](INSTALL.md) for details such as minimum versions.
* Support for the Cray XC platform is now deprecated and will be removed in a
  future release.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #594: CCS: Add `--build-id` to linker flags on Linux
* issue #596: Failure of in-build-tree utils when configured without a default network
* issue #600: `upcxx::local_team_position()` returns incorrect results for discontiguous layouts
* issue #604: CCS: Uninitialized variable when reading `-Wl,--build-id` if algorithm is not sha1
* issue #605: library build failure with GCC 13.1.0
* issue #608: Add device UUID to `Device::kind_info()`
* issue #609: Accept LPC function object callbacks that can only be invoked by rvalue
* issue #613: Warnings from persona.hpp on `progress_required()` with GCC 13.1.0
* issue #616: Linker warning on macos
* issue #617: Spawner warnings (upcxx-run) with Python 3.12
* issue #618: CCS segment limit exceeded on MacOS
* spec issue 104: `discharge()` from the restricted context is an error
* spec issue 206: Add `future::is_ready()` as a synonym for `future::ready()`

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

  - ofi-conduit now defaults to setting envvars `FI_MR_CACHE_MAX_COUNT` and
    `FI_MR_CACHE_MAX_SIZE` for cxi provider, partially addressing bug 4676.
  - ibv-conduit now attempts to maximize `RLIMIT_MEMLOCK` by default. 
  - bug4172: crash in ucx-conduit atexit handlers when mpi interop is enabled
  - bug4413: (partial fix) set `FI_UNIVERSE_SIZE` (conflicting provider requirements)
  - bug4594: UCX should not enable native atomics unconditionally
  - bug4598: ucx-conduit + ssh-spawner `GASNET_FREEZE` support is unusable
  - bug4655: ucx: bad exits on Summit
  - bug4663: failure compiling pmi-spawner with PMIx 4.2.0 and higher
  - bug4665: Cray PMI configure detection logic should be smarter
  - bug4669: Some `GASNET_OFI_DEVICE_*` and `GASNET_IBV_PORTS_*` settings ignored
  - bug4670: UCX environment personalized prefix not working as we document
  - bug4676: (partial fix) ofi-conduit RMA performance issues (cxi & verbs providers)
  - bug4677: Linker warnings from Xcode 15

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2023.9.0](docs/spec.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* Invoking `discharge()` from inside the restricted context is now forbidden,
  where previously it could lead to deadlock. 
* `discharge()` and `progress_required()` now default to selecting all personas
  active with the calling thread. The optional argument can override this behavior.
* Member function `future::ready()` has been renamed to `future::is_ready()`, for
  consistency with similar function names elsewhere in the library. 
  The old function name is now deprecated and may be removed in a future release. 

### 2023.03.31: Release 2023.3.0

Improvements to GPU memory kinds:

* NEW: Experimental support for Intel GPUs using oneAPI Level Zero, 
  see [INSTALL.md](INSTALL.md) for details.
    - `configure --enable-ze` flag activates new `ze_device` memory kind
    - This memory kind implementation is currently reference-only and is
      believed to be functionally correct, but has not been tuned for performance.
    - `copy()` operations on `ze_device` memory are currently staged through
      host memory and do not yet leverage network-direct RDMA.
    - `ze_device` includes new experimental member functions designed to streamline
      interoperability with other portions of the oneAPI software ecosystem.
* New `device_allocator::segment_{size,used}()` queries for device segment status
* New `*_device::kind_info()` query for GPU hardware configuration
* New experimental support for `hip_device` using HIP-over-CUDA on Nvidia GPUs.

General features/enhancements: (see specification and programmer's guide for full details)

* Enhancements to `dist_object`:
    - Allow `dist_object<T>` to be constructed non-collectively in an inactive state,
      including before UPC++ initialization, with or without a `T` value.
    - Add queries of whether a `dist_object` holds a value or is active.
    - Enable emplacement of a new `T` value into an existing `dist_object<T>`.
    - Enable an inactive `dist_object` to be activated.
    - `dist_object<T>` is now MoveAssignable when `T` is MoveConstructible
      and MoveAssignable.
* New `upcxx -info` option suppresses compilation and outputs detailed information
  regarding the UPC++/GASNet-EX libraries and configuration in-use.
* New `upcxx-info` convenience script is an alias for `upcxx -info`
* `local_team` members are now officially guaranteed to have consecutive rank
   indexes in `world()`.
* Console output from `upcxx::init()` in verbose mode now compresses process
  identification information to one line per `local_team`.
* `entry_barrier` arguments removed from `experimental::relo::verify_{segment,all}`

Infrastructure changes:

* Support for additional compiler families on HPE Cray EX systems:
    - AMD compilers via PrgEnv-amd and PrgEnv-aocc
    - Nvidia compilers via PrgEnv-nvidia and PrgEnv-nvhpc
    - See [INSTALL.md](INSTALL.md) for details such as minimum versions.
* AMD "AOCC" compilers 2.3+ are now supported on Linux/x86\_64 hosts.
* UPC++ library build now outputs a GASNet-EX configuration summary near the end.
* Integration with Berkeley UPC is now deprecated and may be removed in a future release.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #348: libstdc++ enforcement for PGI and NVHPC compilers
* issue #519: libupcxx build should echo GASNet configure summary
* issue #530: Share `gex_MK_t` objects between endpoints
* issue #531: Consider supporting HIP-over-CUDA
* issue #532: Support `--enable-hip` on PGI/NVHPC
* issue #543: Add `upcxx-info`
* issue #547: MoveAssignment operators and self-assignment
* issue #548: Fix undocumented dependency arc involving `experimental::relo::verify_{segment,all}`
* issue #564: Improve guide's local-team example
* issue #565: `upcxx-run --help` fails in a build directory
* issue #566: re-configure in a dirty build tree often does not apply new settings
* issue #572: Fix unintended user-level progress in `experimental::relo::verify_{segment,all}`
* issue #573: Assertion failure in `segmap_cache::lookup_at_idx` for multi-threaded CCS
* issue #574: Document `local_team`-is-always-contiguous-in-world behavior 
* issue #575: Improve guide's broadcast example
* issue #584: Confusing behavior if `timeout` is present on front-end but not on compute node
* issue #587: `upcxx::optional` `constexpr` operators lack assertions
* spec issue 192: Move semantics for distributed objects

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

  - New opt-in work-around for bug 4461 (failure of the Slingshot-11 cxi
    provider under certain AM traffic patterns) replaces a less effective
    workaround introduced in GASNet-EX 2022.9.0.
  - bug4157: ibv XRC deadlock under certain loads
  - bug4179: ofi: failures specific to verbs provider
  - bug4427: ofi-conduit failures with `RDMADISSEM` barrier
  - bug4507: ofi "message too long" errors on enormous RMA
  - bug4517: ofi/verbs 'no associated AM handler function' errors with libfabric 1.11 or older
  - bug4527: Erroneous/confusing startup warning with psm2 provider
  - bug4567: broken support for psm2 provider in libfabric < 1.10
  - bug4596: smp-conduit lacks multi-process `GASNET_FREEZE` support
  - bug4597: ofi-conduit + ssh-spawner `GASNET_FREEZE` support is unusable
  - bug4606: current `aprun` not recognized by gasnetrun

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2023.3.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2023.3.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* Calls to `{cuda,hip}_device::device_n()` are now prohibited before `upcxx::init()`
* There is now a runtime limit on verified code segments, defaulting to 256, 
  controlled by the `UPCXX_CCS_MAX_SEGMENTS` environment variable.

### 2022.09.30: Release 2022.9.0

General features/enhancements: (see specification and programmer's guide for full details)

* NEW: Memory kinds support for HPE Cray EX systems with AMD and NVIDIA GPUs
  now leverages GPUDirect RDMA (GDR) and ROCmRDMA acceleration technology on
  supported networks.  See [INSTALL.md](INSTALL.md) for full details.
* `upcxx-run` verbosity levels have been adjusted, moving some of the spammier/non-scalable
  output to verbosity level three (i.e., `upcxx-run -vvv`).
* Fixed several compatibility issues with the ROCm/HIP SDK
* Several robustness and performance improvements to CCS RPC support

Serialization changes:

* Added new `upcxx::optional` template that provides the same interface as
  C++17 `std::optional`, and new overloads of `[Reader]::read_into()`
  and `deserializing_iterator<T>::deserialize_into()` that deserialize
  into a `upcxx::optional`.
* The signature for the user-defined `deserialize()` member-function template
  used for custom class serialization has changed. This interface has been generalized to
  support emplacement into managed storage, as well as construction in raw memory.
  The old signature is now deprecated and may be removed in a future release. 
* Serialization has been implemented for `std::reference_wrapper<T>`, which now
  works analogously to serialization for other reference types.
* Added new `[Reader]::read_overwrite()`, `[Reader]::read_sequence_overwrite()`,
  and `deserializing_iterator<T>::deserialize_overwrite()` functions
  that work analogously to their `*_into()` counterparts, but additionally
  destruct target objects before deserializing into them.
* See [the specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2022.9.0.pdf) for further details.

Infrastructure changes:

* Improved configure defaults have reduced the complexity of building
  on an HPE Cray EX system.  See [INSTALL.md](INSTALL.md) for details.
* Added initial/experimental support for the RISC-V architecture.
  If you have an interest in this platform, please contact us!
* The set of tests run by `make check` has been adjusted to improve coverage and balance.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #501: Poor failure behavior for tests with RANKS=1
* issue #539: PG: dmap-quiescence-test hangs with one process
* issue #544: CCS: Segment verification not clearing previous `bad_verification` flag
* issue #545: CCS: Allow `experimental::relo::debug*()` before `init()`
* issue #546: CCS: Level 2 cache thread safety
* issue #549: UPC++ headers choke hipcc device-mode compilation
* issue #550: ROCm/HIP headers break the GNU `__noinline__` attribute
* issue #551: CCS: Thread safety race in segment verification
* issue #552: Compilation errors for `write` and `read_into` on a multidimensional array
* issue #553: CUDA 11.0.3 fails to recognize aggregate initialization in some contexts
* issue #554: UPC++ headers choke ROCm 5.x hipcc device-mode compilation
* issue #555: Error running HIP/ROCm examples
* issue #556: CCS: Fix race condition at segment verification exit
* issue #562: nvc++ 22.5 misparses serialization.hpp
* spec issue 185: Prohibit deprecated initiation of internal/none collectives in progress
* spec issue 195: Semantics of `read_(sequence_)into()` with respect to destruction
* spec issue 196: Managed storage for use with `deserialize_into()`
* spec issue 197: Redesign Custom Serialization `deserialize()` for emplacement
* spec issue 198: Requirement that deserialized types be MoveConstructible is too strong
* spec issue 199: Add serialization through `std::reference_wrapper`

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

  - Scaling improvements to startup costs (memory and time) in all conduits
  - bug4083: incorrect GatherAll algorithm selection at large scale
  - bug4434: RFE: runtime adjustment of ofi-conduit MaxMedium
  - bug4432: OFI provider selection issues
  - bug4448: smp-conduit incorrectly duplicates `GASNET_VERBOSEENV` output
  - bug4450: GCC 12.x bogus dangling-pointer warning building UPC++ tests
  - bug4451: GCC 12.x bogus use-after-free warning from UPC++ future library
  - bug4454: Scaling issues in `gasneti_segmentLimit()`
  - bug4490: startup hang for large `GASNET_MAX_SEGSIZE` and huge pages > 4MB
  - bug4496: SEGV in `gasnete_coll_pf_tm_reduce_TreePutSeg` for reduce-to-all
  - bug4509: Non-scalable reduction temporaries

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2022.9.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2022.9.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* Initiating collective operations with a progress level of `internal` or `none` from within
  the restricted context (within a callback running inside progress), an action
  deprecated with a runtime warning since 2020.10.0, is now prohibited with a fatal error.
  For details, see spec issue 169.
* `experimental::relocation::rebuild_cache()` has been removed.
* `[Reader]::read_into()`, `[Reader]::read_sequence_into()`, and
  `deserializing_iterator<T>::deserialize_into()` on typed
  (non-`void*`) pointers to non-TriviallyDestructible types are now
  prohibited with a static assertion. Use `*_overwrite()` instead, or
  insert an explicit cast to `void*`. See spec issue 195 for details.

### 2022.03.31: Release 2022.3.0

Improvements to GPU memory kinds:

This release features a number of synergistic improvements to the UPC++ memory kinds
feature that supports efficient PGAS communication involving GPU memory buffers.

* NEW: Memory kinds support for AMD GPUs using ROCm/HIP, see [INSTALL.md](INSTALL.md).
    New `configure --enable-hip` flag activates new `upcxx::hip_device` class.
    This includes native offload support for `upcxx::copy()` using ROCmRDMA on 
    recent InfiniBand network hardware - see GASNet-EX documentation for details.
* `cuda_device` and `hip_device` are derived from new abstract base class `gpu_device` and
  `device_allocator<Device>` is now derived from new abstract base class `heap_allocator`.
  These help enable vendor-agnostic polymorphism in use of memory kinds.
* New optional interface to GPU memory kinds simplifies startup code, e.g.:    
    `auto gpu_alloc = make_gpu_allocator(2UL<<20);`    
  creates a 2MB device segment with a "smart" choice of GPU, and:    
    `auto gpu_alloc = make_gpu_allocator<hip_device>(2UL<<20, 2);`    
  creates a `device_allocator` for a segment on HIP GPU number 2.
* Several new members have been added to `device_allocator` to provide convenience
  and support the above improvements. See the specification for details.
* These improvements are demonstrated in `example/gpu_vecadd` a renamed version of
  the `cuda_vecadd` kernel example which now supports either GPU vendor.

General features/enhancements: (see specification and programmer's guide for full details)

* Experimental support for making RPC calls to functions in executable code segments
  other than the core UPC++ application, such as those in dynamic libraries. For
  more information, see [docs/ccs-rpc.md](docs/ccs-rpc.md).
* Performance improvements to `atomic_domain` operations using shared-memory bypass.
* New query `upcxx::local_team_position()` provides job topology information
* `team` and `atomic_domain<T>` are now DefaultConstructible and have a new `is_active()` query
* `team`, `atomic_domain<T>`, `cuda_device` and `device_allocator<Device>` are now MoveAssignable

Infrastructure changes:

* NEW initial support for the HPE Cray EX platform
    - Complete and correct, but still untuned
    - Supports Slingshot-10 and Slingshot-11 NICs via GASNet-EX's
      experimental support for the OFI network API (aka "libfabric").
    - Supports PrgEnv-gnu and PrgEnv-cray.
    - See [INSTALL.md](INSTALL.md) for instructions to enable the
      appropriate support for this platform.
* Memory kinds implementation internals have been factored and restructured, simplifying
  the addition of new memory kinds in future releases.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #288: implementation details of future lead to type errors using `when_all` with inference
* issue #494: Intermittent rpc-ctor-trace failure on `copy-get-d2d(as_rpc)`
* issue #512: ADL fails with `when_all`
* issue #518: configure should warn or prohibit mixed-family/mixed-version compilers
* issue #522: Runtime crash along exception path for `rpc()` returning non-empty `operation_cx::as_future`
* issue #523: Round-trip RPC lacking `operation_cx` should generate an error
* issue #527: Raise PGI version floor to 19.3
* issue #528: `cuda_device::destroy()` incorrectly perturbs CUDA Driver context stack
* issue #534: Prune unnecessary system header includes from upcxx.hpp
* issue #537: Numerical error in Kokkos-based 3d heat conduction examples
* spec issue 173: Add `upcxx::local_team_position()`
* spec issue 188: Add `cuda_device::device_n()`
* spec issue 189: Add MoveAssignable to resource object types
* spec issue 190: `device_allocator` constructor has several problems

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

* bug4211: intermittent udp-conduit exit-time hangs on macOS
* bug4227: Bogus maybe-uninit warning building libgasnet with GCC-11.1+
* bug4297: incorrect nbrhd construction for some multi-homed hosts
* bug4321: Intermittent "EBADENDPOINT" failures in single-node udp-conduit
* bug4345: Multiply defined symbols in aries-conduit w/ recent compilers
* bug4360: Insufficient fixed exit timeouts (ucx, ibv, ofi)
* bug4361: (partial fix) reductions on DT_USER of unbounded length
* bug4366: intermittent exit-time assertion failures from debug memcheck

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2022.3.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2022.3.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* UPC++ headers no longer have the undocumented side-effect of including `<cassert>`. 
  Users are highly encouraged to use `UPCXX_ASSERT()` instead, which offers
  more features and automatically tracks `UPCXX_CODEMODE`. 
  See [docs/implementation-defined.md](docs/implementation-defined.md) for more details.
* UPC++ headers no longer have the undocumented side-effect of including some
  system headers. User programs should directly include system headers they need.
* Library classes `team`, `cuda_device`, `atomic_domain<T>` and `device_allocator<D>`
  are now all `final` and may not be sub-classed.
* `atomic_domain<T>` construction with an empty ops set is now prohibited.
* The three-argument `device_allocator` constructor has been deprecated in favor
  of a new constructor that swaps argument order but provides equivalent functionality.
  The deprecated overload will be removed in an upcoming release.
* An active `device_allocator<Device>` object must now be deactivated prior to
  destruction, via either `Device::destroy()` or `device_allocator::destroy()`.
* RPC calls across code segments must now use multi-segment CCS mode. Cases where
  this "magically" worked are now prohibited. See [docs/ccs-rpc.md](docs/ccs-rpc.md)
  for more details.
* The oldest-supported PGI compiler version is raised to 19.3 on all platforms.
* `bench/cuda_microbenchmark` performance test renamed to `bench/gpu_microbenchmark`
* Prior to this release, the configure script would permit values of `CXX` and
  `CC` which had different families or versions (as long as they were
  link-compatible).  This was particularly easy to do on a Linux system if
  specifying a non-default `CXX` while retaining the default `CC=gcc`.   Such
  mixed configurations are now prohibited.  While there is a configure option
  to convert the enforcement to a warning, such configurations are officially
  unsupported.

### 2021.09.30: Release 2021.9.0

Improvements to on-node communication:

This release features a number of synergistic optimizations that streamline
interprocess communication operations that are satisfied on-node using
shared memory bypass. For details, see: [doi:10.25344/S42C71](https://doi.org/10.25344/S42C71)

* New `as_eager_future()`, `as_defer_future()`, `as_eager_promise()`, and
  `as_defer_promise()` calls for requesting eager or deferred notification of
  future and promise completions.
* Existing `as_future()` and `as_promise()` calls now default to eager
  notification for improved performance.
* New `UPCXX_DEFER_COMPLETION` macro for controlling whether `as_future()` and
  `as_promise()` request eager or deferred notification (see
  [implementation-defined.md](docs/implementation-defined.md) for details).
* New overloads of fetching atomics that avoid overheads of non-empty futures
  and promises.
* Performance improvements to contiguous RMA (`rput`, `rget`) using shared-memory bypass.
* Performance improvements to `upcxx::copy()`, especially for cases amenable to
  shared-memory bypass optimizations and/or not involving device memory.
* Performance improvements to `global_ptr` localization queries and operations,
  especially for smp-conduit.

General features/enhancements: (see specification and programmer's guide for full details)

* `upcxx::rpc` and `upcxx::rpc_ff` calls that encounter shared heap exhaustion
  while allocating internal buffers will now throw an exception instead of crashing.
  For details, see [implementation-defined.md](docs/implementation-defined.md)
* New `team::create` factory constructs teams with less communication than `team::split`
  when each participant can enumerate the membership of its own new team.
* The following future operations are now permitted before UPC++ initialization:
  `make_future()`, `to_future()`, `when_all()`, assignment and copy/move
  constructors.
* Added implementation-defined macros `UPCXX_ASSERT` and `UPCXX_ASSERT_ALWAYS`
* New `UPCXX_KIND_CUDA` feature macro indicates the presence of CUDA support.
* Improve error reporting on failure to open a `cuda_device`.
* Add debug codemode checking for exceptions thrown out of user callbacks into
  library code, which is prohibited by the specification.
* Notable GASNet performance improvements for InfiniBand (ibv) network.
* `bench/cuda_microbenchmark` performance test expanded and improved

Infrastructure changes:

* Multiple improvements to default network selection
    - In addition to selection at configure time, the default network can now
      be selected as late as the `make install` step.
    - In support of the behavior above, the `make all` and `make install`
      steps now report the default and available networks.
    - The build logic for Linux now makes an effort to perform more intelligent
      selection of a default network, rather than always defaulting to `smp`.  
    - The default networks for macOS and Cray XC remain unchanged:
      `smp` and `aries`, respectively.
* The build logic now diagnoses conditions in which `CXX` must be a
  MPI-compatible (wrapper) compiler rather than deferring discovery of the
  problem until `make check`, `make test_install` or even to user application
  link time.
* The "NVIDIA HPC SDK" (or "nvhpc") compiler family is now supported on
  x86\_64 and ppc64le hosts for version 20.9 and newer.
* Intel oneAPI compilers v2021.1.2+ are now supported on x86\_64 hosts.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #242: Lack of backpressure in RPC injection leads to shared memory-exhaustion crashes
* issue #299: de-duplication of installed headers
* issue #354: improve default network selection
* issue #464: assert when calling `global_ptr<T,kind>::local()` on device pointers
* issue #468: Harmless unused variable warnings on clang with `-O -Wall`
* issue #473: Divide by zero in serialization when writing a sequence of
  objects that have empty `UPCXX_SERIALIZED_{FIELDS,VALUES}`
* issue #477: `copy(remote_cx::as_rpc)` may invoke callback in the wrong context
* issue #479: intermittent lpc-stress/opt failures on ARM64
* issue #481: Consider use of `__builtin_launder`
* issue #482: SEQ mode incorrectly requires master as `current_persona` for shared allocation
* issue #487: Renaming unspecified internal `UPCXX_` macros and identifiers
* issue #488: Configure-time failure when mixing GCC + Intel
* issue #490: Clang pedantic warnings on template destructors building the library
* issue #495: failures with ibv-conduit recv thread enabled
* issue #496: Configure mishandling quotes in compiler and flags settings
* issue #500: Invalid teams created by split() are not handled according to spec
* issue #502: Discontiguous job layouts now require `configure --enable-discontig-ranks`
* spec issue 176: Change RPC injection to throw an exception on memory exhaustion

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

* bug4148: ibv/GDR completion issues with multiple communication paths
* bug4150: ibv/GDR premature local completion of Puts from device memory
* bug4209: ibv: improve ALC with respect to bounce buffer use
* bug4230: ssh-spawer de-duplication logic is flawed
* bug4263: remove `AD_MY_NBRHD` check on smp-conduit
* bug4265: Collective scratch management is not thread safe
* bug4266: `gex_Coll_ReduceToAllNB` is not thread safe
* bug4292: ucx and aries can leak events from AM Long
* bug4330: ibv conduit incorrectly implements `HIDDEN_AM_CONCURRENCY_LEVEL`

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2021.9.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2021.9.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* Existing `as_future()` and `as_promise()` calls now default to eager
  notification for improved performance. Deferred notification can be requested
  on a per-call basis by changing `as_future()`/`as_promise()` calls to
  `as_defer_future()`/`as_defer_promise()`, or on a translation-unit basis by
  defining the `UPCXX_DEFER_COMPLETION` macro to 1 prior to including
  `upcxx/upcxx.hpp`.
* Many unspecified macros and identifiers that are defined by the public headers
  have been renamed from a `UPCXX_` prefix to `UPCXXI_`. This naming change reflects
  the fact these undocumented tokens are INTERNAL to the implementation and carry
  no guarantee of stability or functionality. Users are strongly advised to avoid 
  direct reference to any such interfaces.

### 2021.03.31: Release 2021.3.0

General features/enhancements: (see specification and programmer's guide for full details)

* The optimizations and features supporting CUDA GPUs initially previewed in
  the 2020.11.0 Memory Kinds Prototype have been hardened and incorporated
  into this release.
* On platforms with NVIDIA-branded CUDA devices and NVIDIA- or Mellanox-branded InfiniBand
  network adapters (such as OLCF Summit), `upcxx::copy()` uses GPUDirect RDMA
  (GDR) hardware support to offload RMA operations involving GPU memory.
* See [INSTALL.md](INSTALL.md) for instructions to enable UPC++ CUDA support
  and for a list of detailed requirements and known issues.
* New `shared_segment_{size,used}` queries return snapshots of the host shared segment
  size and utilization.
* Updates to GASNet's support for InfiniBand networks (ibv-conduit):
    - Significantly improved performance of both RPC and RMA operations under
      certain conditions
    - Measurable reduction in startup time for medium-scale and large-scale
      jobs with wide SMP nodes.
    - Heterogeneous multirail configurations no longer reduce the size of
      `upcxx::local_team()`.

Improvements to RPC and Serialization:

* The RPC implementation has been tuned and now incurs one less payload copy on
  ibv and aries networks on moderately sized RPCs. Additionally, internal
  protocol cross-over points have been adjusted on all networks. These changes
  may result in noticeable performance improvement for RPCs with a total size 
  (including serialized arguments) under about 64kb (exact limit varies with network).
* The default aries-conduit max AM Medium size has been doubled to ~8kb to improve
  performance of the RPC eager protocol. See aries-conduit README for details on
  the available configure/envvar knobs to control this quantity.
* Arguments to `rpc`, `rpc_ff` and `remote_cx::as_rpc` are now serialized synchronously 
  before return from the communication-injection call, regardless of asynchronous `source_cx`
  completions (which are now deprecated for `rpc` and `rpc_ff`).
* Streamlined some overheads associated with `remote_cx::as_rpc` and RPC replies.

Infrastructure changes:

* The `install` script, deprecated since 2020.3.0, has been removed.
* `make check` (and similar) now accept comma-delimited `NETWORKS` settings,
  in addition to space-delimited.
* The PGI C++ compiler (through version 20.4) remains fully supported. The re-branded
  variant of this host compiler (i.e. `pgc++` or `nvc++` released as NVIDIA HPC
  SDK 20.7 and later) is not currently supported, due to critical defects.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #25: Remove non-public symbols from top-level `upcxx::` namespace
* issue #241: Intermittent validation failures in test/copy.cpp
* issue #245: persona-example deadlocks when --with-mpsc-queue=biglock
* issue #276: Use C++ protection features to enforce abstraction boundaries
* issue #382: Expose shared heap usage at runtime
* issue #408: Cannot register multiple completions against a non-copyable results type
* issue #421: `upcxx::copy()` breaks with PGI optimizer
* issue #422: Improve configure behavior for GASNet archives lacking Bootstrap
* issue #423: Prohibit communication using non-master personas in SEQ mode
* issue #427: Crash after `write_sequence()` where serialized element size is
  not a multiple of alignment
* issue #428: Regression in `rpc(team,rank,..,view)` overload resolution
* issue #429: upcxx library exposes dlmalloc symbols
* issue #430: cannot disable the default network
* issue #432: Some `upcxx::copy()` cases do not `discharge()` properly
* issue #440: Invalid GASNet call while deserializing a global ptr
* issue #447: REGRESSION: bulk `upcxx::rput` with l-value completions
* issue #450: `upcxx::lpc` callback return of rvalue reference not decayed as specified
* issue #455: Performance bug in `rput(remote_cx::as_rpc(...))` with "bare" `remote_cx`
* issue #459: Move unspecified identifiers into a new `upcxx::experimental` namespace
* issue #460: Implementation relies on `std::result_of`, which is deprecated in
  C++17 and removed in C++20

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

* bug4194: ibv: unnecessarily slow startup
* bug4208: ibv: unfortunate multi-rail interactions with PSHM and XRC

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2021.3.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2021.3.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* When compiling for the default "seq" threading mode, interprocess
  communication may only be initiated by the primordial thread, 
  and now additionally requires use of the master persona. 
  For details, see [docs/implementation-defined.md](docs/implementation-defined.md).
* Array types are now prohibited as the element-type template argument to 
  `upcxx::new_` and `upcxx::new_array`.
* The following *unspecified* identifiers, previously in the `upcxx` namespace,
  have all been moved to the new `upcxx::experimental` namespace.
  These interfaces all remain unspecified and experimental, and they are
  subject to change without notice in future revisions:
    - `broadcast_nontrivial`, `reduce_one_nontrivial`, `reduce_all_nontrivial`
    - The non-fast `op_*` reduction constants (e.g. `op_add`)
    - `os_env`
    - `say`
    - `destroy_heap` and `restore_heap`
* The unspecified/obsolete `UPCXX_REFLECTED()` macro and `upcxx::wait()` function have been removed.
* Many other unspecified internal functions and members have been renamed.
  Applications should avoid depending on unspecified functions or members, which
  are subject to change without notice. This in particular includes anything in
  the upcxx sub-namespaces (e.g. `upcxx::detail` and `upcxx::backend`).


### 2020.10.30: Memory Kinds Prototype 2020.11.0

This is a **prototype** release of UPC++ demonstrating the new GPUDirect RDMA (GDR)
native implementation of memory kinds for NVIDIA-branded CUDA devices with
NVIDIA- or Mellanox-branded InfiniBand network adapters.

As a prototype, it has not been validated as widely as normal stable releases,
and may include features and behaviors that are subject to change without notice.
This prototype is recommended for any users who want to exercise the memory kinds
feature with CUDA-enabled GPUs. All other UPC++ users are recommended to use the
latest stable release.

See [INSTALL.md](INSTALL.md) for instructions to enable UPC++ CUDA support
and for a list of caveats and known issues with the current GDR-accelerated
implementation.

Recent changes to the memory kinds feature:

* Relax the restriction that a given CUDA device ID may only be opened once per process
  using `cuda_device`.
* Add a `device_allocator::is_active()` query, and fix several subtle defects with
  inactive devices/allocators.
* Resource exhaustion failures that occur while allocating a device segment now throw
  `upcxx::bad_segment_alloc`, a new subclass of `std::bad_alloc`.
* Debug-mode `global_ptr` checking for device pointers has been strengthened when
  using GDR-accelerated memory kinds.

Requirements changes:

* The PGI/NVIDIA C++ compiler is not supported in this prototype release, due to a
  known problem with the optimizer. Users are advised to use a supported version
  of the Intel, GNU or LLVM/Clang C++ compiler instead. See [INSTALL.md](INSTALL.md)
  for details on supported compilers.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #221: `upcxx::copy()` mishandling of private memory arguments
* issue #421: Regression with `upcxx::copy(remote_cx::as_rpc)`

This prototype library release conforms to the
[UPC++ v1.0 Specification, Revision 2020.11.0-draft](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2020.11.0-draft.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* `device_allocator` construction is now a collective operation with user-level progress.
* `device_allocator::device_id()` is now restricted to `global_ptr` arguments
  with affinity to the calling process.

### 2020.10.30: Release 2020.10.0

General features/enhancements: (see specification and programmer's guide for full details)

* Added support for `global_ptr<const T>` with implicit conversion
  from `global_ptr<T>` to `global_ptr<const T>`. Communication
  operations now take a `global_ptr<const T>` where appropriate.
* `local_team()` creation during `upcxx::init()` in multi-node runs is now more scalable
  in time and memory, providing noticeable improvements at large scale.
* `team::destroy()` now frees GASNet-level team resources that were previously leaked.
* `when_all(...)` now additionally accepts non-future arguments and implicitly
  promotes them to trivially ready futures (as with `to_future`) before performing
  future concatenation. The main consequence is `when_all()` can now replace
  most uses of `make_future` and `to_future` for constructing trivially ready futures,
  and continue to serve as the future combinator.
* Static checking has been strengthened in a number of places, to help provide or improve
  compile-time diagnostics for incorrect or otherwise problematic use cases.
* Added many precondition sanity checks in debug mode, to help users find
  bugs in their code when compiling with `-codemode=debug` (aka, `upcxx -g`).
* Shared heap exhaustion in `upcxx::new_(array)` now throws `upcxx::bad_shared_alloc` (a type
  derived from `std::bad_alloc`) which provides additional diagnostics about the failure.

Improvements to RPC and Serialization:

* Added support for serialization of reference types where the referent is Serializable.
* RPCs which return future values experience one less heap allocation and
  virtual dispatch in the runtime's critical path.
* RPCs which return future values no longer copy the underlying data prior to serialization.
* `dist_object<T>::fetch()` no longer copies the remote object prior to serialization.
* Added `deserializing_iterator<T>::deserialize_into` to avoid copying large
  objects when iterating over a `view` of non-TriviallySerializable elements.
* Objects passed as lvalues to RPC will now be serialized directly from the provided 
  object, reducing copy overhead and enabling passing of non-copyable (but movable) types.
* Non-copyable (but movable) types can now be returned from RPC by reference

Build system changes:

* Improved compatibility with heap analysis tools like Valgrind (at some
  potential performance cost) using new configure option --enable-valgrind
* `configure --enable-X` is now equivalent to `--with-X`, similarly for `--disable`/`--without`
* Tests run by `make check` and friends now enforce a 5m time limit by default

Requirements changes:

* The minimum required `gcc` environment module version for PrgEnv-gnu and
  PrgEnv-intel on a Cray XC has risen from 6.4.0 to 7.1.0.
* The minimum required Intel compiler version for PrgEnv-intel on a Cray XC
  has risen from 17.0.2 to 18.0.1.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #151: Validate requested completions against the events supported by an
  operation
* issue #262: Implement view buffer lifetime extension for `remote_cx::as_rpc`
* issue #288: (partial fix) future-producing calls like `upcxx::make_future` now
  return the exact type `future<T>`. Sole remaining exception is `when_all`.
* issue #313: implement `future::{result,wait}_reference`
* issue #336: Add `static_assert` to prohibit massive types as top-level arguments to RPC
* issue #344: poor handling for `make install prefix=relative-path`
* issue #345: configure with single-dash arguments
* issue #346: `configure --cross=cray*` ignores `--with-cc/--with-cxx`
* issue #355: `upcxx::view<T>` broken with asymmetric deserialization of `T`
* issue #361: `upcxx::rpc` broken when mixing arguments of `T&&` and `dist_object&`
* issue #364: Stray "-e" output on macOS and possibly elsewhere
* issue #375: Improve error message for C array types by-value arguments to RPC
* issue #376: warnings from GCC 10.1 in reduce.hpp for boolean reductions
* issue #384: finalize can complete without invoking progress, leading to obscure leaks
* issue #386: `upcxx_memberof_general` prohibits member designators that end with an array access
* issue #388: `deserialized_value()` overflows buffer for massive static types
* issue #389: `future::result*()` should assert readiness
* issue #391: View of containers of `UPCXX_SERIALIZED_FIELDS` crashes in deserialization
* issue #392: Prevent silent use of by-value communication APIs for huge types
* issue #393: Improve type check error for l-value reference args to RPC callbacks
* issue #400: `UPCXX_SERIALIZED_VALUES()` misoptimized by GCC{7,8,9} with -O2+
* issue #402: Cannot use `promise<T>::fulfill_result()` when T is not MoveConstructible
* issue #405: regression: `upcxx::copy(T*,global_ptr<T>,size_t)` fails to compile
* issue #407: RPC breaks if an argument asymmetrically deserializes to a type that
  itself has asymmetric deserialization
* issue #412: entry barriers deadlock when invoked inside user-level progress callbacks
* issue #413: LPC callback that returns a reference produces a future containing
  a dangling reference
* issue #419: Ensure correct/portable behavior of `upcxx::initialized()` in static destructors
* spec issue 104: Provide a universal variadic factory for future
* spec issue 158: prohibit reference types in `global_ptr` and `upcxx_memberof_general`
* spec issue 160: Deadlocks arising from synchronous collective calls with internal progress
* spec issue 167: `dist_object<T>::fetch` does not correctly handle `T` with asymmetric serialization
* spec issue 169: Deprecate collective calls inside progress callbacks
* spec issue 170: Implement `upcxx::in_progress()` query

This library release conforms to the
[UPC++ v1.0 Specification, Revision 2020.10.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2020.10.0.pdf).
All currently specified features are fully implemented.
See the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for status of known bugs.

Breaking changes:

* Build-time `UPCXX_CODEMODE`/`-codemode` value "O3" has been renamed to "opt".
  For backward compatibility, the former is still accepted.
* `upcxx_memberof(_general)(gp, mem)` now produce a `global_ptr<T>` when `mem` 
  names an array whose element type is `T`.
* `atomic_domain` construction now has user-level progress
* Initiating collective operations with progress level `user` from inside the restricted 
  context (within a callback running inside progress) is now prohibited, and diagnosed
  with a runtime error.  Most such calls previously led to silent deadlock.
* Initiating collective operations with a progress level of `internal` or `none` from within
  the restricted context (within a callback running inside progress) is now a deprecated
  behavior, and diagnosed with a runtime warning. For details, see spec issue 169.

### 2020.07.17: Bug-fix release 2020.3.2

New features/enhancements:

* Shared heap exhaustion in `upcxx::new_(array)` now throws `upcxx::bad_shared_alloc` (a type
  derived from `std::bad_alloc`) which provides additional diagnostics about the failure.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #343: Guarantee equality for default-constructed invalid `upcxx::team_id`
* issue #353: configure: automatically cross-compile on Cray XC
* issue #356: `SERIALIZED_{FIELDS|VALUES}` incorrectly require public constructors
* issue #369: `completion_cx::as_future()` typically leaks
* issue #371: `team_id`s are not "universal" as documented
* issue #373: No `python` in `$PATH` in recent Linux distros
* issue #380: Compile regression on bulk `upcxx::rput` with source+operation completions

Breaking changes:

* Configure-time envvar `CROSS` has been renamed to `UPCXX_CROSS`.
  For backward compatibility, the former is still accepted when the latter is unset.
* Construction of `upcxx::team_id` is no longer Trivial (was never guaranteed to be).
  It remains DefaultConstructible, TriviallyCopyable, StandardLayoutType, EqualityComparable

### 2020.03.12: Release 2020.3.0

Infrastructure and requirements changes:

* The `install` and `run-tests` scripts have been replaced with a `configure`
  script (with GNU autoconf-compatible options) and corresponding `make all`,
  `make check` and `make install` steps.
  See [INSTALL.md](INSTALL.md) for step-by-step instructions.
* The minimum required GNU Make has risen from 3.79 to 3.80 (released in 2002).
* There is no longer a requirement for Python2.7.  While `upcxx-run` still
  requires a Python interpreter, both Python3 and Python2 (>= 2.7.5) are
  acceptable.

New features/enhancements: (see specification and programmer's guide for full details)

* Added support for non-trivial serialization of user-defined types.  See the 
  [new chapter of the programmer's guide](https://upcxx.lbl.gov/docs/html/guide.html#serialization)
  for an introduction, and [the specification](docs/spec.pdf) for all the details.
* Implement `upcxx_memberof(_general)()`, enabling RMA access to fields of remote objects
* `upcxx::promise` now behaves as a CopyAssignable handle to a reference-counted hidden object,
  meaning users no longer have to worry about promise lifetime issues.
* `atomic_domain<T>` is now implemented for all 32- and 64-bit integer types,
  in addition to the fixed-width integer types, `(u)int{32,64}_t`.
* `atomic_domain<T>` implementation has been streamlined to reduce overhead,
  most notably for targets with affinity to `local_team` using CPU-based atomics.
* Improve GASNet polling heuristic to reduce head-of-line blocking
* Installed CMake package file is now more robust, and supports versioning.
* `global_ptr<T>` now enforces many additional correctness checks in debug mode
* Significantly improve compile latency associated with `upcxx` compiler wrapper script
* Improve handling of `upcxx-run -v` verbose options

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #83: strengthen `global_ptr` correctness assertions
* issue #142: Clarify semantic restrictions on `UPCXX_THREADMODE=seq`
* issue #173: upcxx-run for executables in CWD and error messages
* issue #247: unused-local-typedef warning for some compilers
* issue #266: Redundant completion handler type breaks compile
* issue #267: Same completion value can't be received multiple times
* issue #271: Use of `-pthread` in `example/prog-guide/Makefile` breaks PGI
* issue #277: Ensure `completion_cx::as_promise(p)` works even when p is destroyed prior to fulfillment
* issue #280: Off-by-one error in promise constructor leads to breakage with any explicit argument
* issue #282: Improve installed CMake package files
* issue #287: Bogus install warnings on Cray XC regarding CC/CXX
* issue #289: link failure for PGI with -std=c++17
* issue #294: redundant-move warning in completion.hpp from g++-9.2
* issue #304: Bad behavior for misspelled CC or CXX
* issue #323: Incorrect behavior for `global_ptr<T>(nullptr).is_local()` in multi-node jobs
* issue #333: Multiply defined symbol `detail::device_allocator_core<cuda_device>::min_alignment` w/ std=c++2a
* spec issue 148: Missing `const` qualifiers on team and other API objects
* spec issue 155: value argument type to value collectives is changed to a simple by-value T

This library release mostly conforms to the
[UPC++ v1.0 Specification, Revision 2020.3.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2020.3.0.pdf).
The following features from that specification are not yet implemented:

* view buffer lifetime extension for `remote_cx::as_rpc` (issue #262)
* `boost_serialization` adapter class is not yet implemented (issue #330)
* `future<T&&>::{result,wait}_reference` currently return the wrong type.
  E.g. `future<int&&>::result_reference()` returns `int const&` instead of `int&&` (issue #313)

Breaking changes:

* `atomic_domain` has been specified as non-DefaultConstructible since UPC++ spec draft 8 (2018.9)
  This release removes the default constructor implementation from `atomic_domain` (issue #316).
* The trait template classes `upcxx::is_definitely_trivially_serializable` and
  `upcxx::is_definitely_serializable` have been renamed such that the "definitely"
  word has been dropped, e.g.: `upcxx::is_trivially_serializable`.
* `future::{result,wait}_moved`, deprecated since 2019.9.0, have been removed.
  New `future::{result,wait}_reference` calls provide related functionality.

### 2019.09.14: Release 2019.9.0

New features/enhancements: (see specification and programmer's guide for full details)

* `upcxx` has several new convenience options (see `upcxx -help`)
* `upcxx::rput(..., remote_cx::as_rpc(...))` has received an improved implementation
  for remote peers where the dependent RPC is injected immediately following
  the put. This pipelining reduces latency and sensitivity to initiator attentiveness,
  improving performance in most cases (for the exception, see issue #261).
* Accounting measures have been added to track the shared-heap utilization of the
  UPC++ runtime (specifically rendezvous buffers) so that in the case of shared-heap
  exhaustion an informative assertion will fire. Also, fewer rendezvous buffers are
  now required by the runtime, thus alleviating some pressure on the shared heap.
* Environment variable `UPCXX_OVERSUBSCRIBED` has been added to control whether the 
  runtime should yield to OS (`sched_yield()`) within calls to `upcxx::progress()`).
  See [docs/oversubscription.md](docs/oversubscription.md).
* Release tarball downloads now embed a copy of GASNet-EX that is used by default during install.
  Git clones of the repo will still default to downloading GASNet-EX during install.
  The `GASNET` envvar can still be set at install time to change the default behavior.
* A CMake module for UPC++ is now installed. See 'Using UPC++ with CMake' in [README.md](README.md)
* `atomic_domain<float>` and `atomic_domain<double>` are now implemented
* Interoperability support for Berkeley UPC's `-pthreads` mode, see [docs/upc-hybrid.md](docs/upc-hybrid.md)
* New define `UPCXX_SPEC_VERSION` documents the implemented revision of the UPC++ specification

Support has been added for the following compilers/platforms 
(for details, see 'System Requirements' in [INSTALL.md](INSTALL.md)):

* PGI v19.1+ on Linux/x86\_64
* PGI v18.10+ on Linux/ppc64le
* clang v5.0+ on Linux/ppc64le
* PrgEnv/cray with CCE v9.0+ on the Cray XC
* ALCF's PrgEnv/llvm v4.0+ on the Cray XC
* NEW platform: Linux/aarch64 (aka "arm64" or "armv8")
    + gcc v6.4.0+
    + clang 4.0.0+

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #140: `upcxx::discharge()` does not discharge `remote_cx::as_rpc()`
* issue #168: `upcxx::progress_required` always returns 0
* issue #170: `team_id::when_here()` is unimplemented
* issue #181: Library linkage failures when user compiles with a different `-std=c++` level
* issue #184: `bench/put_flood` crashes on opt/Linux
* issue #203: strict aliasing violations in `device_allocator`
* issue #204: No support for `nvcc --compiler-bindir=...`
* issue #210: `cuda_device::default_alignment()` not implemented
* issue #223: `operator<<(std::ostream, global_ptr<T>)` does not match spec
* issue #224: missing `const` qualifier on `dist_object<T>.fetch()`
* issue #228: incorrect behavior for `upcxx -g -O`
* issue #229: Teach `upcxx` wrapper to compile C language files
* issue #234: Generalized operation completion for `barrier_async` and `broadcast`
* issue #243: Honor `$UPCXX_PYTHON` during install
* issue #260: `GASNET_CONFIGURE_ARGS` can break UPC++ build
* issue #264: `upcxx-meta CXX` and `CC` are not full-path expanded
* issue #268: Completion handlers can't accept `&&` references
* spec issue 141: resolve empty transfer ambiguities (count=0 RMA)
* spec issue 142: add `persona::active_with_caller()`

This library release mostly conforms to the
[UPC++ v1.0 Specification, Revision 2019.9.0](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-2019.9.0.pdf).
The following features from that specification are not yet implemented:

* view buffer lifetime extension for `remote_cx::as_rpc` (issue #262)
* User-defined Class Serialization interface (coming soon!)

Breaking changes:

* Applications are recommended to replace calls to `std::getenv` with `upcxx::getenv_console`,
  to maximize portability to loosely coupled distributed systems.
* envvar `UPCXX_GASNET_CONDUIT` has been renamed to `UPCXX_NETWORK`.
  For backward compatibility, the former is still accepted when the latter is unset.
* `upcxx::allocate()` and `device_allocator<Device>::allocate()` have changed signature.
  The `alignment` parameter has moved from being a final defaulted
  template argument to being a final defaulted function argument.
* `future<T...>::result_moved()` and `future<T...>::wait_moved()` are deprecated,
  and will be removed in a future release.

### 2019.05.27: Bug-fix release 2019.3.2

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #209: Broken install defaulting of CC/CXX on macOS

Embeds a GASNet-EX library that addresses the following notable issues
  (see the [GASNet issue tracker](https://gasnet-bugs.lbl.gov) for details):

* bug3943: infrequent startup hang with PSHM and over 62 PPN

### 2019.03.15: Release 2019.3.0

New features/enhancements: (see specification and programmer's guide for full details)

* Prototype Memory Kinds support for CUDA-based NVIDIA GPUs, see [INSTALL.md](INSTALL.md).
    Note the CUDA support in this UPC++ release is a proof-of-concept reference implementation
    which has not been tuned for performance. In particular, the current implementation of
    `upcxx::copy` does not utilize hardware offload and is expected to underperform 
    relative to solutions using RDMA, GPUDirect and similar technologies.
    Performance will improve in an upcoming release.
* Support for interoperability with Berkeley UPC, see [upc-hybrid.md](docs/upc-hybrid.md)
* There is now an offline installer package for UPC++, for systems lacking connectivity
* Barrier synchronization performance has been improved
* Installer now defaults to more build parallelism, improving efficiency (see `UPCXX_MAKE`)

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #100: Fix shared heap setting propagation on loosely-coupled clusters
* issue #118: Enforce GASNet-EX version interlock at compile time
* issue #177: Completion broken for non-fetching binary AMOs
* issue #183: `bench/{put_flood,nebr_exchange}` were failing to compile
* issue #185: Fix argument order for `dist_object` constructor to match spec
* issue #187: Improve Python detection logic for the install script
* issue #190: Teach upcxx-run to honor `UPCXX_PYTHON`
* issue #202: Make `global_ptr::operator bool` conversion explicit 
* issue #205: incorrect metadata handling in `~persona_scope()`

This library release mostly conforms to the
[UPC++ v1.0 Draft 10 Specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-V1.0-Draft10.pdf).
The following features from that specification are not yet implemented:

* `barrier_async()` and `broadcast()` only support default future-based completion (issue #234)
* `atomic_domain<float>` and `atomic_domain<double>` are not yet implemented (issue #235)
* `team_id::when_here()` is unimplemented (issue #170)
* User-defined Class Serialization interface 

Breaking changes:

* envvar `UPCXX_SEGMENT_MB` has been renamed to `UPCXX_SHARED_HEAP_SIZE`.
  For backward compatibility, the former is still accepted when the latter is unset.
* The minimum-supported version of GNU g++ is now 6.4.0
    - This also applies to the stdlibc++ used by Clang or Intel compilers
* The minimum-supported version of llvm/clang for Linux is now 4.0

### 2018.09.26: Release 2018.9.0

New features/enhancements: (see specification and programmer's guide for full details)

* Subset teams and team-aware APIs are added and implemented
* Non-Blocking Collective operations, with team support: barrier, broadcast, reduce
* New atomic operations: `mul, min, max, bit_{and,or,xor}`
* `future::{wait,result}*` return types are now "smarter", allowing more concise syntax
* New `upcxx` compiler wrapper makes it easier to build UPC++ programs
* `upcxx-run`: improved functionality and handling of -shared-heap arguments
* New supported platforms:
    - GNU g++ compiler on macOS (e.g., as installed by Homebrew or Fink)
    - PrgEnv-intel version 17.0.2 or later on Cray XC x86-64 systems
    - Intel C++ version 17.0.2 or later on x86-64/Linux
    - GNU g++ compiler on ppc64le/Linux
* `rput_{strided,(ir)regular}` now provide asynchronous source completion
* Performance improvements to futures, promises and LPCs
* UPC++ library now contains ident strings that can be used to query version info
  from a compiled executable, using the UNIX `ident` tool.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

* issue #49: stability and portability issues caused by C++ `thread_local`
* issue #141: missing promise move assignment operator

This library release mostly conforms to the
[UPC++ v1.0 Draft 8 Specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-V1.0-Draft8.pdf).
The following features from that specification are not yet implemented:

* `barrier_async()` and `broadcast()` only support default future-based completion (issue #234)
* `atomic_domain<float>` and `atomic_domain<double>` are not yet implemented (issue #235)
* `team_id::when_here()` is unimplemented (issue #170)
* User-defined Serialization interface

Breaking changes:

* `global_ptr<T>(T*)` "up-cast" constructor has been replaced with `to_global_ptr<T>(T*)`
* `atomic_domain` now requires a call to new collective `destroy()` before destructor
* `allreduce` has been renamed to `reduce_all`

### 2018.05.10: Release 2018.3.2

This is a re-release of version 2018.3.0 (see below) that corrects a packaging error.

### 2018.03.31: Release 2018.3.0

New features/enhancements:

 * Non-Contiguous One-Sided RMA interface is now fully implemented.
 * Remote Atomics have been revamped, expanded and implemented. See the updated specification
   for usage details.  The current implementation leverages hardware support in
   shared memory and NIC offload support in Cray Aries.
 * View-Based Serialization - see the specification for details
 * Implementation of local memory translation (achieved with
   `global_ptr::local()` / `global_ptr(T*)`). This encompasses a limited
   implementation of teams to support `upcxx::world` and `upcxx::local_team`
   so clients may query their local neighborhood of ranks.

Notable issues resolved
  (see the [UPC++ issue tracker](https://upcxx-bugs.lbl.gov) for details):

 * issue #119: Build system is now more robust to GASNet-EX download failures.
 * issue #125: Fix upcxx-run exit code handling.
 * Minor improvements to upcxx-run and run-tests.

This library release mostly conforms to the
[UPC++ v1.0 Draft 6 Specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-V1.0-Draft6.pdf).
The following features from that specification are not yet implemented:

 * Teams: `team::split`, `team_id`, collectives over teams, passing
       `team&` arguments to RPCs, constructing `dist_object` over teams.
 * Vector broadcast `broadcast(T *buf, size_t count, ...)`
 * `barrier_async`
 * User-defined Serialization interface

This release is not yet performant, and may be unstable or buggy.

Please report any problems in the [issue tracker](https://upcxx-bugs.lbl.gov).

### 2018.01.31: Release 2018.1.0 BETA

This is a BETA preview release of UPC++ v1.0. 

New features/enhancements:

 * Generalized completion. This allows the application to be notified about the
   status of UPC\+\+ operations in a handful of ways. For each event, the user
   is free to choose among: futures, promises, callbacks, delivery of remote
   procedure calls, and in some cases even blocking until the event has occurred.
 * Internal use of lock-free data structures for `lpc` queues.
 * Improvements to the `upcxx-run` command.
 * Improvements to internal assertion checking and diagnostics.
  
This library release mostly conforms to the
[UPC++ v1.0 Draft 5 Specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-V1.0-Draft5.pdf).
The following features from that specification are not yet implemented:

 * Teams
 * Vector broadcast `broadcast(T *buf, size_t count, ...)`
 * `barrier_async`
 * Serialization
 * Non-contiguous transfers
 * Atomics

This release is not performant, and may be unstable or buggy.

### 2017.09.30: Release 2017.9.0

The initial public release of UPC++ v1.0. 

This library release mostly conforms to the
[UPC++ v1.0 Draft 4 Specification](https://bitbucket.org/berkeleylab/upcxx/downloads/upcxx-spec-V1.0-Draft4.pdf).
The following features from that specification are not yet implemented:

 * Continuation-based and Promise-based completion (use future completion for
   now)
 * `rput_then_rpc`
 * Teams
 * Vector broadcast `broadcast(T *buf, size_t count, ...)`
 * `barrier_async`
 * Serialization
 * Non-contiguous transfers

This release is not performant, and may be unstable or buggy.

### 2017.09.01: Release v1.0-pre

This is a pre-release of v1.0. This pre-release supports most of the functionality
covered in the UPC++ specification, except personas, promise-based completion,
teams, serialization, and non-contiguous transfers. This pre-release is not
performant, and may be unstable or buggy. Please notify us of issues by sending
email to `upcxx@googlegroups.com`.

