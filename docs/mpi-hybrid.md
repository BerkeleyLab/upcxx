## Recommendations for hybrid UPC++/MPI applications

With care, it is possible to construct applications which use both UPC++ and MPI
explicitly in user code (note that this is different from UPC++ programs which
do not explicitly use MPI, but are compiled with `UPCXX_NETWORK=mpi`: such programs do
not require any special treatment). Note however, that strict programming
conventions (below) must be adhered to when switching between MPI and UPC++
network communication, otherwise deadlock can result on many systems.

In general, mixed MPI/UPC++ applications must be linked with an MPI C++
compiler.  This may be named `mpicxx` or `mpic++`, among other possible names.
However, on Cray systems `CC` is both the regular C++ compiler and the MPI C++
compiler.  You may need to pass this same compiler as $CXX when installing UPC++
to ensure object compatibility.

Certain UPC++ network types (currently `mpi` and `ibv`) may use MPI
internally. For this reason, MPI objects should be compiled with the same MPI
compiler that was used when UPC++ itself was build (normally the `mpicc` in
one's $PATH, unless some action is taken to override that default).
Additionally, the MPI portion of an application should make use of
`MPI_Initialized()` to ensure exactly one call is made to initialize MPI.
See 'Correct library initialization' section below.

Both MPI and UPC++ cause network communication, and the respective runtimes do
so without any coordination. As a result, it is quite easy to cause network
deadlock when mixing MPI and UPC++, unless the following protocol is strictly
observed:

1.  When the application starts, the first MPI or UPC++ call (*i.e.*
    `MPI_Init()` or `upcxx::init()`) which may result in network traffic from
    any thread should be considered to put the entire job in 'MPI' or 'UPC++'
    mode, respectively.

2.  When an application is in 'MPI' mode, and needs to switch to using UPC++, it
    should quiesce all MPI operations in-flight and then collectively execute an
    `MPI_Barrier()` as the last MPI call before causing any UPC++
    communication. Once any UPC++ communication has occurred from any rank, the
    program should be considered to have switched to 'UPC++' mode.

3.  When an application is in 'UPC++' mode, and an MPI call that may communicate
    is needed, the application must quiesce all UPC++ communication and then
    execute a upcxx::barrier() before any MPI calls are made. Once any MPI
    functions have been called from any thread, the program should be considered
    to be in 'MPI' mode.

If this simple construct of alternating MPI and UPC++ phases can be observed,
then it should be possible to avoid deadlock.

### Correct library initialization

When writing your hybrid MPI / UPC++ program, it's important to correctly
initialize both layers before using them. When doing so, it's crucial to
realize that UPC++ might also be initializing MPI inside `upcxx::init()`,
e.g., in order to control MPI-based job spawning. Whether or not this happens
depends on your site configuration (see next section).

The recommended method for hybrids with no special MPI thread safety 
requirements is a formula such as:

```C++
#include <upcxx/upcxx.hpp>
#include <mpi.h>

#include <iostream> // optional

int main(int argc, char **argv) {
  // init UPC++
  upcxx::init();

  // init MPI, if necessary
  int mpi_already_init;
  MPI_Initialized(&mpi_already_init);
  if (!mpi_already_init) MPI_Init(&argc, &argv);

  /* program goes here */
  int mpi_rankme, mpi_rankn;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rankme);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_rankn);
  char hostname[MPI_MAX_PROCESSOR_NAME];
  int junk;
  MPI_Get_processor_name(hostname, &junk);
  std::cout << "Hello world from UPC++ rank " << upcxx::rank_me() << "/" << upcxx::rank_n()
            << ", MPI rank " << mpi_rankme << "/" << mpi_rankn << " : " 
            << hostname << std::endl;

  // Finalize MPI only if we initted it
  if (!mpi_already_init) MPI_Finalize();

  // finalize UPC++
  upcxx::finalize();

  return 0;
}
```

#### MPI thread-safety considerations

UPC++ configurations that initialize MPI generally do so with the minimal
thread-safety guarantees needed for correct UPC++ operation. For
`UPCXX_THREADMODE=seq` (the default) this usually means single-threaded MPI
mode `MPI_THREAD_SINGLE`. For `UPCXX_THREADMODE=par`, this usually means
`MPI_THREAD_SERIALIZED` (where the application is responsible for serializing
multi-threaded calls to MPI *and* UPC++ operations that may invoke MPI).  If your
MPI program needs full MPI thread safety (e.g., `MPI_THREAD_MULTIPLE`), there are
two possible approaches:

1. If your current UPC++ configuration is initializing MPI for you, set
   `export GASNET_MPI_THREAD=MULTIPLE` to request it does so using the stronger
   thread-safety mode. Note this only has any effect if `upcxx::init` indeed
   initializes MPI and does so before any other `MPI_Init*` calls in the process.
   You can confirm the setting by running with `upcxx-run -vv`.

2. Call `MPI_Init_thread()` explicitly before `upcxx::init()` with the desired
   thread-safety level.  UPC++ configurations using MPI will detect MPI has
   already been initialized and act accordingly.  Consult MPI documentation for
   the details of using this call.

### Job spawning

The details of correctly spawning parallel jobs are often site-specific,
especially for distributed systems. Correctly spawning a hybrid MPI / UPC++
application carries additional wrinkles on some systems. The details also
differ based on which GASNet conduit your UPC++ application was compiled for
(via `$UPCXX_NETWORK` or the default value determined at installation
time).

#### aries-conduit for Cray XC systems

The native GASNet conduits on Cray are fully compatible with the PMI-based ALPS
and SLURM spawners used at most sites. Run your job using the normal `aprun` or
`srun` command recommended for MPI programs at your site.

#### ibv-conduit for InfiniBand networks

On ibv-conduit, the use of the MPI-based spawner is recommended for best
results.  To enable this support, make sure to set `CXX=mpicxx` when installing
UPC++.  You may additionally need to set `$MPI_CC` at install time to the
location of the MPI C compiler, if the correct`mpicc` is not first in your
`$PATH`. This should enable MPI-based spawning by default when using
`upcxx-run`, otherwise you can force this behavior at run-time by setting
`GASNET_IBV_SPAWNER=mpi`.  

This assumes that job spawn support for regular MPI programs is already
correctly installed and configured on your system. Consult the documentation
for your MPI distribution if this is not the case.

#### udp-conduit for laptops and Ethernet networks

When running hybrid applications on a single-node system (e.g., your laptop) or on
Ethernet-based clusters, the recommended GASNet backend is udp-conduit (i.e.,
`UPCXX_NETWORK=udp` at app compile time). A few additional run-time
settings are recommended for MPI integration, and
then the job can be spawned using `upcxx-run`:

```bash
export GASNET_SPAWNFN='C'
export GASNET_CSPAWN_CMD='mpirun -np %N %C'
export GASNET_WORKER_RANK=OMPI_COMM_WORLD_RANK   # optional, assumes Open MPI
upcxx-run -np 2 hello-world
```

This assumes the `mpirun` command is in your `$PATH` and takes the usual
arguments - otherwise adjust the above settings accordingly. It also assumes
that job spawn support for regular MPI programs is already correctly installed
and configured on your system. Consult the documentation for your MPI
distribution if this is not the case.

The (optional) `GASNET_WORKER_RANK=OMPI_COMM_WORLD_RANK` setting above assumes
an Open-MPI-based MPI implementation, and names an environment variable provided
by the mpirun job spawner in the worker process environment that identifies the
MPI process rank.  This requests udp-conduit to assign the UPC++/GASNet rank
identifiers identically to the `MPI_COMM_WORLD` rank numbering for the
corresponding process.  For MPICH-based MPI implementations, you may want
the setting `GASNET_WORKER_RANK=PMI_RANK`.  For other MPI implementations,
consult their documentation for the corresponding variable name.

If `GASNET_WORKER_RANK` is unset (or names a non-existent variable) then
udp-conduit may number your UPC++ ranks differently from MPI ranks,
so you may want to perform an MPI rank renumbering after startup, eg:
```C++
  MPI_Comm newcomm;
  MPI_Comm_split(MPI_COMM_WORLD, 0, upcxx::rank_me(), &newcomm);
```
to create an MPI communicator that re-numbers the MPI ranks to match the UPC++ rank order.
Consult MPI documentation for further details on using communicators.

#### mpi-conduit portable MPI-based backend

On mpi-conduit, your UPC++ application *is* an MPI program, so job spawning works
exactly like it would for any other MPI program. Note this portable conduit exists
only for portability reasons and is basically *never* the highest-performance
choice for distributed systems.

#### smp-conduit for single-node systems

smp-conduit is currently incompatible with hybrid MPI applications, due to job
spawning limitations. Please use one of the other conduits listed above with
appropriate spawn arguments to use your single node. By default UPC++ will use
process shared memory and efficient inter-process comms to implement all the
on-node communication (bypassing the network adapter).  High-quality MPI
implementations will do the same.

### Troubleshooting:

If your application follows the recommendation above but still has problems,
here are some things to consider:

1.  Some HPC networks APIs are not well-suited to running multiple runtimes.
    They may either permit a process to "open" the network adapter at most once,
    or they may provide multiple opens but allow them to block one another.
    Additionally, it is possible in some cases that one network runtime may
    monopolize the network resources, leading to abnormally slow performance or
    complete failure of the other.  The solutions available for these case are
    either to use distinct networks for UPC++ and MPI, or to use mpi-conduit for
    UPC++.  These have adverse performance impact on different portions of your
    code, and the best choice will depend on specifics of your code.
    
    1.  Use TCP-based communication in MPI  
	      We have compiled a list of the relevant configuration parameters for
        several common MPI implementations, which can be found
        [here](https://gasnet.lbl.gov/dist/other/mpi-spawner/README) (look for
        the phrase "hybrid GASNet+MPI").
    2.  Use UDP for communication in UPC++  
        Set `export UPCXX_NETWORK=udp` when compiling UPC++ app code
    3.  Use MPI for communication in UPC++  
        Set `export UPCXX_NETWORK=mpi` when compiling UPC++ app code

2.  The Aries network adapter on the Cray XC platform has approximately 120
    hardware contexts for communications.  With MPI and UPC++ each consuming one
    per process, a 64-process-per-node run of a hybrid application exceeds the
    available resources.  The solution is to set the following two environment
    variables at run time to instruct both libraries to request virtualized
    contexts: `GASNET_GNI_FMA_SHARING=1 MPICH_GNI_FMA_SHARING=enabled`
