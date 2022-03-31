#include <utility>
#include "../util.hpp"

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  global_ptr<int> gp1 = new_<int>();
  global_ptr<int> gp2 = new_<int>();
  global_ptr<int> gp3 = new_<int>();
  *gp1.local() = 42;
  *gp3.local() = 420 + rank_me();
  dist_object<std::pair<global_ptr<int>,
                        global_ptr<int>>> dptr(std::make_pair(gp2, gp3));
  static bool done = false;
  int left = (rank_me() + rank_n() - 1) % rank_n();
  int right = (rank_me() + 1) % rank_n();
  global_ptr<int> gp_left2, gp_left3;
  std::tie(gp_left2, gp_left3) = dptr.fetch(left).wait();
  global_ptr<int> gp_right2, gp_right3;
  std::tie(gp_right2, gp_right3) = dptr.fetch(right).wait();
  barrier();

  // local to local
  copy(gp1, gp2, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  assert(*gp2.local() == 42);
  done = false;
  barrier();

  // local to remote
  copy(gp3, gp_left2, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  done = false;
  barrier();
  assert(*gp2.local() == 420 + right);
  barrier();

  // remote to local
  copy(gp_left2, gp3, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  done = false;
  barrier();
  assert(*gp3.local() == 420 + rank_me());
  barrier();

  // remote to remote
  copy(gp_left3, gp_right2, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  done = false;
  barrier();
  assert(*gp2.local() == 420 + (rank_me() + rank_n() - 2) % rank_n());
  barrier();

  delete_(gp1);
  delete_(gp2);
  delete_(gp3);
  print_test_success();
  upcxx::finalize();
}
