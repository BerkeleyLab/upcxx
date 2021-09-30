#include <upcxx/upcxx.hpp>
#include <assert.h>
#include <iostream>
#include <tuple>

using upcxx::global_ptr;

struct incomplete;
struct T{};
enum myenum { enum1, enum2, enum3 };
struct hasref {
  int x;
  int &xref;
  hasref() : x(0), xref(x) {}
  hasref(int&theref) : x(0), xref(theref) {}
};

global_ptr<incomplete> foo() {
  return nullptr;
}

int main() {
  upcxx::init();

  // permitted:
  global_ptr<bool> pb;
  global_ptr<long> pl;
  global_ptr<long*> plp;
  global_ptr<long const *> plcp;
  global_ptr<long volatile *> plvp;
  global_ptr<T> pt;
  global_ptr<T*> ptp;
  global_ptr<T const *> ptcp;
  global_ptr<myenum> pe;
  global_ptr<incomplete> pi;

  // exercise member functions on incomplete types
  global_ptr<incomplete> pi2(nullptr);
  pi = foo();
  global_ptr<incomplete, upcxx::memory_kind::any> pa(pi);
  assert(pa.dynamic_kind() == pi.dynamic_kind());
  assert(pi.is_local());
  incomplete *lip = pi.local();
  assert(!lip);
  upcxx::intrank_t impl_def = pi.where();
  global_ptr<incomplete> pi3 = upcxx::to_global_ptr(lip);
  global_ptr<incomplete> pi4 = upcxx::try_global_ptr(lip);
  assert(pi3 == pi);
  assert(pi.is_null());
  assert(!pi);
  assert(pi3 == pi4);
  assert(!(pi3 != pi4));
  assert((pi3 <= pi4));
  assert((pi3 >= pi4));
  assert(!(pi3 < pi4));
  assert(!(pi3 > pi4));

  assert(!(std::less<global_ptr<incomplete>>()(pi3,pi4)));
  assert((std::less_equal<global_ptr<incomplete>>()(pi3,pi4)));
  assert(!(std::greater<global_ptr<incomplete>>()(pi3,pi4)));
  assert((std::greater_equal<global_ptr<incomplete>>()(pi3,pi4)));
  assert(std::hash<global_ptr<incomplete>>()(pi3) == 
         std::hash<global_ptr<incomplete>>()(pi4));
  if (!upcxx::rank_me()) std::cout << pi3 << std::endl;

  global_ptr<void> pc1 = upcxx::static_pointer_cast<void>(pi);
  global_ptr<void> pc2 = upcxx::reinterpret_pointer_cast<void>(pi);

  pi4 = upcxx::static_kind_cast<upcxx::memory_kind::host>(pa);
  pi4 = upcxx::dynamic_kind_cast<upcxx::memory_kind::host>(pa);

  global_ptr<std::tuple<int &>> ptir; // ptr to type with embedded ref is ok

#if ERROR
  // forbidden types:
  global_ptr<long&> plr;
  global_ptr<const long> pcl;
  global_ptr<volatile long> pvl;

  // forbidden functions:
  pi++; 
  pi-=10; 
  size_t s = pi2 - pi;
  
  // forbidden macros:
  void *mp1 = upcxx_memberof(pi, foo);
  auto f =  upcxx_memberof_general(pi, foo);

  auto mp21 = upcxx_experimental_memberof_unsafe(po, xref);
 
  // memberof_general on reference-typed field
  global_ptr<hasref> po = upcxx::new_<hasref>();
  upcxx::future<global_ptr<int>> fmp = upcxx_memberof_general(po, x);
  global_ptr<int> mp = fmp.wait();

  upcxx::future<global_ptr<int>> fmp2 = upcxx_memberof_general(po, xref);
  global_ptr<int> mp2 = fmp2.wait();
  assert(mp == mp2);

  global_ptr<int> sheapvar = upcxx::new_<int>(42);
  global_ptr<hasref> po2 = upcxx::new_<hasref>(*sheapvar.local());
  upcxx::future<global_ptr<int>> fmp3 = upcxx_memberof_general(po2, xref);
  global_ptr<int> mp3 = fmp3.wait();
  assert(sheapvar == mp3);

  int stackvar = 42;
  global_ptr<hasref> po3 = upcxx::new_<hasref>(stackvar);
  upcxx::future<global_ptr<int>> fmp4 = upcxx_memberof_general(po3, xref);
  global_ptr<int> mp4 = fmp4.wait();

#endif

  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
}
