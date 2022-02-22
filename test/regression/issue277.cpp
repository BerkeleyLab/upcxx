#include "../util.hpp"
#include <iostream>
#include <algorithm>

using namespace upcxx;
using namespace std;

future<> fetch_vals(global_ptr<int> gp, int *dst, int cnt) {
#if STATIC_PROMISE
  static 
#endif
  assert(cnt % 2 == 0);
  promise<> p(cnt/2 + 1);
  promise<> p1 = p;
  for (int i=0;i<cnt;i++) {
    if (i & 1) {
      upcxx::rget(gp+i, dst+i, 1, operation_cx::as_promise(p));
    } else {
      upcxx::rget(gp+i, dst+i, 1).then([dst,i,p,p1]() {
        dst[i] *= 2;  // some post-processing
        if (i & 2) p.fulfill_anonymous(1);
        else       p1.fulfill_anonymous(1);
      }); 
    }
  }
  return p1.finalize();
}

double stack_writer(double v) {
  if (v < 3) return 1;
  else return stack_writer(v-2) * stack_writer(v-1) * v;
}

int main() {
  upcxx::init();
  print_test_header();
  int cnt = 100;
  int *vals = new int[cnt]();
  global_ptr<int> gp;

  if (!rank_me()) {
    gp = new_array<int>(cnt);
    int *src = gp.local();
    std::fill(src, src+cnt, 42);
  }
  gp = broadcast(gp, 0).wait();
  future<> f = fetch_vals(gp, vals, cnt);
  cout << stack_writer(10.1) << endl;
  f.wait();

  for (int i=0;i<cnt;i++) {
    if (i & 1) assert(vals[i] == 42);
    else       assert(vals[i] == 84);
  }

  barrier();
  delete [] vals;

  print_test_success();
  upcxx::finalize();
}
