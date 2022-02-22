#include <vector>
#include "../util.hpp"

struct CtgInfo {
  size_t cid;
  char c;
  UPCXX_SERIALIZED_FIELDS(cid, c)
};
using std::vector;

using upcxx::rank_me;
using upcxx::rank_n;
using upcxx::rpc;

using Data = vector<vector<CtgInfo>>;

int main() {
  upcxx::init();
  print_test_header();

  upcxx::dist_object<size_t> dist_count(0);
  static size_t count_sent = 0, count_received = 0, count_returned = 0;
  for (int i = 0; i < 10; i++) {
    if (!rank_me()) std::cout << i << std::endl;
    auto target_rank = (rank_me() + i) % rank_n();
    upcxx::barrier();
    auto fut = rpc(target_rank,
                   [](upcxx::dist_object<size_t> &dist_count) -> Data {
                     Data data(count_received);
                     for (auto &v : data) {
                       v.resize(count_received);
                     }
                     (*dist_count)++;
                     count_received++;
                     std::cout << "Sending " << data.size() << " entries" << std::endl;
                     return data;
                   },
                   dist_count);

    std::cout << "Sent " << count_sent << std::endl;
    count_sent++;

    fut.then([](Data data) {
         assert(data.size() == count_returned);
         for (auto &v : data) {
           assert(v.size() == count_returned);
         }
         std::cout << "Got return " << data.size() << std::endl;
         count_returned++;
       })
        .wait();
    upcxx::barrier();
    assert(count_received == count_returned);
    assert(count_returned == (size_t)i + 1);
    assert(*dist_count == count_returned);
    assert(count_sent == count_returned);
  }

  upcxx::barrier();
  assert(count_received == count_returned);
  assert(*dist_count == count_returned);
  assert(count_sent == count_returned);

  print_test_success();
  upcxx::finalize();
}
