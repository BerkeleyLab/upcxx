#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"
#include <assert.h>

using namespace std;
using namespace upcxx;

#define VAL(rank,iter) ((rank)*1000 + (iter)*10)

int main() {
    upcxx::init();
    print_test_header();

    dist_object< global_ptr<int> > dobj(new_array<int>(4));
    int *mydata = dobj->local();
    int peer[2] = { (rank_me() + rank_n() - 1) % rank_n(), (rank_me() + 1) % rank_n() };
    global_ptr<int> gptr[2];
    gptr[0] = dobj.fetch(peer[0]).wait() + 2;
    gptr[1] = dobj.fetch(peer[1]).wait();

    promise<> p[2];
    future<> f[2];
    for (int i=0; i < 100; i++) {
      int step = i % 2;
      int prev = !step;
      int val = VAL(rank_me(), i); // easily identifiable val
      const char *method;
      // re-init p[step] for this iteration:
      #if PLACEMENT // works
        new (&p[step]) promise<>();
        method = "PLACEMENT";
      #elif COPY_ASSIGN // should fail
        promise<> tmp;
        p[step] = tmp;
        method = "COPY_ASSIGN";
      #else //MOVE_ASSIGN - should work
        promise<> tmp;
        p[step] = std::move(tmp);
        method = "MOVE_ASSIGN";
      #endif
      if (i == 0) cout << "Rank " << rank_me() << " using " << method << endl;
      if (i > 1) assert(f[step].is_ready()); // check 2 iters ago
      f[step] = p[step].get_future();
      assert(!f[step].is_ready()); assert(!p[step].get_future().is_ready());

      // communicate for this iteration
      rput(val,   gptr[step],   operation_cx::as_promise(p[step]));
      rput(val+1, gptr[step]+1, operation_cx::as_promise(p[step]));
      p[step].finalize();
      #if UPCXX_DEFER_COMPLETION
        // with eager completion and local bypass, p[step] may be
        // readied by finalize() above
        assert(!f[step].is_ready()); assert(!p[step].get_future().is_ready());
      #endif

      if (i > 0) {  // sync and check the last iteration
        f[prev].wait();
        assert(f[prev].is_ready()); assert(p[prev].get_future().is_ready());
        barrier();
        assert(mydata[!prev*2]   == VAL(peer[!prev],i-1));
        assert(mydata[!prev*2+1] == VAL(peer[!prev],i-1)+1);
        barrier();
      }
    }
    // epilog: sync last iteration
    p[0].get_future().wait();
    p[1].get_future().wait();

    barrier();

    print_test_success(true);
    upcxx::finalize();
    return 0;
} 
