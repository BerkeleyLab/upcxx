#include <upcxx/upcxx.hpp>
#include <iostream>
#include <vector>
#include <assert.h>
#include "util.hpp"

using std::int64_t;
using namespace upcxx;

// exercise shared segment queries
int main() {
  upcxx::init();


  int64_t total = upcxx::shared_segment_size();
  int64_t used_initial = upcxx::shared_segment_used();

  say() << "Shared segment: size=" << total << " used=" << used_initial;

  assert(total > 0); // NOT guaranteed by spec, but true for curr implementation
  assert(used_initial >= 0); // NOT guaranteed by spec, but true for curr implementation

  upcxx::barrier();

  if (!upcxx::rank_me()) {
    std::vector<global_ptr<char>> ptrs;
    int64_t used = used_initial; 
    for (size_t sz = 1; sz <= 64*1024; sz *= 2) {
      global_ptr<char> gp = upcxx::new_array<char>(sz);
      assert(gp);
      ptrs.push_back(gp);

      int64_t new_total = upcxx::shared_segment_size();
      assert(new_total == total); // NOT guaranteed by spec, but true for curr implementation

      int64_t new_used = upcxx::shared_segment_used();
      int64_t delta = new_used - used;
      say() << " allocated=" << sz << ": used=" << new_used << " delta=" << delta;

      assert(delta >= (int64_t)sz); // NOT guaranteed by spec, but should be true in this particular case

      used = new_used;
    }

    for (auto gp : ptrs) {
      upcxx::delete_array(gp);

      int64_t new_used = upcxx::shared_segment_used();
      int64_t delta = new_used - used;

      say() << " deleted one object: used=" << new_used << " delta=" << delta;
      assert(delta < 0); // NOT guaranteed by spec, but should be true in this particular case

      used = new_used;
    }

  }

  upcxx::barrier();

  if (!upcxx::rank_me()) {
    int64_t new_used = upcxx::shared_segment_used();
    int64_t new_total = upcxx::shared_segment_size();
    say() << "Shared segment: size=" << new_total << " used=" << new_used;

    assert(new_total == total); // NOT guaranteed by spec, but true for curr implementation
  }

  print_test_success();

  upcxx::finalize();
}
