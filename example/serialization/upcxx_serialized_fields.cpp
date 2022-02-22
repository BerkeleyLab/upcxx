#include <upcxx/upcxx.hpp>
#include <iostream>
#include <memory>

/*
 * This example illustrates the use of UPCXX_SERIALIZED_FIELDS to serialize an
 * object captured by a call to upcxx::rpc.
 *
 * This example computes a sum reduction across all ranks of the values
 * contained in the 'values' array, which is a field of the class
 * dist_reduction. The local/partial sum reduction is computed in to the field
 * 'partial_sum_reduction', and then an object of type dist_reduction is sent
 * from each rank to rank 0 where the global reduction is computed. Using
 * UPCXX_SERIALIZED_FIELDS allows us to only send the 'partial_sum_reduction'
 * field of dist_reduction, and not the entire 'values' field. This
 * significantly reduces the number of bytes sent over the wire.
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

        // Default constructor used by UPC++ serialization
        dist_reduction() {
            deserialized = true;
        }

        // Constructor to be used from user code
        dist_reduction(bool set_deserialized) : values(new double[N]) {
            for (int i = 0; i < N; i++) {
                values[i] = 1.0;
            }
            partial_sum_reduction = 0.0;
            deserialized = set_deserialized;
        }

        void calculate_partial_sum_reduction() {
            for (int i = 0; i < N; i++) {
                partial_sum_reduction += values[i];
            }
        }

        bool was_deserialized() const { return deserialized; }

        UPCXX_SERIALIZED_FIELDS(partial_sum_reduction)
};

int main(void) {
    upcxx::init();

    int rank = upcxx::rank_me();
    int nranks = upcxx::rank_n();

    upcxx::dist_object<double> sum_reduction(0);

    // Compute a local sum reduction
    dist_reduction reduce(false);
    reduce.calculate_partial_sum_reduction();
    UPCXX_ASSERT(reduce.partial_sum_reduction == N);
    UPCXX_ASSERT(!reduce.was_deserialized());

    /*
     * Compute a global sum reduction by sending local results to rank 0. Note
     * that this captures an object of type dist_reduction, but thanks to UPC++
     * serialization we only need to send one double over the network.
     */
    upcxx::rpc(0, [] (upcxx::dist_object<double>& sum_reduction,
                    const dist_reduction& reduce) {
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
