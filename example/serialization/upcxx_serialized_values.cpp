#include <upcxx/upcxx.hpp>
#include <iostream>
#include <memory>

/*
 * This example illustrates the use of UPCXX_SERIALIZED_VALUES to serialize an
 * object captured by a call to upcxx::rpc.
 *
 * This example computes a sum reduction across all ranks of the values
 * contained in the 'values' array, which is a field of the class
 * dist_reduction. The local/partial sum reduction is computed in the call to
 * UPCXX_SERIALIZED_VALUES when an instance of dist_reduction is captured by a
 * call to upcxx::rpc. Each rank sends an RPC to rank 0, which implicitly first
 * computes a local sum reduction and then a global sum reduction on rank 0.
 * UPCXX_SERIALIZED_VALUES allows us to only send a single double (the result of
 * the local sum reduction) rather than the entire dist_reduction object.
 *
 * This is an example of subset serialization, in which a proper subset of
 * fields is sent without mutation in order to reduce communication costs.
 * Hence, the full object state is not transmitted and the RPCs transmitting
 * these objects must be aware of this so that they do not try to access
 * invalid/uninitialized state.
 */

#define N (128)

class dist_reduction {
    public:
        /*
         * A flag used for testing whether an instance of dist_reduction was
         * constructed by UPC++ deserialization logic or by user code.
         */
        bool deserialized;
        // The values to perform a sum reduction across
        std::unique_ptr<double[]> values;
        // Used to store a local sum reduction result on each rank
        double partial_sum_reduction;

        // Default contructor used by user code
        dist_reduction() : values(new double[N]) {
            for (int i = 0; i < N; i++) {
                values[i] = 1.0;
            }
            partial_sum_reduction = 0.0;
            deserialized = false;
        }

        // Constructor used by UPC++ deserialization
        dist_reduction(double _partial_sum_reduction) :
                partial_sum_reduction(_partial_sum_reduction) {
            deserialized = true;
        }

        double calculate_partial_sum_reduction() const {
            double partial_sum_reduction = 0.0;
            for (int i = 0; i < N; i++) {
                partial_sum_reduction += values[i];
            }
            return partial_sum_reduction;
        }

        bool was_deserialized() const { return deserialized; }

        UPCXX_SERIALIZED_VALUES(calculate_partial_sum_reduction())
};

int main(void) {
    upcxx::init();

    int rank = upcxx::rank_me();
    int nranks = upcxx::rank_n();

    upcxx::dist_object<double> sum_reduction(0);

    dist_reduction reduce;

    upcxx::rpc(0, [] (upcxx::dist_object<double>& sum_reduction,
                    const dist_reduction& reduce){
                /*
                 * Validate that this is a deserialized instance of
                 * dist_reduction, and that the entire object was not sent.
                 */
                UPCXX_ASSERT(reduce.was_deserialized());
                *sum_reduction += reduce.partial_sum_reduction;
            }, sum_reduction, std::move(reduce)).wait();

    upcxx::barrier();

    if (rank == 0) {
        UPCXX_ASSERT(*sum_reduction == nranks * N);
        std::cout << "Rank 0 out of " << nranks << " got a sum of " <<
            *sum_reduction << std::endl;
        std::cout << "SUCCESS" << std::endl;
    }

    upcxx::finalize();

    return 0;
}
