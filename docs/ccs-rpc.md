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
is in another segment, the segment hash is sent along side the offset to identify
the segment to offset against.

## Cache

The meat of the segment mapping and caching takes place in the `segmap_cache`
class.  The main components of this class are the level 1 cache, level 2 cache,
and detailed segment map used for debugging and building the caches.

The level 1 cache is intended to be used at thread level and is not thread safe
to avoid the overhead of locking and atomic operations. An instance of
`segmap_cache` is created in the `persona_tls` for this purpose.  Due to the
restrictions on constructors and destructors of thread local storage, the level
1 cache uses `std::array`s for storage. Two sorted arrays are used to enable
binary searches of the level 1 cache in each direction. The capacity of this
storage can be set with `UPCXX_MAX_L1_DLCACHE_SIZE`, which defaults to 20.
This cache only contains segments that the program has actually used, so in
order to exceed this threshold, the program would have to not only use at least
20 libraries, but also make direct RPC calls to function pointers within them.
In practice, even if that many libraries were loaded, most would likely not
have function pointers invoked directly.  

The level 2 cache is held as a process-wide static structure and is only used
if the level 1 cache is full.  It contains all mapped segments. If there is a
miss at level 1 cache and the level 1 cache is not full, lookup goes directly
to the heavy-weight segment map that can be used to add the segment to the
level 1 cache. While it would be possible to promote a level 2 segment to level
2, rebuilding the level 2 cache is not thread safe and requires the user to
call `rebuild_cache()`. The segment map, on the other hand, uses locks and can
be rebuilt automatically if a library was loaded. Because hitting the uncached
segment map would only happen once per thread per segment, there is negligible
benefit to maintaining an additional cache promotion mechanism.

It is assumed that libraries are not unloaded, or at least not unloaded and the
address space reused by another library.  The level 1 caches are not purged if
a library is unloaded as `rebuild_cache()` only affects the level 2 cache and
segment map and is unable to touch every thread's thread-local storage. In
practice, `dlclose` usually doesn't actually unmap the library and this
shouldn't be a limitation with any practical effect.

## CCS Verification

When CCS is enabled, UPC++ can use program segment mapping information to
detect UPC++ RPC function pointer relocation errors, such as invoking functions
outside the primary segment in single segment mode or asymmetry in loaded
libraries across processes.

CCS verification is automatically enabled in debug mode and can be controlled
by the `upcxx::experimental::relocation::enforce_verification(bool)` function.
CCS verification detects asymmetry in loaded libraries, such as if different
processes loaded different versions of a library or if a library uses writable
executable segments or TEXTRELs.  It causes these errors to be detected by the
sender rather than the receiver for easier debugging.  These sources of
asymmetry may result in different hashes of the executable segments and/or
different function offsets within the library, both of which prevent UPC++ from
properly relocating function pointers. The library can be rebuilt with
`-Wl,--build-id` to provide a consistent hash.  Otherwise, UPC++ will fall back
to trying to use the hash of the library's file path as an identifier.  Some
systems can report inconsistent file paths for a library, in which case this
will fail.

Duplicate code segments are also a problem for UPC++ acquiring unique hashes.
This is a known occurrance with small libraries that return different
constants.  Because the constants are located in a different code segment, the
executable segment can be identical if the number of functions is the same.
`-Wl,--build-id` can be used to provide UPC++ with unique hashes.

MacOS automatically builds all libraries with the equivalent of
`-Wl,--build-id`.

Disabling CCS verification may be necessary for advanced use cases such as
intentional asymmetry and heterogeneity.

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

#### `void rebuild_cache()`

Rebuilds the process's segment map and populates the level 2 cache.  Should be
called after `dlopen`s to avoid expensive misses if the level 1 cache misses.

#### `void verify_segment(R(*ptr)(Args...), entry_barrier eb = entry_barrier::user)`

World collective function. Checks the segment is not a bad segment (RWX segment
or containing TEXTRELs with an unknown file path). Runs a reduction on the
segment hash to verify all processes have the same hash for the segment.  `ptr`
must be a pointer to the same function on all processes.  Raises an error on
failure.  Allows outgoing RPC verification.

#### `void verify_all(entry_barrier eb = entry_barrier::user)`

World collective function. All processes have their segment maps compared
against rank 0 for verification. Marks segments as verified if they are
symmetric among all processes but does not raise an error if there are
failures.  Segments invalid for UPC++ RPCs can still be used indirectly within
functions in valid segments.  Called automatically by `upcxx::init()`.  This
function should be called after `dlopen` if UPC++ intends to RPC the functions
contained withinthis library.

#### `bool enforce_verification(bool)` 

Not threadsafe. If set to true causes an error to be raised if attempting to
tokenize a function pointer in an unverified segment.  Returns the previous
verification state.

#### `bool verification_enforced()`

Not threadsafe. Returns `true` if segment verification is enabled.

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

## Environment Variables

* `UPCXX_COLORIZE_DEBUG`: Controls colorization of segment table printing.
  "yes" or "true" forces color on, "no" or "false" forces color off, and if
  unset `isatty` is used automatically color output if the output is a
  terminal.
  
## Potential Improvements

* Map the main executable file into memory and use `.symtab`/`.strtab` rather
  than `.dynsym`/`.dynstr`. This would enable symbol lookup for executables not
  linked with `-rdynamic`. Possibly build with `-rdynamic` by default in debug
  mode.

* `cache_segment(R(*ptr)(Args...))`: Manually promote segment into level 1
  cache.  Although caching happens automatically, this might be nice for
  sensitive benchmarks to pre-premote the segment, caching would be triggered
  by warmup runs, too.

* Add a `par_recursive_mutex` to optimize `CODEMODE=seq`
