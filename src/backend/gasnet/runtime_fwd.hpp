#ifndef _8574a911_0646_45ba_8375_d0da989899ec
#define _8574a911_0646_45ba_8375_d0da989899ec

/* This header defines the types that are forward declared in backend_fwd.hpp
 * as needed by *this* specific backend.
 */
 
#include <upcxx/backend_fwd.hpp>

#include <cstdint>

////////////////////////////////////////////////////////////////////////////////

#if !UPCXXI_BACKEND_GASNET
  #error "This header can only be used when the GASNet backend is enabled."
#endif

////////////////////////////////////////////////////////////////////////////////

namespace upcxx {
namespace backend {
  struct team_base {
    std::uintptr_t handle;
  };
}}

#endif
