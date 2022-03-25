# CCS Tests

There are four main components to the CCS tests:

* `test.cxx`: Contains the function `upcxx_test()` containing RPC calls testing
  CCS functionality.
    * RPC targets: `test_segment_function()`. Tests RPC within the same segment
      as the UPC++ program
* `dynamic.cxx`:  Contains `main()`. All it does is call `upcxx_test()`
* `dynamic-test-funcs.cxx`:  Gets compiled into a `.so` Contains the C++
  functions that need to be RPCed with CCS.
    * RPC targets: `dynamic_test_function()`. Tests an RPC into a dynamic
      library.
* `dlopen-test-funcs.cxx`: Like `dynamic-test-funcs`, but this library is
  intended to be `dlopen`ed 
    * RPC targets:
        * `"dlopen_function"`: Function with C linkage to be used with `dlopen`.
        * `"_Z19dlopen_cpp_functionv"`: Function with C++ linkage to be used with
          `dlopen`

## ccs-dynamic

`test.cxx`, `dynamic.cxx`, and `libupcxx.a` are linked together into a single
executable, testing the functionality of a traditional UPC++ program by RPCing
`test_segment_function()` and the CCS use cases of a compile-time dynamically
linked library and a runtime `dlopen`ed library. This tests the most standard
way of using UPC++ and CCS.

Segments:

1. `main()`, UPC++ library, RPC call tests, `test_segment_function()` RPC
   target 
2. `dynamic_test_function()` target 
3. `dlopen` test function targets

## ccs-inlib

Here, `dynamic.cxx` and `libupcxx.a` are linked together to form a `.so`. The
executable contains no UPC++ code.  This is like "ccs-dynamic" but as if
everything was done in dynamic libraries. This requires UPC++ to be built as
position independent code.  

Segments:

1. `main()` 
2. UPC++ library, RPC call tests, `test_segment_function()` RPC target 
3. `dynamic_test_function()` target
4. `dlopen` test function targets

## ccs-dynamic-dynupcxx

This is like "ccs-dynamic", however `libupcxx.a` is transformed into
`libupcxx.so` and is therefore linked as a separate segment. This requires
UPC++ to be built as position independent code.

Segments:

1. `main()`, RPC call tests, `test_segment_function()` RPC target 
2. UPC++ library
3. `dynamic_test_function()` target
4. `dlopen` test function targets

## ccs-python

Creates a compiled Python library using the Python C API using `test.cxx`.
`libupcxx.so` is linked in as a separate dynamic library. This requires UPC++
to be built as position independent code.

Segments:

1. python 
2. RPC call tests, `test_segment_function()` RPC target 
3. UPC++ library
4. `dynamic_test_function()` target
5. `dlopen` test function targets

## Manual: ccs-static

Uses a modified version of `test.cxx` that removes the RPC to
`dynamic_test_function`. `main()`, the UPC++ RPC calls, `libupcxx.a`, and
`libdl.a` are linked together with `-static` to build a static binary. Even as
a static binary, it is still able to use `dlopen` to test RPCs against those
functions.  The primary purpose of this test is to ensure that CCS does not
break with statically linked binaries, as even traditional UPC++ programs need
the segment mapping to work to find the primary segment. If `dl_iterate_phdr`
and its parsing were to not work, this would be a problem. This shows UPC++
with CCS support enabled doesn't break static linking.  However, static
binaries aren't well supported, so this test doesn't run as part of
`dev-check`.

Segments:

1. Static binary: `main()`, UPC++ library, RPC call tests,
   `test_segment_function()` RPC target
2. `dlopen` test function targets

## ccs-asymmetric

These test error handling if a library is asymmetrically loaded, which can
occur if different library versions are loaded by different ranks or if
`dlopen` is only used on some. The effect on the segment map is equivalent in
both cases: segments with hashes not found across all ranks. "ccs-asymmetric1"
attempts segment verification with `experimental::relocation::verify_all()`
causing the RPC attempt to throw a `segment_verification_error`.
"ccs-asymmetric2" uses `experimental::relocation::verify_segment()` which
should immediately trigger this exception.
