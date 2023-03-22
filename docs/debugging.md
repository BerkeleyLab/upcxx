# Debugging

## General recommendations for debugging UPC++ programs

1. Whenever debugging your UPC++ program, **ALWAYS** build in debug mode, 
i.e. compile with `export UPCXX_CODEMODE=debug` (or equivalently, `upcxx -g`).  This enables thousands of
sanity checks system-wide that can greatly accelerate narrowing down the
problem. Just remember to switch back to production mode `UPCXX_CODEMODE=opt` (aka `upcxx -O`)
for building performance tests!

2. If your problem is simple enough that a crash stack might help to solve it, 
set `export GASNET_BACKTRACE=1` at run-time (or equivalently, `upcxx-run -backtrace`) and you will get a backtrace from
any rank crash.

    If the problem is a hang (instead of a crash), you can generate an on-demand
    backtrace by setting `export GASNET_BACKTRACE_SIGNAL=USR1` at runtime,
    wait for the hang, and then send that signal to the rank processes you 
    wish to backtrace, i.e.: `kill -USR1 <pid>`. Note this signal needs to be
    sent to the actual worker process, which may be running remotely on some systems.

3. Otherwise, if the problem occurs with a single rank, you can spawn
smp-conduit jobs in a serial debugger just like any other process.  
E.g., build with `UPCXX_NETWORK=smp` and then start your debugger
with a command like: `env GASNET_PSHM_NODES=1 gdb yourprogram`.

4. Otherwise, if you need multiple ranks and/or a distributed backend, we
recommend setting one or more of the following variables at runtime, and then
following the on-screen instructions to attach a debugger to the failing rank
process(es) and then resume them (by changing a process variable):


    * `GASNET_FREEZE`: set to 1 to make UPC++ always pause and wait for a debugger to attach on startup

    * `GASNET_FREEZE_ON_ERROR`: set to 1 to make UPC++ pause and wait for a
       debugger to attach on any fatal errors or fatal signals

    * `GASNET_FREEZE_SIGNAL`: set to a signal name (e.g. "SIGINT" or "SIGUSR1")
      to specify a signal that will cause the process to freeze and await debugger
      attach.

Note in particular that runs of multi-rank jobs on many systems include
non-trivial spawning activities (e.g., required spawning scripts and/or `fork`
calls) that serial debuggers generally won't correctly follow and handle. Hence
the general recommendation to debug multi-rank jobs by attaching your favorite
debugger to already-running rank processes.

### Segment verification errors

Setting breakpoints may modify program code segments, breaking code segment
symmetry between processes and potentially interfering with the Cross Code
Segment (CCS) verification process. If this occurs, it will result in segment
verification errors. The solution to this problem is to link with
`-Wl,--build-id`, which embeds a unique identifier in executables/libraries at
link time. CCS will use this identifier in place of hashing the code segment
at run time. To allow breakpoints to work with CCS, this link flag should be
enabled for the main executable and any dynamic libraries called directly via
RPC (those requiring CCS). Dynamic libraries only invoked indirectly by RPC,
such as those wrapped in a lambda or called by a function linked in the main
program, do not require CCS or this link flag to debug. macOS enables
`-Wl,--build-id` by default.

## Using Valgrind with UPC++

UPC++ has some *limited* support for interoperating with the 
[Valgrind instrumentation framework](https://valgrind.org/). 
However it's important to understand the fundamental limitations of Valgrind
for analyzing multi-process/distributed applications, limitations which have
nothing to do with UPC++:

1. First and foremost, **valgrind is purely a single-process tool**. It
   effectively has **no** support for coherently debugging
   multi-process/multi-node parallel jobs. It's possible to run Valgrind
   concurrently on all the processes of a parallel job, but those Valgrind
   instances do not communicate or coordinate with each other.

2. Second (and as a caveat of 1), valgrind often gets confused by operations
   taking place inside the network layer for multi-process jobs. This has the
   potential to generate lots of spam about unrecognized `ioctl()`s and/or
   warnings about other system calls it doesn't comprehend.

3. Even if you ignore warnings from the network layer, **valgrind usually has
   no way to track cross-process RMA accesses to objects in the shared heap**;
   such accesses are routinely implemented via shared-memory bypass between
   `local_team` processes, or by the NIC using RDMA on behalf of other
   processes. Consequently Valgrind will miss all such accesses, degrading
   accuracy.

4. Finally, Valgrind has no understanding of object boundaries in the global
   address space. As such, it's **incapable of detecting memory errors (buffer
   overruns, use-after-free, memory leaks, etc) for any object in the shared
   heap**. Similarly, valgrind has no visibility into GPU memory at all.

With all those caveats Valgrind still has some limited utility for UPC++
programs, primarily for detecting programming errors involving the **private**
heap. The best way to use Valgrind is to configure UPC++ with
`--enable-valgrind`; this activates some compatibility tweaks in UPC++/GASNet,
at some performance cost. 

Once you've done that, you'll need to invoke the `valgrind` wrapper command
*inside* the `upcxx-run` command (otherwise you're running valgrind on the
spawner).  So the general format is:

```bash
upcxx-run [upcxx-run args...] valgrind [valgrind args...] your-program [program args...]`
```

It's recommended to initially try reproducing your problem with
a single process, since that's where Valgrind works best. 

UPC++ and GASNet both supply optional Valgrind suppression files that suppress
some known-benign valgrind messages. These files are installed in the following locations:

* `$prefix/lib/valgrind/upcxx.supp`
* `$prefix/gasnet.debug/lib/valgrind/gasnet.supp`

Finally, valgrind doesn't handle process/thread contention very efficiently,
so regardless of system core count it's recommended to enable UPC++'s 
[oversubscription support](oversubscription.md) to help reduce running time.

Here's a complete example:

```bash
env UPCXX_OVERSUBSCRIBED=1 upcxx-run -vv -np 1 \
    valgrind --leak-check=full --suppressions=<path>/gasnet.supp --suppressions=<path>/upcxx.supp \
    ./a.out -myarg
```


