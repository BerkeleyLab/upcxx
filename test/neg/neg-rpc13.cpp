#include <upcxx/upcxx.hpp>

struct NmNcFn { // non-movable/non-copyable function object
  int x;
  void operator()() {}
  NmNcFn() {}
  NmNcFn(const NmNcFn&) = delete;
  UPCXX_SERIALIZED_FIELDS(x)
};

int main() {
  upcxx::init();
  upcxx::rpc(upcxx::world(), 0, NmNcFn()).wait_reference();
  upcxx::finalize();
}
