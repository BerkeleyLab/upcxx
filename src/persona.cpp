#include <upcxx/persona.hpp>

namespace upcxx {
  persona_scope persona_scope::the_default_dummy_;
  
  namespace detail {
    __thread persona_tls the_persona_tls{};
  }
}
