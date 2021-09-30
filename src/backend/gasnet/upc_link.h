// upc_link.h
// This header provides utilities for managing interoperability with the Berkeley UPC Runtime (aka upcr)

#ifndef __UPC_LINK_H

#include <gasnet.h>

#ifdef __cplusplus
extern "C" {
#endif

// returns non-zero iff Berkeley UPC Runtime was linked in
// otherwise, returns 0 and all other functions declared in this header have undefined behavior.
extern int upcxxi_upc_is_linked(void);

// returns non-zero iff Berkeley UPC Runtime is using -pthreads mode,
// which potentially maps multiple UPC threads to this process
extern int upcxxi_upc_is_pthreads(void);

// Initialize the Berkeley UPC runtime system (if needed) 
// and return the GASNet-EX objects which are SHARED with upcr
extern void upcxxi_upc_init(
                gex_Client_t           *client_p,
                gex_EP_t               *ep_p,
                gex_TM_t               *tm_p
            );

// allocate sz bytes from the libupcr shared heap
// the _all variant is collective and sz must be single-valued
// out-of-memory failures are fatal.
extern void *upcxxi_upc_alloc(size_t sz);
extern void *upcxxi_upc_all_alloc(size_t sz);

// release storage previously allocated using upcxxi_upc_alloc()
extern void upcxxi_upc_free(void *ptr);
// collectively release storage previously allocated using upcxxi_upc_all_alloc()
extern void upcxxi_upc_all_free(void *ptr);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __UPC_LINK_H
