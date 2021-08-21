/* The intrepid user can use this header to get access to the upcxx runtime
 * internals. Notably, it pulls in GASNet-Ex.
 */
#ifndef _86b2c721_5d2d_496d_a301_06af43a17de2
#define _86b2c721_5d2d_496d_a301_06af43a17de2

#if UPCXXI_BACKEND_GASNET_SEQ || UPCXXI_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/runtime_internal.hpp>
#endif

#endif
