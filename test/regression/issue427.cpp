#include <vector>
#include "../util.hpp"

struct CtgInfo {
  size_t cid;
  char c;
  UPCXX_SERIALIZED_FIELDS(cid, c)
};

int main() {
  upcxx::init();
  print_test_header();

  static size_t count = 20;
  for (int i = 0; i < 100; i++) {
    if (!upcxx::rank_me()) std::cout << i << std::endl;
    auto target_rank = (upcxx::rank_me() + i) % upcxx::rank_n();
    upcxx::barrier();
    std::vector<CtgInfo> vec(i * count, CtgInfo{});
    auto fut = upcxx::rpc(target_rank,
                          [i](const std::vector<CtgInfo> &vec) {
                            assert(vec.size() == i * count);
                            return vec;
                          },
                          vec);
    fut.then([i](const std::vector<CtgInfo> &vec) {
               assert(vec.size() == i * count);
             })
      .wait();
    upcxx::barrier();
  }

  print_test_success();
  upcxx::finalize();
}
