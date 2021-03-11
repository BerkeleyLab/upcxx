#include <upcxx/upcxx.hpp>
using namespace upcxx;

struct bigarray_t {
  double value[1024];

  bigarray_t operator+=(bigarray_t const &other) { // YUK!
    for (int i=0; i<1024; i++) value[i] += other.value[i];
    return *this;
  }
};

int main() {
  init();

  global_ptr<bigarray_t> gp;
  if (!rank_me()) gp = new_<bigarray_t>();
  gp = broadcast(gp, 0).wait();

  static bigarray_t bigval;
  reduce_all(bigval, op_fast_add).wait(); // reduce huge val passed on stack

  finalize();
  return 0;
}
