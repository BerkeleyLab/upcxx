#include <upcxx/upcxx.hpp>

struct NmNcFn { // non-movable/non-copyable function object
  int x;
  void operator()() {}
  NmNcFn() {}
  NmNcFn(const NmNcFn&) = delete;
};

int main() {
  upcxx::init();
  NmNcFn fn;
  upcxx::rpc(upcxx::world(), 0, fn).wait_reference();
  upcxx::finalize();
}
