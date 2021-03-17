#include <upcxx/upcxx.hpp>
#include <iostream>

using upcxx::promise;
using upcxx::operation_cx;
using upcxx::world;

int main() {
  upcxx::init();

  int me = upcxx::rank_me();
  int n = upcxx::rank_n();
  
  promise<int> pi;
  upcxx::reduce_all(me, upcxx::op_fast_max, world(), operation_cx::as_promise(pi));
  
  promise<int> pi2;
  upcxx::reduce_one(me, upcxx::op_fast_max, 0, world(), operation_cx::as_promise(pi2));

#if 1
  promise<> p;
  upcxx::barrier_async(world(), operation_cx::as_promise(p));

  promise<int> pi3;
  upcxx::broadcast(42, 0, world(), operation_cx::as_promise(pi3));

  int vbuf[1] = {123};
  int awaiting = 1;

  { // trivial vector bcast
    awaiting += 1;
    if(me == 0) vbuf[0] = 456;
    upcxx::broadcast(vbuf, 1, 0, world(),
      operation_cx::as_lpc(
        upcxx::current_persona(),
        [&]() { awaiting--; }
      )
    );
  }

  { // nontrivial scalar bcast
    awaiting += 1;
    upcxx::experimental::broadcast_nontrivial(
      std::string(100, 'a' + (char)(me % 26)),
      0, world(),
      operation_cx::as_lpc(
        upcxx::current_persona(),
        [&](std::string &&got) {
          awaiting--;
          UPCXX_ASSERT_ALWAYS(got == std::string(100, 'a'));
        }
      )
    );
  }
  
  p.finalize().wait();

  int res3 = pi3.finalize().wait();
  UPCXX_ASSERT_ALWAYS(res3 == 42);

  awaiting -= 1;
  while(awaiting != 0)
    upcxx::progress();
  UPCXX_ASSERT_ALWAYS(vbuf[0] == 456);
#endif

  int res = pi.finalize().wait();
  UPCXX_ASSERT_ALWAYS(res == (n-1));

  int res2 = pi2.finalize().wait();
  if (!me) UPCXX_ASSERT_ALWAYS(res2 == (n-1));

  upcxx::barrier();
  if (!me) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
  return 0;
}
