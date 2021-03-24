# UPC++ Implementation Defined Behavior #

This document describes stable features supported by this implementation that
go beyond the requirements of the UPC++ Specification.

## Version Identification and Compilation Settings ##

The following macro definitions are provided by `upcxx/upcxx.hpp`:

  * `UPCXX_VERSION`:
    An integer literal providing the release version of the implementation, 
    in the format [YYYY][MM][PP] corresponding to release YYYY.MM.PP
  * `UPCXX_SPEC_VERSION`:
    An integer literal providing the revision of the UPC++ specification
    to which this implementation adheres. See the specification for the specified value.

  * `UPCXX_THREADMODE`:
    This is either undefined (for the default "seq" threadmode) or defined to
    an unspecified non-zero integer value for the "par" threadmode.
    Recommended usage is `#if UPCXX_THREADMODE` to identify the need for
    thread-safty constructs, such as locks.
  * `UPCXX_CODEMODE`:
    This is either undefined (for the "debug" codemode) or defined to an
    unspecified non-zero integer value for the "opt" (production) codemode.
  * `UPCXX_NETWORK_*`:
    The network being targeted is indicated by a preprocessor identifier with a
    `UPCXX_NETWORK_` prefix followed by the network name in capitals, which is
    defined to a non-zero integer value.  Identifiers corresponding to other
    networks are undefined.  Examples include `UPCXX_NETWORK_IBV` and
    `UPCXX_NETWORK_ARIES`.

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
    while holding the master persona.

Note that these restrictions must be respected by all object files linked into
the final executable, as they are all sharing the same libupcxx.

Types of communication that do not experience restriction:

  * Sending lpc's via `upcxx::persona::lpc()` or `<completion>_cx::as_lpc()`
    has no added restriction.

  * `upcxx::progress()` and `upcxx::future::wait()` have no added restriction.
    Incoming rpc's are only processed if progress is called from the primordial
    thread while it has the master persona.

  * Upcasting/downcasting shared heap memory (e.g. `global_ptr::local()`) is
    always OK. This facilitates a kind of interprocess communication via native
    CPU shared memory access which is permitted in "seq". Note that
    `upcxx::rput/rget` is still invalid from non-primordial threads even when
    the remote memory is downcastable locally.

The legality of lpc and progress from the non-primordial thread permits users
to orchestrate their own "funneling" strategy, e.g.:

```
#!c++

// How a non-primordial thread can tell the master persona to put an rpc on the
// wire on its behalf.
upcxx::master_persona().lpc_ff([=]() {
  upcxx::rpc_ff(99, [=]() { std::cout << "Initiated from far away."; });
});
```

## Interoperability and Multi-Threading ##

Some caution must be taken when integrating threaded upcxx code with other
threading paradigms (notably OpenMP). The main issue comes about when one
threading library blocks a thread waiting for a condition that is satisfied
concurrently by another thread. Unfortunately, when threading libraries block,
they tend only to service their internal state and not the state of the other
threading libraries that have outstanding work. For instance, when an OpenMP
thread enters an OpenMP barrier it will no longer be capable of servicing
the upcxx runtime: OpenMP is not kind enough make calls to `upcxx::progress()`
while waiting for the condition that will release it from the barrier. Deadlock
can ensue whenever there are two threads A and B (possibly on different
processes) where thread A is blocking for an OpenMP condition before servicing
upcxx and thread B is blocking for a upcxx condition before servicing OpenMP.

The following example has such a deadlock:

```
#!c++

#pragma omp parallel num_threads(2)
{
  // Assume thread 0 has the master persona...
  
  if(omp_get_thread_num() == 1) {
    // Bounce an rpc off buddy rank and wait for it.
    upcxx::rpc(upcxx::rank_me() ^ 1, [=](){}).wait();
  }
  // Here comes the implicit OpenMP barrier at the end of the parallel region. 
  // While in the OpenMP barrier, the master can't respond to rpc's. So we can't 
  // reply to our buddy's message and therefore will delay the buddy's sending
  // thread from making it to the OpenMP barrier. And if our buddy is in the same
  // situation with respect to our message then we deadlock.
}

```

Unfortunately we have no simple fix for this scenario. There are two separate
runtimes blocking for conditions that can only be satisfied by the other
runtime making progress. The user must ensure this doesn't happen by either
breaking the cyclic dependencies, or carefully quiescing the activity of one
runtime before blocking in the another. Another strategy is to separate
the master persona from the threads using OpenMP, for example by moving the
master persona to a scheduling thread that never blocks in OpenMP constructs.
This can ensure the master persona always remains attentive to service incoming
RPCs from other processes.

Another deadlock issue can arise from failing to discharge personas before
they cease to be attentive:

```
#!c++

int got = -1;

#pragma omp parallel num_threads(2)
{
  // Assume thread 0 has master...
  
  upcxx::global_ptr<int> gp = /*...*/;
  
  if(omp_get_thread_num() == 1) {
    int *value_spot = &got;
    upcxx::rget(gp,
      upcxx::operation_cx::as_lpc(
        upcxx::master_persona(),
        [=](int value) { *value_spot = value; }
      )
    );
  }

  upcxx::progress(); // Try to be nice, not good enough!
}

// wait for the rget injected by omp thread=1 to complete... will it?
while(got == -1)
  upcxx::progress();
```

Here, upon leaving the parallel region, the thread which injected the rget will
most likely go to sleep in OpenMP's thread pool. Unfortunately, if the rget
hasn't replied before the thread sleeps, then the associated lpc won't be fired
since no thread is polling that persona (thread 1's default persona). Possible
fixes include calling `upcxx::discharge()` from thread 1 before leaving the
region, or thread 1 releasing the persona it used to inject the rget (the
default persona won't work), and the serial thread outside the region picks up
that persona while spinning.

The astute reader of the "UPC++ Specification: Progress" section may notice
that `upcxx::progress_required()` is semantically prohibited from ever
conveying to the application that the master persona is free from needing
progress. So attempting to discharge the master persona has no effect on being
provably deadlock free, which begs the question can that thread ever enter an
OpenMP barrier safely? Not by the specification's rules. But here in the
implementation we've loosened the requirement somewhat. This implementation and
all of its future versions guarantee that under `UPCXX_THREADMODE={seq|par}`
the master persona will never silently be required to progress outgoing work
initiated by another persona on this process. The implication is that for
`upcxx::discharge/progress_required()` the master persona comes with the same
discharging guarantees as other personas. With this property of the
implementation, the second example remains fixable according to the advice given.
