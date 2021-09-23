/*
 * This is the test driver for all the reduction examples that appeared in former versions of the guide. 
 * It wraps all of the various reduce_to_rank0 implementations, using namespaces. 
 * Although this is a rather peculiar structure, it is intended to exercise
 * individual stand-alone code snippets implementing a naive reduction in various ways.
*/

#include <libgen.h>
#include <iostream>
#include <cstdlib>
#include <random>
#include <upcxx/upcxx.hpp>
#include "../../test/util.hpp"

using namespace std;

namespace rpc {
    #include "reduce-rpc.hpp"
}

namespace rpc_no_barrier {
    #include "reduce-rpc-no-barrier.hpp"
}

namespace rpc_ff {
    #include "reduce-rpc_ff.hpp"
}

namespace distobj {
    #include "reduce-distobj.hpp"
}

namespace distobj_async {
    #include "reduce-distobj-async.hpp"
}

namespace rput {
    #include "reduce-rput.hpp"
}

namespace rput_rpc_promise {
    #include "reduce-rput-rpc-promise.hpp"
}

namespace atomics {
    #include "reduce-atomics.hpp"
}

int hit()
{
    double x = static_cast<double>(rand()) / RAND_MAX;
    double y = static_cast<double>(rand()) / RAND_MAX;
    if (x*x + y*y <= 1.0) return 1;
    else return 0;
}

// Perform the reduction using given algorithm, report and check the results
#define ACCM(version) do {                           \
    int hits = version::reduce_to_rank0(my_hits);    \
    if (!upcxx::rank_me()) {                         \
        cout << #version << ": pi estimate: " << 4.0 * hits / trials << endl; \
        UPCXX_ASSERT_ALWAYS(hits == hits_reference, "hits mismatch in " #version ); \
    } \
  } while (0)

int main(int argc, char **argv)
{
    upcxx::init();
    if (!upcxx::rank_me()) {
        cout << "Testing " << basename((char*)__FILE__) << " with " << upcxx::rank_n() << " ranks" << endl;
    }
    int my_hits = 0;
    int my_trials = 100000;
    if (argc >= 2) my_trials = atoi(argv[1]);
    int trials = upcxx::rank_n() * my_trials;
    if (!upcxx::rank_me()) 
      cout << "Calculating pi with " << trials << " trials, distributed across " << upcxx::rank_n() << " ranks." << endl;
    srand(upcxx::rank_me());
    for (int i = 0; i < my_trials; i++) {
        my_hits += hit();
    }

    // recommended algorithm: built-in reduction call
    int hits_reference = upcxx::reduce_one(my_hits, upcxx::op_fast_add, 0).wait();

    // output initial result and check it's reasonable
    if (!upcxx::rank_me()) {
        double pi = 4.0 * hits_reference / trials;
        cout << "Computed pi to be " << pi << endl;
        cout << "Estimate from rank 0 alone: " << 4.0 * my_hits / my_trials << endl;
        UPCXX_ASSERT_ALWAYS(pi >= 3 && pi <= 3.5, "pi is out of range (3, 3.5)");
    }

    // now test some alternate reduction algorithms, provided for demonstration purposes
    ACCM(rpc);
    ACCM(rpc_no_barrier);
    ACCM(rpc_ff);
    ACCM(distobj);
    ACCM(distobj_async);
    ACCM(rput);
    ACCM(rput_rpc_promise);
    ACCM(atomics);

    if (!upcxx::rank_me()) cout << "SUCCESS" << endl;
    upcxx::finalize();
    return 0;
}
