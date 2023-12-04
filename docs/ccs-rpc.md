# Cross Code Segment RPC Support

This document describes the cross code segment (CCS) feature supported by this
implementation.  This feature is necessary for RPC calls to functions outside
the executable code segment containing `libupcxx.a`. 

## Technical Background

The purpose of this technical background is to provide an explanation of what
this feature does and when it is necessary.  A user is not required to have a
complete understanding of the technical details of this feature.  When UPC++ is
built with CCS support available, debugging functionality is capable of telling
the user when this feature must be used.

When a program is executed, the program and its dynamically linked libraries
are loaded into memory.  In modern operating systems, the base addresses at
which these are loaded are often randomized due to a security feature called
Address Space Layout Randomization (ASLR).  Function pointers are then
relocated relative to the base address of their containing code segments.
Because these base addresses can be different for each process, function
pointers invoked via remote procedure calls must have their relocation undone to
determine their offset, then re-relocated on the receiving process against its
different random base address.  In a legacy UPC++ application, where all UPC++
user code and the UPC++ library are statically linked into a single executable
segment, this can be done by calculating offsets relative to a known function
pointer.

However, this strategy can be insufficient in the presence of dynamic
libraries, where multiple executable segments exist at random distances in
memory from each other.  A legacy UPC++ application can use dynamic library
calls within Callables, but the RPC entry point, the Callable itself, must live
within the primary UPC++ code segment:

```c++
//OK in legacy UPC++: containing lambda in main executable
upcxx::rpc(rank, []() { dynamic_library_function(); });

//CCS necessary: RPC entry point in dynamic library
upcxx::rpc(rank, dynamic_library_function);
```

Similar issues can arise when using a shared library written with UPC++ in a
UPC++ application, as UPC++ calls would exist in multiple executable segments.

For UPC++ to handle the relocation of function pointers used in RPC calls that
cross into dynamic libraries, cross code segment functionality must be enabled.
This feature allows UPC++ to navigate multiple code segment memory regions.
Instead of calculating offsets relative to a single basis pointer, identifying
hashes and address ranges of each of the code segments are used to identify the
correct basis address to offset against.  Because this more comprehensive
relocation mechanism is more expensive, it is designed to be optional and can
be enabled independently for individual translation units.

## Function Pointer Relocation Modes

There are some cases where load-time shared libraries can share randomized
addresses due to randomization happening before duplicating processes, such as
smp-conduit or if the system lacked ASLR entirely. Previously, this resulted in
cross-segment calls "magically" working, but was unspecified behavior.  This is
now prohibited.  Conforming UPC++ programs must make all cross-segment calls
using CCS Multi-Segment mode.

### Legacy RPC Relocation (`configure --disable-ccs-rpc`)

Active if CCS support is disabled.  Relocates pointers as an offset from a
basis address of a function pointer within `libupcxx`.  Executable segment
mapping is disabled.  The CCS API interprets this as a single code segment one
byte in size starting at the address of the internal sentinel function.  All
function pointer relocations are performed as an offset from this address.
Segment verification is not enabled and attempting an RPC that requires CCS may
result in a segmentation fault.

### CCS RPC Relocation (default, `configure --enable-ccs-rpc`)

If the pointer is within the primary code segment, this mode performs the
relocation by sending an offset to the target as in legacy mode. If the pointer
is in another segment, the mechanism for relocation is dependent upon the
verification state of the segment.  Unverified segments use an offset plus a
hash to look up the segment's basis address.  Sets of verified segments are
guaranteed to be identical on all ranks and so are assigned deterministic
indexes for unique identification.  This allows a verified segment to be
relocated using just the space of a single `uint64_t` without the need to send
the segment's hash. The most significant bit indicates a multi-segment
relocation, the next 15 bits the segment index, and the bottom bits are the
address offset from the basis pointer.

Verified segment number to offset conversion uses a fixed-size array with a
size defined by the `UPCXX_CCS_MAX_SEGMENTS` environment variable, defaulting
to 256 segments.

Each batch of `verify_all()` or `verify_segment()` newly verified segments are
sorted and appended to a vector of segments that can be identified by index.
Index zero indicates a segment that is relocated using a hash.

## Cache

Each UPC++ thread  maintains a cache of segments previously used by the CCS
mechanism for faster subsequent lookup. An instance of `segmap_cache` is
created in the `persona_tls` for this purpose. Due to the restrictions of
`__thread` storage, the cache uses `std::array`s with a capacity defined by
`UPCXXI_MAX_SEGCACHE_SIZE` of 20. This doesn't require a guard as a dynamically
sized cache would. It is unlikely that a program will exceed this cache
capacity, as the program would not only have to use at least 20 libraries but
also make direct RPC calls to function pointers within them.  If the cache
capacity is exceeded, an old entry is evicted.  

There are two caches for unverified segment relocation. One is sorted for fast
binary search lookup of function address to segment hash and basis pointer. The
other is sorted for fast binary search lookup of segment hash to basis address.
Entries are promoted and evicted from these caches together. A separate cache
is used for lookup of verified segment index and basis pointer from a function
address.  There is no need for a cache for the reverse direction.

It is assumed that libraries are not unloaded, or at least not unloaded and the
address space reused by another library.  In practice, `dlclose` usually
doesn't actually unmap the library and this shouldn't be a limitation with any
practical effect.

## CCS Verification

When CCS is enabled, UPC++ can use program segment mapping information to
detect UPC++ RPC function pointer relocation errors, such as invoking functions
outside the primary segment in single segment mode or asymmetry in loaded
libraries across processes.

CCS verification is automatically enabled on `init()` and enforcement of
verification for RPCs can be controlled by the
`upcxx::experimental::relocation::enforce_verification(bool)` function.  CCS
verification detects asymmetry in loaded libraries, such as if different
processes loaded different versions of a library or if a library uses writable
executable segments or TEXTRELs.  It causes these errors to be detected by the
sender rather than the receiver for easier debugging.  These sources of
asymmetry may result in different hashes of the executable segments and/or
different function offsets within the library, both of which prevent UPC++ from
properly relocating function pointers.  However, setting breakpoints may modify
code segments legitimately, resulting in asymmetry. The library can be rebuilt
with `-Wl,--build-id` to provide a consistent hash.  For segments with writable
code segments or TEXTRELs, UPC++ will fall back to trying to use the hash of
the library's file path as an identifier.  Some systems can report inconsistent
file paths for a library, in which case this will fail.

Duplicate code segments are also a problem for UPC++ acquiring unique hashes.
This is a known occurrence with small libraries that return different
constants.  Because the constants are located in a different code segment, the
executable segment can be identical if the number of functions is the same.
`-Wl,--build-id` can be used to provide UPC++ with unique hashes.

MacOS automatically builds all libraries with the equivalent of
`-Wl,--build-id`.

It is possible to catch `upcxx::segment_verification_error` exceptions to then
arrange for synchronizing and loading the necessary libraries before
reverifying and continuing.

Disabling CCS verification enforcement may be necessary for advanced use cases
such as intentional asymmetry and heterogeneity.

See [ccs-rpc-debugging.md](ccs-rpc-debugging.md) for practical examples of 
debugging CCS RPCs.

## Supported Configurations

As of UPC++ 2022.3.0, CCS support only extends to the relocation of function
pointers in other executable segments. The ability to compile and link UPC++
and its dependencies as dynamic libraries is not yet supported by the build
infrastructure. This means `libupcxx.a` must still be linked into the main
executable for a supported configuration. Configurations involving building
UPC++ as position independent code (`-fPIC`) for use in creating a dynamic
library (`libupcxx.so`), such as for use in Python libraries and UPC++ within
dynamic libraries, are not yet supported. 

## CCS API

### `upcxx` namespace

#### `class upcxx::segment_verification_error`

A `upcxx::segment_verification_error` exception is thrown when attempting to
invoke a function pointer in an unverified segment when verification
enforcement is enabled.

### `upcxx::experimental::relocation` namespace

This namespace has a shorthand name of `upcxx::experimental::relo`.

#### `void verify_segment(R(*ptr)(Args...))`

World collective function. Checks the segment is not a bad segment (RWX segment
or containing TEXTRELs with an unknown file path). Runs a reduction on the
segment hash to verify all processes have the same hash for the segment.  `ptr`
must be a pointer to the same function on all processes.  Raises an error on
failure.  Allows outgoing RPC verification. Allows for more compact function
pointer relocation.  

UPC++ progress level: `internal`

#### `void verify_all()`

World collective function. All processes have their segment maps compared
against rank 0 for verification. Marks segments as verified if they are
symmetric among all processes but does not raise an error if there are
failures.  Segments invalid for UPC++ RPCs can still be used indirectly within
functions in valid segments.  Called automatically by `upcxx::init()`.  This
function should be called after `dlopen` if UPC++ intends to RPC the functions
contained within this library. Allows for more compact function pointer
relocation.

UPC++ progress level: `internal`

#### `bool enforce_verification(bool)` 

Not thread-safe. State read on RPC injection and `debug_write_*()` calls. If set
to true causes an error to be raised if attempting to tokenize a function
pointer in an unverified segment.  Returns the previous verification state.

#### `bool verification_enforced()`

Not thread-safe. Returns `true` if segment verification is enabled.

#### `void debug_write_ptr(R(*ptr)(Args...), int fd = 2, int color = 2)`

Attempts a lookup of the supplied pointer. Prints out the relocation token,
symbol (if available), a table of the mapped segments, and if found indicates
in which segment of the table it was found. If colors are enabled, green
indicates the found segment, cyan verified segments, and red bad segments. The
color parameter can be set to 0 to disable color, 1 to force color, and 2
(default) to detect if the output is to a terminal and enable color if it is.
Prints to the `STDERR_FILENO` file descriptor by default.

#### `void debug_write_ptr(R(*ptr)(Args...), std::ostream& out, bool color = false)`

As above, but writes to a `std::ostream`.

#### `void debug_write_segment_table(int fd = 2, int color = 2)`

Prints just the segment table like above without attempting to look up a
pointer. Prints to the `STDERR_FILENO` file descriptor by default. See
`debug_write_ptr` for a description of the color parameter.

#### `void debug_write_segment_table(std::ostream& out, bool color = false)`

As above, but writes to a `std::ostream`.

#### `void debug_write_cache(int fd = 2)`

Dumps a table containing the state of the current thread's cache.

### `void debug_write_cache(std::ostream& out)`

As above, but writes to a `std::ostream`

## Environment Variables

* `UPCXX_COLORIZE_DEBUG`: Controls colorization of segment table printing.
  "yes" or "true" forces color on, "no" or "false" forces color off, and if
  unset `isatty` is used automatically color output if the output is a
  terminal.

* `UPCXX_CCS_MAX_SEGMENTS`: Controls the limit on verified executable segments.
  Default: initially loaded segment count + 256. The value of
  `UPCXX_CCS_MAX_SEGMENTS` must fall between the number of segments loaded at
  init time (plus some unspecified padding) and 32768. Values outside this
  range are silently raised or lowered to meet this requirement.
  
## Potential Improvements

Additional features which could be implemented if users express a desire for
them:

* Map the main executable file into memory and use `.symtab`/`.strtab` rather
  than `.dynsym`/`.dynstr`. This would enable symbol lookup for executables not
  linked with `-rdynamic`. Possibly build with `-rdynamic` by default in debug
  mode.

* `cache_segment(R(*ptr)(Args...))`: Manually promote segment into level 1
  cache.  Although caching happens automatically, this might be nice for
  sensitive benchmarks to pre-promote the segment, caching would be triggered
  by warm-up runs, too.

* Add a `par_recursive_mutex` to optimize `CODEMODE=seq`

* Asymmetric verification. Verify a single segment within a team and pass
  `nullptr` for non-member ranks (world collective). This would allow for
  appending segments to the indexed list for more space efficient multi-segment
  relocations. A check of if the correct team is used for RPC might not be
  implemented as that would require additional complexity.
