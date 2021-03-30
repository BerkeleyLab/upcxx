#include <upcxx/upcxx.hpp>
#include <iostream>
#include <assert.h>

using namespace upcxx;

struct P {
  int x,y,z;
};

future<> sf = make_future();

struct A {
  global_ptr<P> g;
  bool local;
  A(): g(nullptr) {}
  A(global_ptr<P> gp) { 
   this->g = gp;
   // some artifical uses to verify critical operations work:
   // (spec requires these to be progress-level:none)
   assert(gp);
   gp.check();
   global_ptr<int> gpx = upcxx_memberof(gp, x);
   global_ptr<char> gpc = reinterpret_pointer_cast<char>(gpx);
   this->local = gpx.is_local();
   if (!this->local) assert(gpc.where() != upcxx::rank_me());
   auto junk = upcxx::new_<int>();
   upcxx::delete_(junk);
   auto junk2 = upcxx::new_array<int>(4);
   upcxx::delete_array(junk2);
   auto junk3 = upcxx::allocate<int>();
   upcxx::deallocate(junk3);
   int v = serialization_traits<int>::deserialized_value(4);
   bool b = upcxx::in_progress();
   if (master_persona().active_with_caller()) { 
     // this line might be skipped in conduits with a progress thread
     sf = when_all(sf, current_persona().lpc([]() { std::cout<<"hi from "<<upcxx::rank_me()<<std::endl; }));
   }
   persona p;
   persona_scope ps(p);
   assert(&top_persona_scope() == &ps);
   assert(&current_persona() == &p);
  }
  UPCXX_SERIALIZED_VALUES(g)
};

int main() {
  upcxx::init();
  
    auto f = upcxx::rpc(0,[]() {
      global_ptr<P> gp = upcxx::new_<P>();
      A a;
      a.g = gp;
      return a;
    });
    A const &a = f.wait_reference();
    assert(a.g);

  sf.wait();
 
  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout<<"SUCCESS"<<std::endl;
  upcxx::finalize();
  return 0;
}
