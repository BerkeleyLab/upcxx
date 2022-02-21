#include <upcxx/upcxx.hpp>
#include <iostream>
#include <memory>
#include "../util.hpp"

/*
 * This example illustrates the use of UPCXX_SERIALIZED_FIELDS to serialize a 
 * reference to a statically massive type.
 *
 * This is an example of subset serialization, in which a proper subset of
 * fields is sent without mutation in order to reduce communication costs.
 * Hence, the full object state is not transmitted and the RPCs transmitting
 * these objects must be aware of this so that they do not try to access
 * invalid/uninitialized state.
 */

#ifndef N
#define N (128 * 1024 * 1024)
#endif

class massive {
    public:
        /*
         * A flag used for testing whether an instance was
         * constructed by UPC++ deserialization logic or by user code.
         */
        bool deserialized;

        int value;
        int scratch_space[N];

        // Default constructor used by UPC++ serialization
        massive() {
            std::cout << "deserialization..." << std::endl;
            deserialized = true;
            scratch_space[N-1] = 27;
        }

        // Constructor to be used from user code
        massive(int val) {
            std::cout << "construction..." << std::endl;
            value = val;
            scratch_space[N-1] = 666;
            deserialized = false;
        }

        UPCXX_SERIALIZED_FIELDS(value)
};

int main(void) {
    upcxx::init();
    print_test_header();

    int rank = upcxx::rank_me();
    int nranks = upcxx::rank_n();

    massive *r = new massive(42);
    assert(!r->deserialized);

    std::cout << "serialization..." << std::endl;
  #if 0
    // This approach technically works (with a large enough program stack)
    // but is a bad idea, because it imposes lots of overhead copying the large type
    // on both sides.
    upcxx::rpc(0, [] (const massive& r_r) {
                /*
                 * Validate that this is a deserialized instance 
                 * and that the entire object was not sent.
                 */
                assert(r_r.deserialized);
                assert(r_r.value==42);
                assert(r_r.scratch_space[N-1] == 27);
            }, std::move(*r)).wait();
  #else
    // This approach avoids the problem by using a view, which
    // avoids unnecessary copies on the initiator.
    upcxx::rpc(0, [] (upcxx::view<massive> r_view) {
        #if UPCXX_SPEC_VERSION <= 20200300L
              // Prior to 2020.3.8, this was still not great because the view's deserializing_iterator
              // forced the large object to be instantiated and returned by value, which still leads
              // to stack overflows on many platforms.
              static massive const & r_r = *r_view.begin();
        #else
              // Starting in 2020.3.8, we can use deserializing_iterator::deserialize_into()
              // thereby avoiding all object copies.
              // This also allows us to exclusively use heap memory for our object
              // (some systems get finicky about static objects this size, even in static data)
              std::unique_ptr<massive> p(new massive);
              massive &r_r = *p;
              r_view.begin().deserialize_into(&r_r);
        #endif
                /*
                 * Validate that this is a deserialized instance 
                 * and that the entire object was not sent.
                 */
                assert(r_r.deserialized);
                assert(r_r.value==42);
                assert(r_r.scratch_space[N-1] == 27);
            }, upcxx::make_view(r,r+1)).wait();
  #endif

    print_test_success();
    delete r;
    upcxx::finalize();

    return 0;
}
