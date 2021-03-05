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
  upcxx::rpc_ff(0, NmNcFn());
  upcxx::finalize();
}
