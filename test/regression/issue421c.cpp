#include <utility>
#include "../util.hpp"

using namespace upcxx;

int errors = 0;

void check_val(global_ptr<int> gp, int expect) {
  int val = *gp.local();
  const char *diag = "";
  if (val != expect) {
    diag="  ERROR"; errors++;
  }
  say() << (gp) << " => " << val << " expect=" << expect << diag; 
}


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
  check_val(gp2, 42);
  done = false;
  barrier();

  // local to remote
  copy(gp3, gp_left2, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  check_val(gp2, 420 + right);
  done = false;
  barrier();

  // remote to local
  *gp3.local() = -1;
  copy(gp_left2, gp3, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  check_val(gp3, 420 + rank_me());
  done = false;
  barrier();

  // remote to remote
  copy(gp_left3, gp_right2, 1, remote_cx::as_rpc([]() { done = true; }));
  while (!done) upcxx::progress();
  check_val(gp2, 420 + (rank_me() + rank_n() - 2) % rank_n());
  done = false;
  barrier();

  check_val(gp1, 42);
  check_val(gp3, 420 + rank_me());

  delete_(gp1);
  delete_(gp2);
  delete_(gp3);
  print_test_success(errors == 0);
  upcxx::finalize();

  return errors;
}
