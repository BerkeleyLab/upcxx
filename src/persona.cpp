#include <upcxx/persona.hpp>
#include <upcxx/backend.hpp>

namespace upcxx {
  persona_scope persona_scope::the_default_dummy_;
  
  namespace detail {
    __thread persona_tls the_persona_tls{};

    future<> get_ready_empty_future_wrapper() {
      return backend::get_ready_empty_future<>();
    }
  }
}
