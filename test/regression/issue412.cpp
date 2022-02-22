#include <upcxx/upcxx.hpp>
#include <cstdio>
#include <memory>
#include "../util.hpp"

int main(int argc, char **argv) {
    upcxx::init();
    print_test_header();
    upcxx::barrier();

    using AD = upcxx::atomic_domain<int>;
    //AD ad({upcxx::atomic_op::fetch_add, upcxx::atomic_op::load, upcxx::atomic_op::store}, upcxx::world());
    std::vector<upcxx::atomic_op> ops{upcxx::atomic_op::fetch_add, upcxx::atomic_op::load, upcxx::atomic_op::store};
    std::shared_ptr< AD > sh_ad = std::make_shared<AD>(ops, upcxx::world());

    upcxx::barrier();
    sh_ad->destroy();
    upcxx::barrier();
    sh_ad.reset();

    upcxx::barrier();
    if (!upcxx::rank_me()) std::cout << "Normal destory good" << std::endl;
    upcxx::barrier();

    sh_ad = std::make_shared<AD>(ops, upcxx::world());

    upcxx::future<> fut = upcxx::barrier_async().then([sh_ad](){
        assert(upcxx::current_persona().active_with_caller());
        assert(upcxx::master_persona().active_with_caller());
        std::cout << upcxx::rank_me() << " ready\n";
        //sh_ad->destroy(); // error
        //sh_ad->destroy(upcxx::entry_barrier::none); // okay
        sh_ad->destroy(upcxx::entry_barrier::internal); // previously hung due to issue #412
        std::cout << upcxx::rank_me() << " destroyed\n";
        });

    fut.wait();
    upcxx::barrier();
    sh_ad.reset();

    print_test_success();
    upcxx::finalize();
    return 0;
}
