# UPC++ Interoperability with Berkeley UPC #

## WARNING: Integration with Berkeley UPC is now deprecated and may be removed in a future release

UPC++ now has experimental support for interoperability with the 
[Berkeley UPC Runtime](https://upc.lbl.gov) (a.k.a "UPCR"), 
using any of the four UPC translators targeting that runtime.
This makes it possible to run hybrid applications that use both UPC and UPC++
(in separate object files, due to the difference in base languages).

## UPC++ / Berkeley UPC Runtime Hybrid Usage Basics

The UPC and UPC++ layers can be initialized in either order - `upcxx::init()` 
will detect if UPC has been linked in and initialize UPC if necessary.
[test/interop/main_upc.upc](../test/interop/main_upc.upc) and 
[test/interop/main_upcxx.cpp](../test/interop/main_upcxx.cpp) provide simple
interoperability examples, where `main()` is in UPC or UPC++, respectively.

Both layers may be active simultaneously, and shared objects from either layer are also 
valid shared objects in the other layer - however there are some important caveats. 
In particular, the `upcxx::global_ptr` and UPC pointer-to-shared
representations are NOT interchangeable. Passing of shared objects across layers should be
accomplished by "down-casting" to a raw C pointer (i.e., `void *`) on a process with affinity
to the shared object (eg in UPC this is done using a `(void*)` cast, in UPC++ use `global_ptr<T>::local()`).
The raw pointer can then be passed across layers, and "up-cast" using the
appropriate function (i.e. `upcxx::try_global_ptr()` or `bupc_inverse_cast()`).
See the documentation for each model for details on up-casting/down-casting.
Finally, note that shared objects dynamically allocated by one layer may only be 
freed using the appropriate routine in that same layer.
[test/interop/arrval_upc.upc](../test/interop/arrval_upc.upc) and 
[test/interop/arrval_upcxx.cpp](../test/interop/arrval_upcxx.cpp) provide examples of 
passing shared objects across layers.

Note that UPC communication operations will NOT advance the user-level UPC++ progress engine,
so for example processes stalled inside a `upc_barrier` or other UPC collective operations 
will NOT execute UPC++ RPCs, which could lead to deadlock if a remote process
is waiting for an RPC acknowledgment before entering the matching UPC collective call.
Conversely, UPC++ internal progress IS sufficient to service remotely initiated
UPC operations (i.e. `upcxx::progress()`, `upcxx::progress(upcxx::progress_level::internal)`,
and any UPC++ routines specified as "progress level: internal" all ensure
UPC-side progress equivalent to `bupc_poll()`).

UPC atomic memory operations are not guaranteed to be coherent with UPC++ atomic memory operations,
as there is currently no way to express a single atomic domain shared by both layers.

## UPC++ / Berkeley UPC Runtime Hybrid Build Rules

* UPC++ must be version 2018.9.5 or newer (`UPCXX_VERSION=20180905`)
* UPCR must be version 2018.5.3 or newer 
  (visible via `__BERKELEY_UPC{,_MINOR,PATCHLEVEL}__` or `UPCR_RUNTIME_SPEC_{MAJOR,MINOR}=3,13`)
* Both packages must be configured with the same release version of GASNet-EX
  (see the [GASNet-EX version table](http://upcxx.lbl.gov/wiki/GASNet-EX%20Version%20Table)),
  and compatible settings for any non-default GASNet configure options.
* The C++ compiler used for UPC++ must be ABI compatible with the backend C compiler configured for UPCR.
* All object files linked into one executable must agree upon GASNet conduit, debug mode and thread-safety setting.
* If `UPCXX_THREADMODE=par`, then must pass `upcc -uses-threads`.
  This in turn may require UPCR's `configure --enable-uses-threads`.
* If using ibv-conduit (InfiniBand systems) one must pass `--disable-ibv-rcv-thread` 
  (or optionally `--enable-ibv-rcv-thread`) to both configures.
* The link command should use the UPCR link wrapper, and specify `upcc -link-with='upcxx <args>'`.
* If the `main()` function appears outside UPC code, the link command should include `upcc -extern-main`.
* Additional restrictions apply to `upcc -pthreads` mode, see "UPC++ with Berkeley UPC -pthreads mode" below.

[test/interop/Makefile](../test/interop/Makefile) provides examples of this process in action.

## Running UPC++ / Berkeley UPC Runtime Hybrid programs

Resulting executables can be run using either `upcrun` or `upcxx-run` (or in many cases, 
the normal system mpirun equivalent), the job layout options are very similar. However `upcrun`
is recommended for most users because the UPC Runtime controls the overall shared heap sizing
(which most users will want to tweak) and `upcxx-run` does not know how control the UPC heap size.
For obvious reasons, the model-specific scripts only have command-line options for altering 
model-specific behaviors of their own model (implemented by setting environment variables). 
If one needs non-default runtime behaviors from both models, then the recommended mechanism is to 
manually set the appropriate environment variables. Both `upcrun` and `upcxx-run` scripts have `-v` 
options that output the environment variables set to effect a given set of command-line options.

Note that special care must usually be given to the shared heap settings.

For the default `UPCXX_USE_UPC_ALLOC=yes` mode: (recommended)

  In this mode, UPC++ uses the UPCR non-collective shared heap allocator directly to service all 
  UPC++ shared allocations. In this mode, UPC++ shared heap controls are disabled and the size of the
  shared heap (shared by both models) is controlled by Berkeley UPC Runtime.
  See documentation for `upcrun -shared-heap` and `UPC_SHARED_HEAP_SIZE` for details on controlling size.
  *Note that UPC++ shared heap allocation failures (i.e. out of memory) are always immediately fatal in this mode,
  even in cases where the specification or documentation state an exception should be thrown for that case.*

For `UPCXX_USE_UPC_ALLOC=no` mode:

  In this mode, the UPC++ shared heap is created inside `upcxx::init()` by allocating one large block
  from the Berkeley UPC Runtime allocator. By default this block is allocated from the UPCR
  non-collective shared heap, but `UPCXX_UPC_HEAP_COLL=yes` changes this to use the UPCR collective shared heap.
  In both cases, there must be sufficient (non-fragmented) free space in the selected UPCR heap to
  accommodate the UPC++ shared heap during the call to `upcxx::init()`.
  In this mode, the UPC and UPC++ shared heap sizes are controlled independently by the appropriate
  spawner args or envvar settings - the UPC shared heap size must be set large enough to allow space
  for the UPC++ shared heap block creation. Note that UPCR reserves guard pages at either end of the 
  UPC shared heap and statically-allocated shared UPC objects also consume space in the UPC shared heap,
  so one should generally allow some padding in addition to anticipated shared heap consumption from 
  dynamically allocated UPC shared objects.
  For more details, see [UPCR memory management](https://upc.lbl.gov/docs/system/runtime_notes/memory_mgmt.shtml)

## UPC++ with Berkeley UPC -pthreads mode

Starting in UPC++ version 2019.3.5, hybrid applications may now be linked with
UPC programs compiled using the Berkeley UPC using the `upcc -pthreads` mode.

It's important to understand that UPC++ uses process-based rank numbering, but
in Berkeley UPC -pthreads mode there may be multiple UPC ranks per process.
Consequently in this configuration there is generally a 1-to-many mapping
between UPC++ ranks and UPC ranks.  The application is responsible for managing
any consequences of this difference. One example is the affinity of objects in
the shared heap is reported in the model-specific rank id, thus the affinity of
a given shared object will be reported differently by each model.

The following additional restrictions apply to hybrid use of UPC -pthreads mode with UPC++:

* UPC++ cannot be relied upon to implicitly init UPC in -pthreads mode.
  UPC must be initialized before UPC++, either by placing main() in UPC code,
  or by linking with `upcc -extern-main` and invoking `bupc_init_reentrant()`.
  For more details, see the [UPC Runtime specification](https://upc.lbl.gov/docs/system/).
* upcxx::init() must be called by exactly one thread in each process.
  Because UPC must be initialized first, this means the app must elect one UPC thread
  per process to make this call, and that thread inherits the UPC++ master persona.  
  Many UPC++ calls (notably including all collective calls) must be invoked
  by exactly one thread per node while holding the master persona.
* Only the default `UPCXX_USE_UPC_ALLOC=yes` mode is supported.
* UPC++ must use the thread-safe backend (`UPCXX_THREADMODE=par`)
* Calls to UPC++ shared storage management (allocation/deallocation) or any
  UPC++ function with progress level internal or user may only be issued from
  threads corresponding to a UPC rank pthread. Invoking such UPC++ functions
  from other threads (eg those created manually or by OpenMP) has undefined
  behavior. Similarly, invoking any UPC code or library functions from non-UPC
  threads also has undefined behavior.
* Shared heap objects allocated using UPC++ are placed in the UPC shared heap
  with affinity to the calling UPC rank thread.
* Global variables defined in UPC code do not have cross-language linkage in -pthreads mode,
  and thus cannot be directly referenced by name from UPC++ code (regardless of declaration).
  However they can still be accessed by pointer (or by calling UPC code to operate upon them).
  For details, see [https://upc.lbl.gov/docs/user/interoperability.shtml#pthreads]

