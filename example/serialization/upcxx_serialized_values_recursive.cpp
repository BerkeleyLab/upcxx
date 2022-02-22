#include <upcxx/upcxx.hpp>
#include <iostream>
#include <string>
#include <vector>

/*
 * This file illustrates a few simple examples of serializing nested
 * user-defined or STL classes from within a user-defined class using
 * UPCXX_SERIALIZED_FIELDS.
 */

class custom_class_1 {
    public:
        int rank;
        std::string msg;
        std::string full_msg;

        custom_class_1(int _rank, std::string _msg) : rank(_rank), msg(_msg) { }
        custom_class_1(std::string _full_msg) : full_msg(_full_msg) { }

        UPCXX_SERIALIZED_VALUES("RANK " + std::to_string(rank) + "[" + msg + "]")
};

std::string concat_strings(const std::vector<std::string>& v) {
    std::stringstream ss;
    for (auto i = v.begin(), e = v.end(); i != e; i++) {
        ss << " " << *i;
    }
    return ss.str();
}

class custom_class_2 {
    public:
        int rank;
        std::vector<std::string> msgs;
        std::string full_msg;

        custom_class_2(int _rank) : rank(_rank) { }
        custom_class_2(std::string _full_msg) : full_msg(_full_msg) { }

        void add_msg(std::string m) { msgs.push_back(m); }

        UPCXX_SERIALIZED_VALUES("RANK " + std::to_string(rank) + "[" +
                concat_strings(msgs) + "]")
};

int main(void) {
    upcxx::init();

    int rank = upcxx::rank_me();

    // Illustrate serializing and sending a std::string
    custom_class_1 msg(rank, std::string("Howdy!"));
    upcxx::rpc(0, [] (const custom_class_1& msg) {
                std::cout << msg.full_msg << std::endl;
            }, msg).wait();
    upcxx::barrier();

    if (rank == 0) std::cout << std::endl;

    // Illustrate serializing and sending a std::vector<std::string>
    custom_class_2 msgs(rank);
    msgs.add_msg("Howdy");
    msgs.add_msg("again!");
    upcxx::rpc(0, [] (const custom_class_2& msgs) {
                std::cout << msgs.full_msg << std::endl;
            }, msgs).wait();
    upcxx::barrier();

    if (rank == 0) {
        std::cout << "SUCCESS" << std::endl;
    }

    upcxx::finalize();

    return 0;
}
