// upc_link.c
// this file MUST be compiled as C (not C++) to support the bupc_tentative API

#if UPCXXI_ASSERT_ENABLED
  #undef NDEBUG
#else
  #define NDEBUG 1
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <upcxx/backend/gasnet/upc_link.h>
#include "bupc_tentative.h"

static gex_Rank_t upc_rank_me = GEX_RANK_INVALID;
static gex_Rank_t upc_rank_n = GEX_RANK_INVALID;
static int upc_is_init = 0;
static int upc_is_pthreads = 0;

extern int upcxxi_upc_is_linked(void) {
  int result = !!bupc_tentative_version_major;
  if (result) { // ensure our header clone is not out-of-date
    assert(bupc_tentative_version_major == BUPC_TENTATIVE_VERSION_MAJOR &&
           bupc_tentative_version_minor >= BUPC_TENTATIVE_VERSION_MINOR);
  }
  // sanity-check that the required symbols are present
  assert(result == !!bupc_tentative_init);
  assert(result == !!bupc_tentative_config_info);
  assert(result == !!bupc_tentative_alloc);
  assert(result == !!bupc_tentative_free);
  assert(result == !!bupc_tentative_all_alloc);
  assert(result == !!bupc_tentative_all_free);
  return result; 
}

extern int upcxxi_upc_is_pthreads(void) {
  return upc_is_pthreads;
}

extern void upcxxi_upc_init(
                gex_Client_t           *client_p,
                gex_EP_t               *ep_p,
                gex_TM_t               *tm_p
            ) {
  assert(upcxxi_upc_is_linked());
  assert(!upc_is_init);

  // Query UPCR configuration information, to check compatibility
  const char *upcr_config_str = NULL;
  const char *gasnet_config_str = NULL;
  const char *upcr_version_str = NULL;
  int upcr_debug = 0;
  bupc_tentative_config_info(&upcr_config_str, &gasnet_config_str, &upcr_version_str,
                             0, 0, &upcr_debug, &upc_is_pthreads, 0);

  static int    dummy_argc = 1;
  static char dummy_exename[] = "upcxx_dummy";
  static char *_dummy_argv[] = { dummy_exename, NULL };
  static char **dummy_argv = _dummy_argv;
  if (upc_is_pthreads) {
    // if UPC is in -pthreads mode, we require UPC to already be init
    // Otherwise, the following should trigger an error in DEBUG mode:
    int junk = bupc_tentative_mythread();
  } else {
    // non-pthreads mode may lazily init UPC
    bupc_tentative_init(&dummy_argc, &dummy_argv);
  }

  // we currently assume UPCR performs client init with GEX_FLAG_USES_GASNET1
  gasnet_QueryGexObjects(client_p, ep_p, tm_p, NULL);
  assert(gex_Client_QueryFlags(*client_p) & GEX_FLAG_USES_GASNET1);

  upc_rank_me = gex_TM_QueryRank(*tm_p);
  upc_rank_n = gex_TM_QuerySize(*tm_p);
  assert(upc_rank_n > 0);
  assert(upc_rank_me < upc_rank_n);

  upc_is_init = 1;
}

extern void *upcxxi_upc_alloc(size_t sz) {
  assert(upcxxi_upc_is_linked());
  assert(upc_is_init);

  void *ptr = bupc_tentative_alloc(sz);
  assert(ptr); // UPCR allocation failures are fatal
  return ptr;
}

extern void *upcxxi_upc_all_alloc(size_t sz) {
  assert(upcxxi_upc_is_linked());
  assert(upc_is_init);
  assert(!upc_is_pthreads);

  void *ptr = bupc_tentative_all_alloc(upc_rank_n, sz);
  assert(ptr); // UPCR allocation failures are fatal
  return ptr;
}


extern void upcxxi_upc_free(void *ptr) {
  assert(upcxxi_upc_is_linked());
  assert(upc_is_init);

  if (upc_is_pthreads) { // need to lookup current thread
    if (bupc_tentative_version_major > 1 || 
        bupc_tentative_version_minor >= 2) {
      bupc_tentative_free(ptr, -1); // auto lookup added in 1.2
    } else {
      static int warned = 0;
      if (!warned) {
        fprintf(stderr, "WARNING: UPC++ interoperability with BUPC in -pthreads mode recommends use of Berkeley UPC v2019.4.5 or newer.\n");
        fflush(stderr);
        warned = 1;
      }
      bupc_tentative_free(ptr, bupc_tentative_mythread());
    }
  } else {
    bupc_tentative_free(ptr, upc_rank_me);
  }
}

extern void upcxxi_upc_all_free(void *ptr) {
  assert(upcxxi_upc_is_linked());
  assert(upc_is_init);
  assert(!upc_is_pthreads);

  bupc_tentative_all_free(ptr, 0);
}


