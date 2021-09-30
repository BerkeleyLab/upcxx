# UPC++ Benchmarks

This directory contains our performance benchmarks. Each should be a `.cpp` file
containing its `main()` function. It is recommended that a benchmark include all
of the following headers in `./common/`:

  * `./common/timer.hpp`: A timer class `bench::timer` used like a 
    stopwatch to capture time intervals.
  
  * `./common/report.hpp`: A class `bench::report` for appending measurements to
    a report file in a format easily consumed by the `util/show.py` script. The
    target report file path and external measurement parameters are passed
    through the following environment variables:\
      
      - `report_file`: Path to file to create or append with report data.
        A value of "-" (single hyphen) indicates stdout. Defaults to `report.out`.
      
      - `report_args`: Python keyword argument assignments qualifying this
        running instance of the benchmark (e.g. `mood="happy", ranks=100` or
        `mood="sad", ranks=1`, take care to comma separate assignments and
        enclose strings in quotes). These should reflect properties of the
        execution environment beyond the control of the benchmark. For instance,
        if you're studying the performance of the benchmark under different
        compilers, you might run:
        
          `report_args="comp='gcc-O2'" ./my-benchmark-as-built-by-gcc-O2`
          `report_args="comp='gcc-O3'" ./my-benchmark-as-built-by-gcc-O3`
          `report_args="comp='clang-O3'" ./my-benchmark-as-built-by-clang-O3`
        
        Then the data points dumped to `report.out` will be qualified with a
        dimension `comp` taking 3 different string values.
        

  * `./common/operator_new.hpp`: Provides global replacements for
    `operator new/delete` parameterized by a compile time constant. All benchmarks
    are encouraged to include this so that experiments can be performed using
    different allocators unobtrusively w.r.t. the benchmark code. This will
    affect every allocation made using C++'s `new/delete` expressions across
    the executable, including those in libupcxx (note: some libupcxx allocations
    are performed with posix_memalign, so those are not affected) (second note:
    libgasnet is a C library and will not be affected). The supported allocators
    are:
      
      - `std`: The standard provided implementation (usually malloc/free).
      
      - `ltalloc`: [ltalloc](https://github.com/r-lyeh-archived/ltalloc) excels
        at lots of small and regular objects in a multi-threaded environment.
        I can't imagine us hand-rolling something better than this, except we
        could potentially take advantage of the following:
          
          1. Known object size at delete time. Thus hardcoding the query for
             "which bin to I put this free block in?".
          
          2. Inlining of both allocation and deallocation since C++ forbids
             that replacements of `operator new/delete` be marked `inline`.
             LTO should remedy this though.
      
      - `insane`: Hand-rolled. It only works if the program never asks for
        object sizes greater than 8K and never enters the allocator concurrently.
        If these conditions are met, it should be the fastest thing possible
        for our runtime's small objects, and thus is a useful means to generating
        speed-of-light bounds to determine if hand-rolling a more robust allocator
        would be worth the effort.
    
    This header is affected by the following preprocessor defines coming from
    the compiler invocation command:
    
      - `OPNEW={0|1|2}`, the numeric values correspond to the choice of C++
        allocator:
        
          0. `std`.
          
          1. `ltalloc`: note that `ltalloc.{h,cc}` which are not provided must
             already present in `./common/` (see "Build & Run" below).
             
          2. `insane`.

      - `OPNEW_INLINE={0|1}` which determines if the allocator's source code
        is included within the `./common/operator_new.hpp` header. By enabling
        this, the translation unit which includes this header will have all of
        its `new/delete's` as candidates for inlining. The purpose for this
        mode is in testing if writing our own inlineable allocator (which
        `new/delete` are not) would be worth it. Any such benchmark should take
        care to reside in a single "main" translation unit. Note that it is
        illegal to enable this mode and include `./common/operator_new.hpp` from
        multiple translation units as that would confuse the linker with
        duplicate symbol definitions. This option is essentially a no-op for the
        `std` allocator.


## Build & Run: From UPC++ installation

Can be built like any other upcxx application, but if you want to play with
the allocator you'll need the appropriate preprocessor defines present. For
instance:

```bash

# Build. Default allocator = std.
<upcxx-install>/bin/upcxx my-bench.cpp -o my-bench

# Build. Selecting for allocator = insane.
<upcxx-install>/bin/upcxx -DOPNEW=2 my-bench.cpp -o my-bench-with-insane

# Run. Since ${report_file} is unset in environment, runtime measurements
# will be appended to "report.out".
<upcxx-install>/bin/upcxx-run -n <run-ranks> my-bench-with-insane
```

If you want to use ltalloc (`-DOPNEW=1`), you will need to download `ltalloc.h`
and `ltalloc.cc` and place them into `./common/ltalloc.{h,cc}`. At this time
they can be found at the following repository
[ltalloc](https://github.com/r-lyeh-archived/ltalloc), or with these direct
links: 

  [ltalloc.h](https://raw.githubusercontent.com/r-lyeh-archived/ltalloc/master/ltalloc.h)

  [ltalloc.cc](https://raw.githubusercontent.com/r-lyeh-archived/ltalloc/master/ltalloc.cc)



# Report File Analysis

See `./util/show.py -h` for info on how to plot reports.
