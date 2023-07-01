#include <upcxx/upcxx.hpp>
bool check() {
  {
    upcxx::persona my_persona;
    upcxx::persona_scope my_scope(my_persona);
  }
  return upcxx::progress_required(upcxx::default_persona_scope());
}
int main() { return check(); }
