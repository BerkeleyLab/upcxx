// arrval_upcxx
//
// This UPC++ program demonstrates passing and use of UPC shared objects across
// the layer boundary. It calls into UPC to allocate objects on the shared heap, 
// then creates global_ptr's referencing those objects and performs RMA on them.
// Pointers to shared objects always cross the layer boundary as a raw C local 
// pointer (in this case (val_t *)), but can then be upcast to global_ptr<val_t>
// passed to other ranks and used just like any other global_ptr<T>.

#include <iostream>

#include <upcxx/upcxx.hpp>

#include "test.h"

#define CHECK(lp, base, sz) do { \
 if (arrval_check(lp, base, sz)) ERROR("failed arrval_check(" #lp "=" << (lp) \
                                       << ", " #base "=0x" << std::hex << (base) \
                                       << ", " #sz "=" << (sz) << ")"); \
} while (0)

// Usage: arrval <num_iters> <max_chunk>
// Peak shared memory utilization is approximately: (4 * max_chunk * num_iters)
int main(int argc, char **argv) {

  bupc_init(&argc, &argv); // optional, but recommended

  upcxx::init();

  int iters = 0;
  size_t maxchunk = 0;

  if (argc > 1) {
    iters = atoi(argv[1]);
    argc--; argv++;
  }
  if (!iters) iters = 100;
  if (argc > 1) {
    maxchunk = (size_t)atol(argv[1]);
    argc--; argv++;
  }
  if (!maxchunk) maxchunk = 16*1024;

  int rank_me = upcxx::rank_me();
  if (!rank_me) std::cout << "Running arrval_upcxx num_iters=" << iters << " max_chunk=" << maxchunk << std::endl;

  int gpeer = (upcxx::rank_me() + 1) % upcxx::rank_n();
  int lpeer = (upcxx::local_team().rank_me() + 1) % upcxx::local_team().rank_n();
  int lpeer_gid = upcxx::local_team()[lpeer];

  upcxx::barrier();
  char msg[120];
  snprintf(msg,sizeof(msg),"rank %i/%i: local rank %i/%i gpeer=%i lpeer=%i\n", upcxx::rank_me(), upcxx::rank_n(),
    upcxx::local_team().rank_me(), upcxx::local_team().rank_n(),
    gpeer, lpeer_gid);
  std::cout << msg << std::flush;
  upcxx::barrier();

  val_t **bufs = new val_t *[iters];
  for (size_t sz = 1; sz <= maxchunk; sz*=2) {
    for (int i=0; i < iters; i++) {
      // create an object from the opposite model heap and initialize it
      val_t *lp = construct_arr_upc(sz);
      UPCXX_ASSERT_ALWAYS(lp);
      bufs[i] = lp;
      val_t base = BASEVAL(rank_me, i);
      arrval_set(lp, base, sz);
      CHECK(lp, base, sz);

      // check upcast and downcasts
      upcxx::global_ptr<val_t> gp = upcxx::try_global_ptr(lp);
      if (!gp) ERROR("Failed to try_global_ptr " << lp);
      if (!gp.is_local()) ERROR("failed is_local for " << gp);
      val_t *lp2 = gp.local();
      if (lp != lp2) ERROR(lp << " != " << lp2);

      // exchange pointers around local_team and test usability
      upcxx::dist_object<upcxx::global_ptr<val_t>> dobj_local(upcxx::local_team(), gp);
      upcxx::global_ptr<val_t> lgp = dobj_local.fetch(lpeer).wait();
      if (!lgp.is_local()) ERROR("failed locality check for " << lgp);
      val_t *llp = lgp.local();
      int where = lgp.where();
      if (where != lpeer_gid) ERROR("lgp:" << lgp << " .where()=" << where << " expect=" << lpeer_gid);
      upcxx::global_ptr<val_t> cgp = upcxx::try_global_ptr(llp);
      if (cgp != lgp) ERROR("cgp:" << cgp << " != lgp:" << lgp);
      val_t base_lpeer = BASEVAL(lpeer_gid, i);
      CHECK(llp, base_lpeer, sz);

      // create a temporary local buffer using various methods
      val_t *tmp = nullptr;
      void (*tmpfree)(val_t *) = nullptr;
      switch (i % 3) {
        case 0: 
          tmp = new val_t[sz];
          tmpfree = ([](val_t *tmp){ delete [] tmp; });
        break;
        case 1: 
          tmp = construct_arr_upc(sz);
          tmpfree = destruct_arr_upc;
        break;
        case 2: 
          tmp = construct_arr_upcxx(sz);
          tmpfree = destruct_arr_upcxx;
        break;
      }
      UPCXX_ASSERT_ALWAYS(tmp && tmpfree);

      // test local RMA
      arrval_set(tmp, (val_t)-1, sz);
      upcxx::rget(lgp, tmp, sz).wait();
      CHECK(tmp, base_lpeer, sz);
      arrval_set(tmp, base, sz);
      upcxx::rput(tmp, lgp, sz).wait();
      CHECK(llp, base, sz);
      arrval_set(tmp, base_lpeer, sz);
      upcxx::rput(tmp, lgp, sz).wait();
      CHECK(llp, base_lpeer, sz);

      upcxx::barrier();

      // exchange pointers around world and test usability
      upcxx::dist_object<upcxx::global_ptr<val_t>> dobj_global(gp);
      upcxx::global_ptr<val_t> ggp = dobj_global.fetch(gpeer).wait();
      int gwhere = ggp.where();
      if (gwhere != gpeer) ERROR("ggp:" << ggp << " .where()=" << gwhere << " expect=" << gpeer);
      val_t base_gpeer = BASEVAL(gpeer, i);

      // test global RMA
      arrval_set(tmp, (val_t)-1, sz);
      upcxx::rget(ggp, tmp, sz).wait();
      CHECK(tmp, base_gpeer, sz);
      arrval_set(tmp, base, sz);
      upcxx::rput(tmp, ggp, sz).wait();
      arrval_set(tmp, (val_t)-1, sz);
      upcxx::rget(ggp, tmp, sz).wait();
      CHECK(tmp, base, sz);
      arrval_set(tmp, base_gpeer, sz);
      upcxx::rput(tmp, ggp, sz).wait();
      arrval_set(tmp, (val_t)-1, sz);
      upcxx::rget(ggp, tmp, sz).wait();
      CHECK(tmp, base_gpeer, sz);

      tmpfree(tmp);

      upcxx::barrier();

      CHECK(lp, base, sz);
    }
    // cleanup
    for (int i=0; i < iters; i++) {
      val_t *lp = bufs[i];
      UPCXX_ASSERT_ALWAYS(lp);
      val_t base = BASEVAL(rank_me, i);
      CHECK(lp, base, sz);
      destruct_arr_upc(lp);
    }
    upcxx::barrier();
  }
  delete [] bufs;

  upcxx::barrier();
  if (!rank_me) std::cout << "SUCCESS" << std::endl;

  upcxx::finalize();

  return 0;
}
