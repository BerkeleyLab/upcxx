# UPC++ Implementation Defined Behavior #

This document describes stable features supported by this implementation that
go beyond the requirements of the UPC++ Specification, or are specified
as implementation-defined behavior.

## Version Identification and Compilation Settings ##

The following macro definitions are provided by `upcxx/upcxx.hpp`:

  * `UPCXX_VERSION`:
    An integer literal providing the release version of the implementation, 
    in the format [YYYY][MM][PP] corresponding to release YYYY.MM.PP
  * `UPCXX_SPEC_VERSION`:
    An integer literal providing the revision of the UPC++ specification
    to which this implementation adheres. See the specification for the specified value.
  * `UPCXX_KIND_CUDA`:
    An integer literal providing the version number of the CUDA memory-kind
    feature to which this implementation adheres, defined only when the library
    is built with CUDA enabled. See the UPC++ specification for the specified
    value.
  * `UPCXX_KIND_HIP`:
    An integer literal providing the version number of the ROCm/HIP memory-kind
    feature to which this implementation adheres, defined only when the library
    is built with ROCm/HIP enabled. See the UPC++ specification for the specified
    value.
  * `UPCXX_KIND_ZE`:
    An integer literal providing the version number of the oneAPI Level Zero
    (ZE) memory-kind feature to which this implementation adheres, defined only
    when the library is built with ZE support enabled. See the UPC++
    specification for the specified value.


  * `UPCXX_THREADMODE`:
    This is either undefined (for the default "seq" threadmode) or defined to
    an unspecified non-zero integer value for the "par" threadmode.
    Recommended usage is `#if UPCXX_THREADMODE` to identify the need for
    thread-safety constructs, such as locks.
  * `UPCXX_CODEMODE`:
    This is either undefined (for the "debug" codemode) or defined to an
    unspecified non-zero integer value for the "opt" (production) codemode.
  * `UPCXX_NETWORK_*`:
    The network being targeted is indicated by a preprocessor identifier with a
    `UPCXX_NETWORK_` prefix followed by the network name in capitals, which is
    defined to a non-zero integer value.  Identifiers corresponding to other
    networks are undefined.  Examples include `UPCXX_NETWORK_IBV` and
    `UPCXX_NETWORK_ARIES`.

## Eagerness of Future and Promise Completions ##

Future and promise completions default to eager notification. Thus:

  * `source_cx::as_future()` and `operation_cx::as_future()` are equivalent to
    `source_cx::as_eager_future()` and `operation_cx::as_eager_future()`,
    respectively
  * `source_cx::as_promise(p)` and `operation_cx::as_promise(p)` are equivalent
    to `source_cx::as_eager_promise(p)` and `operation_cx::as_eager_promise(p)`,
    respectively

The default can be changed on a per-translation-unit basis by defining the
`UPCXX_DEFER_COMPLETION` macro prior to including `upcxx/upcxx.hpp`. Defining
the macro to a non-zero value makes the default deferred (so that `as_future()`
and `as_promise(p)` are equivalent to `as_defer_future()` and
`as_defer_promise(p)`, respectively), while defining it to 0 makes the default
eager.

## Exceptions thrown from RPC ##

The communication functions `upcxx::rpc` and `upcxx::rpc_ff` may throw
exceptions. The exceptions may be thrown on the initiating thread before or
after serialization of the function arguments. In all other ways, a call
throwing such an exception is effectively "canceled" -- it will not lead to
invocation of the function object at the target, nor will it deliver any event
notifications (for example, a promise passed using an `as_promise()` completion
will remain unchanged by the exceptional call).

Starting in 2021.9.0, resource exhaustion while trying to inject an RPC will
throw a `upcxx::bad_shared_alloc` exception. The `what()` member function
includes information about the shared heap state at the point of failure. In
the current release, this should only occur when the RPC payload is somewhat
large (over a few KiB) and the shared heap on the initiating process fails to
allocate a temporary buffer large enough to hold the serialized RPC. In
releases prior to 2021.9.0, such conditions led to an immediate fatal error.

Starting in 2022.3.0, attempting to make a cross-code segment (CCS) RPC call
into an unverified segment when CCS segment verification is enabled will throw
a `upcxx::segment_verification_error`.  CCS RPC calls are RPC calls which
directly invoke functions existing in other executable program segments, such
as dynamic libraries. The `what()` member function includes information about
the state of the function pointer relocation tables.  Catching this exception
may be used to arrange for later collective synchronization of cross-segment
function pointer relocation information using
`upcxx::experimental::relo::verify_all()` or
`upcxx::experimental::relo::verify_segment()` when libraries are `dlopen`ed
asynchronously.  See [docs/ccs-rpc.md](ccs-rpc.md) for more information
about the CCS RPC feature.

## Simplified Device Allocator Management

UPC++ specifies the type `upcxx::gpu_default_device` which is an implementation-defined
alias for a GPU device type. The binding of that alias is determined as follows:

1. For the common case where UPC++ is configured for exactly one GPU
   variety (e.g. `--enable-cuda` OR `--enable-hip` OR `--enable-ze`) then
   `upcxx::gpu_default_device` defaults to an alias for that corresponding device
   type (i.e. `upcxx::cuda_device`, `upcxx::hip_device`, `upcxx::ze_device`).

2. When no device support is configured, then `upcxx::gpu_default_device`
   defaults to an alias for `upcxx::cuda_device`.

3. User programs may override this default choice by defining one of the following 
   preprocessor macros to 1 before including upcxx.hpp (these may be set
   independently per translation unit):
    * `UPCXX_GPU_DEFAULT_DEVICE_CUDA=1` makes `gpu_default_device` an alias for `cuda_device`
    * `UPCXX_GPU_DEFAULT_DEVICE_HIP=1` makes `gpu_default_device` an alias for `hip_device`
    * `UPCXX_GPU_DEFAULT_DEVICE_ZE=1` makes `gpu_default_device` an alias for `ze_device`

4. For rare cases where UPC++ is configured to support two or more GPU varieties, then 
   `upcxx::gpu_default_device` will default to aliasing an unspecified device type.
   Users of such configurations are advised to define one of the two macros
   described above.

The resulting memory kind can be queried via the `gpu_default_device::kind` constant.
`upcxx::make_gpu_allocator()` defaults to returning a `device_allocator<gpu_default_device>`,
but this can also be overridden on a call-site basis via template argument.

The `upcxx::make_gpu_allocator<Device>(sz,device_id)` factory function defaults
to `device_id = auto_device_id` which activates an implementation-defined
"smart" choice of valid GPU device when a device ID was not explicitly provided
by the caller. That "smart" choice is determined as follows:

1. If `Device::device_n()` is zero, there are no valid GPUs at the calling
   process and the call to `upcxx::make_gpu_allocator(sz, auto_device_id)` will
   return an inactive `device_allocator` (one with no corresponding segment).

2. If `Device::device_n()` is one, there is a single valid GPU at the calling
   process and the call will attempt to construct a `device_allocator` segment
   for that GPU.

3. Otherwise, there are multiple valid GPUs at the calling process. In this
   case the "smart" choice will cycle through valid IDs with subsequent calls,
   with a starting point determined from the process rank in `local_team()`.
   The resulting device ID can be queried via `device_allocator::device_id()`.
   Programs wanting finer-grained control over device selection in multi-GPU
   environments may override this choice by explicitly passing the `device_id`
   argument to `upcxx::make_gpu_allocator(sz,device_id)`.

## Assertion Macros ##

This implementation provides assertion macros to facilitate debugging on
distributed systems. Unlike the standard `assert()` macro, the macros below
print a backtrace and/or freeze to allow a debugger to be attached before
aborting program execution.

  * `UPCXX_ASSERT_ALWAYS(test)`, `UPCXX_ASSERT_ALWAYS(test, message)`:
    Evaluates `test`, and if the result is a false value, outputs `message` if
    provided and diagnostic information to standard error, optionally prints a
    backtrace and/or freezes for debugger, and aborts execution by calling
    `std::abort()`. `message` may be any expression such that `std::cerr <<
    message` is well-formed; for instance, it may itself include
    stream-insertion operators (e.g. `UPCXX_ASSERT_ALWAYS(x > 5, "error! x = "
    << x)`). `message` is only evaluated when `test` produces a false value. If
    `message` is not provided, it defaults to a string that includes a textual
    representation of `test`. In all cases, this macro expands to an expression
    with type `void`.
  * `UPCXX_ASSERT(test)`, `UPCXX_ASSERT(test, message)`:
    In the "debug" codemode, provides the same behavior as
    `UPCXX_ASSERT_ALWAYS()`. In the "opt" codemode, this macro expands to a
    side-effect-free expression with type `void` that does not evaluate the
    arguments.

## Experimental Features ##

Several unspecified, experimental features are implemented in the
`upcxx::experimental` namespace. These include the following:

  * broadcast of Serializable but non-TriviallySerializable values:

    ```c++
    template<typename T, typename Cx=/*unspecified*/>
    RType broadcast_nontrivial(T &&value, intrank_t root, const team &team=world(),
                               Cx &&completions=operation_cx::as_future());
    ```

  * reduction of Serializable but non-TriviallySerializable values:

    ```c++
    constexpr /*unspecified*/ op_add;
    constexpr /*unspecified*/ op_mul;
    constexpr /*unspecified*/ op_min;
    constexpr /*unspecified*/ op_max;
    constexpr /*unspecified*/ op_bit_and;
    constexpr /*unspecified*/ op_bit_or;
    constexpr /*unspecified*/ op_bit_xor;

    template <typename T, typename BinaryOp , typename Cx=/*unspecified*/>
    RType reduce_one_nontrivial(T &&value, BinaryOp &&op, intrank_t root,
                                const team &team = world(),
                                Cx &&completions=operation_cx::as_future());
    template <typename T, typename BinaryOp , typename Cx=/*unspecified*/>
    RType reduce_all_nontrivial(T &&value, BinaryOp &&op, const team &team = world(),
                                Cx &&completions=operation_cx::as_future());
    ```

  * utilities for reading environment variables:

    ```c++
    template<class T>
    T os_env(const std::string &name);
    template<class T>
    T os_env(const std::string &name, const T &otherwise);
    std::int64_t os_env(const std::string &name, const std::int64_t &otherwise,
                        std::size_t mem_size_multiplier);
    ```

  * `ostream`-like class that prints to a stream with an optional prefix and as
    much atomicity as possible:

    ```c++
    class say {
    public:
      say(std::ostream &output, const char *prefix="[%d] ");
      say(const char *prefix="[%d] ");
      ~say();
      template<typename T>
      say& operator<<(T const &that);
    };
    ```

In addition, the implementation provides the following unspecified,
experimental macro:

  * variant of `upcxx_memberof` that can be used on a type `T` that is either
    standard-layout (in which case the equivalent, specified `upcxx_memberof`
    should be used instead), or for which the compiler conditionally supports
    `offsetof`:

    ```c++
    // Macro: function template syntax used for clarity
    template<typename T, memory_kind Kind>
    global_ptr<MType, Kind> upcxx_experimental_memberof_unsafe(
        global_ptr<T, Kind> ptr, member-designator MEMBER
    )
    ```

These features are subject to change or removal at any time. If you find any of
them useful, please send an email to `upcxx@googlegroups.com`, and we will
consider adding them to the specification proper.

## Unspecified Internals

Aside from `upcxx::experimental`, all other namespaces nested inside of `upcxx`
are intended solely for internal use by the implementation (e.g. `upcxx::backend`,
`upcxx::detail`). Similarly, all identifiers with the `UPCXXI` or `upcxxi`
prefix are intended solely for internal use by the implementation. 
The behavior and existence of all such interfaces and identifiers is subject
to change without notice, and as such their use in user code is STRONGLY discouraged.

The UPC++ v1.0 Specification is the canonical authoritative document that 
specifies all the required and guaranteed behaviors of the UPC++ interface.
Users are strongly advised to rely solely on features and behaviors specified
by that document, or implementation-defined behaviors outlined in the other
sections of this document.

## UPCXX_THREADMODE=seq Restrictions ##

The "seq" build of libupcxx is performance-optimized for single-threaded
processes, or for a model where only a single thread per process will ever be
invoking interprocess communication via upcxx. The performance gains with
respect to the "par" build stem from the removal of internal synchronization
(mutexes, atomic memory ops) within the upcxx runtime. Affected upcxx routines
will be observed to have lower overhead than their "par" counterparts.

Whereas "par-mode" libupcxx permits the full generality of the UPC++
specification with respect to multi-threading concerns, "seq" imposes these
additional restrictions on the client application:

  * Only the thread which invokes `upcxx::init()` may ever hold the master
    persona. This thread is regarded as the "primordial" thread.

  * Any upcxx routine with internal or user-progress (typically inter-process
    communication, e.g. `upcxx::rput/rget/rpc/...`) must be called from the
    primordial thread with the master persona at the top of the active persona
    stack. There are some routines which are excepted from this restriction and
    are listed below.

  * Shared-heap allocation/deallocation (e.g. `upcxx::allocate/deallocate/new_/
    new_array/delete_/delete_array`) must be called from the primordial thread
    while holding the master persona. The same applies to `device_allocator`
    functions that manipulate a device heap.

Note that these restrictions must be respected by all object files linked into
the final executable, as they are all sharing the same libupcxx.

Types of communication that do not experience restriction:

  * Sending LPCs via `upcxx::persona::lpc()` or `<completion>_cx::as_lpc()`
    has no added restriction.

  * `upcxx::progress()` and `upcxx::future::wait()` have no added restriction.
    Incoming RPCs are only processed if progress is called from the primordial
    thread while it has the master persona.

  * Upcasting/downcasting shared heap memory (e.g. `global_ptr::local()`) is
    always OK. This facilitates a kind of interprocess communication via native
    CPU shared memory access which is permitted in "seq". Note that
    `upcxx::rput/rget` is still invalid from non-primordial threads even when
    the remote memory is downcastable locally.

The legality of lpc and progress from the non-primordial thread permits users
to orchestrate their own "funneling" strategy, e.g.:

```c++

// How a non-primordial thread can tell the master persona to put an rpc on the
// wire on its behalf.
upcxx::master_persona().lpc_ff([=]() {
  upcxx::rpc_ff(99, [=]() { std::cout << "Initiated from far away."; });
});
```

## Job layouts and local_team ##

UPC++ specifies that processes who are members of `upcxx::local_team()` have
the ability to obtain valid "raw" C++ pointers (i.e. `T*`) referencing
shared objects allocated by team members (specifically, `global_ptr::is_local()`
is guaranteed to return true for such objects). In practice, this generally means
these processes must be co-located on the same compute node, defined as a
set of CPU resources sharing an OS image and coherent physical memory domain.

UPC++ computes `upcxx::local_team()` membership at startup by examining the
job layout of processes across physical nodes. By default, UPC++ attempts to
maximize the size of each local team to encompass all processes co-resident
on the same compute node (this strategy can be adjusted via GASNet environment
variables, but the default is strongly recommended). 

The algorithm used to construct `upcxx::local_team()` membership additionally
ensures the following invariant: 

  * **Processes within a single local team always have consecutive rank indexes in `upcxx::world()`**. 
  * More formally, for all `I` in `[0, local_team().rank_n() - 1)`, `local_team()[I+1] == local_team()[I] + 1`

This invariant is not currently required by the UPC++ specification, but it is 
maintained by all versions of the LBNL UPC++ v1.0 implementation.
