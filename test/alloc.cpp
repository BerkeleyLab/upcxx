#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <new>
#include <assert.h>
#include <cstddef>
#include <upcxx/upcxx.hpp>

#ifndef DESTROY
#define DESTROY 0
#endif

#if UPC_INTEROP
  #include "interop/test.h"
  #define TEST_UPC(val) do {  \
    upcxx::barrier(); /* optional - keep output clean */ \
    test_upc((int)(val)); \
    upcxx::barrier(); /* optional - keep output clean */ \
  } while (0)
#else
  #define TEST_UPC(val) ((void)0)
#endif

using namespace std;

#undef ERROR
#define ERROR(stream) do { \
  errors++; \
  cerr << rank_me << ": ERROR at " << __LINE__ << ": " << stream << endl; \
} while(0)

#if __cplusplus > 201700L 
  #if __PGI // workaround issue #307
    #define byte unsigned char
  #else
    using byte = std::byte;
  #endif
#else
typedef unsigned char byte;
#endif

#define VAL(sz,i) ((byte)(((sz) << 2) | ((i) & 0x3)))

// usage: alloc <log_2 of max requests> <max heap to use in MB>
int main(int argc, char **argv) {
  #if UPC_INTEROP
    bupc_init(&argc, &argv); // optional, but recommended
  #endif

  int errors = 0;

  upcxx::init();

  upcxx::intrank_t rank_me = upcxx::rank_me();

  // determine an upper bound on the shared heap size
  size_t heapsz = 128<<20;
  const char *szstr = upcxx::getenv_console("UPCXX_SHARED_HEAP_SIZE");
  if (!szstr) szstr = upcxx::getenv_console("UPCXX_SEGMENT_MB");
  if (szstr) {
    float val = atof(szstr);
    if (val > 0) heapsz = val * (1<<20);
  }

  int log_maxreq = 10;
  argv++; argc--;
  if (argc > 0) {
    int reqarg = atoi(argv[0]);
    if (reqarg>=0 && reqarg < 62) log_maxreq = reqarg;
    argv++; argc--;
  }
  size_t maxreq = ((size_t)1)<<log_maxreq;

  size_t maxheap = (size_t)-1;
  if (argc > 0) {
    float heaparg = atof(argv[0]);
    if (heaparg>0) maxheap = heaparg * (1<<20);
    argv++; argc--;
  }
  size_t max_write = 4096;

  if (!rank_me) {
    cout << "Running allocator test, max_requests=" << maxreq;
    if (maxheap != (size_t)-1) cout << "  maxheap=" << (float)maxheap/(1<<20) << "MB";
    cout << endl;
  }
  upcxx::barrier();
  
  TEST_UPC(maxreq);

  for (size_t sz=1; sz <= std::min(maxheap,heapsz*2); sz *= 2) {
    size_t reqcnt = std::min(std::min(heapsz/sz + 10, maxreq), maxheap/sz);
    upcxx::global_ptr<byte> *ptrs = new upcxx::global_ptr<byte>[reqcnt];
    size_t alloc_cnt = 0;
    for (size_t i=0; i < reqcnt; i++) {
      upcxx::global_ptr<byte> gp = upcxx::allocate<byte>(sz);
      if (!gp) break; // oom failure
      ptrs[i] = gp;
      alloc_cnt++;
      // verify it actually works
      upcxx::intrank_t where = gp.where();
      bool local = gp.is_local();
      if (where != rank_me) ERROR("gp.where()=" << where);
      if (!local) ERROR("gp.is_local() is false");
      else {
        byte *lp = gp.local();
        if (!lp) ERROR("gp.local() is null");
        else {
          byte val = VAL(sz,i);
          *lp = val;
          assert(*lp == val);
          memset(lp, static_cast<int>(val), std::min(sz,max_write));
          upcxx::global_ptr<byte> cgp = upcxx::try_global_ptr(lp);
          if (!cgp) ERROR("try_global_ptr("<<lp<<") returned null");
          else if (cgp != gp) ERROR("cgp:[" << cgp << "] != gp:[" << gp << "]");
        }
      }
    }
    char report[255];
    snprintf(report,sizeof(report),
       "%4i: %10lli byte chunks: %10lli allocated, %10lli total bytes\n",
       (int)rank_me, (long long)sz, (long long)alloc_cnt, (long long)(alloc_cnt * sz)
    );
    cout << report << flush;

    TEST_UPC(sz);

    for (size_t i=0; i < alloc_cnt; i++) {
      byte val = VAL(sz,i);
      upcxx::global_ptr<byte> gp = ptrs[i];
      byte *lp = gp.local();
      if (!lp) ERROR("gp.local() is null");
      for (size_t j=0; j < std::min(sz,max_write); j++) {
        if (lp[j] != val) {
          ERROR("data corruption detected at i=" <<i<< " j=" <<j);
          break;
        }
      }
      upcxx::deallocate<byte>(gp);
    }
    delete [] ptrs;
    upcxx::barrier();
    #if DESTROY
      upcxx::experimental::destroy_heap();
      upcxx::experimental::restore_heap();
      upcxx::barrier();
    #endif
  }

  errors = upcxx::reduce_one(errors,upcxx::op_fast_add,0).wait();
  upcxx::finalize();

  if (!rank_me) {
    if (errors) cout << "FAILED: " <<errors<< " errors." << endl;
    else cout << "SUCCESS" << endl;
  }

  return 0;
}
