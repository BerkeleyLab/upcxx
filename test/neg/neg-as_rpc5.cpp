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
  upcxx::dist_object<upcxx::global_ptr<int>> dobj(upcxx::new_<int>(0));
  upcxx::global_ptr<int> gp = dobj.fetch(0).wait();
  upcxx::rput(42, gp, upcxx::remote_cx::as_rpc(NmNcFn()));
  upcxx::finalize();
}
