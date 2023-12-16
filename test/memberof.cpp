#include <stddef.h>
#include <type_traits>
#include <iostream>
#include <upcxx/upcxx.hpp>

#include "util.hpp"

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

#if __cplusplus <= 201703
using std::is_pod;
#else 
// C++20 deprecates this utility for no good reason..
template<typename T>
struct is_pod {
  static constexpr bool value = std::is_standard_layout<T>::value && std::is_trivial<T>::value;
};
#endif

#define T(t) if (!upcxx::rank_me()) do { \
    std::cout << "\n *** " << #t << " *** " << std::endl; \
    std::cout << "std::is_standard_layout<" #t "> = " \
              << std::is_standard_layout<t>::value << std::endl; \
    std::cout << "std::is_trivial<" #t "> = " \
              << std::is_trivial<t>::value << std::endl; \
    std::cout << "is_pod<" #t "> = " \
              << is_pod<t>::value << std::endl; \
  } while (0)

#ifndef _STRINGIFY
#define _STRINGIFY(x) #x
#endif
#define SA(prop) static_assert(prop, _STRINGIFY(prop))

template<typename T, bool standard_layout, bool trivial, bool pod>
void typeprops() {
  SA(std::is_standard_layout<T>::value == standard_layout);
  SA(std::is_trivial<T>::value == trivial);
  SA(is_pod<T>::value == pod);
}

struct tricksy { // standard layout, trivial, POD
  static void _() { typeprops<tricksy,true,true,true>(); }
  char z;
  #ifndef __PGI // PGI offsetof() known to be broken wrt operator& in many installs
  void operator&() { 
    assert(0 && "tricksy::operator& invoked!");    
  }
  #endif
};

struct A {  // standard layout, trivial, POD
  static void _() { typeprops<A,true,true,true>(); }
  char f0;
  char f1;
  char f2;
  double x;
  tricksy z;
};

struct B {  // standard layout, NOT trivial, NOT POD
  static void _() { typeprops<B,true,false,false>(); }
  char f0;
  char f1;
  char f2;
  double x;
  tricksy z;
  B() {}
};

struct C {  // NOT standard layout, trivial, NOT POD
  static void _() { typeprops<C,false,true,false>(); }
  char f0;
  char f1;
  char f2;
  tricksy z;
  protected:
  double x;
};

struct D {  // NOT standard layout, NOT trivial, NOT POD
  static void _() { typeprops<D,false,false,false>(); }
  char f0;
  char f1;
  char f2;
  double x;
  char &f;
  tricksy z;
  D() : f(f0) {}
};

struct V0 { 
  char f0; 
  virtual void foo() = 0;
}; 
  
struct V1 : public virtual V0 { 
  char f1; 
  void foo() {}
}; 
  
struct V2 : public virtual V0 { 
  char f2; 
  tricksy z;
}; 
  
struct V : public V1, public V2 { // NOT standard layout, NOT trivial, NOT POD
  static void _() { typeprops<V,false,false,false>(); }
  void foo() {}
  V() {}
}; 

template<typename T, bool is_const = std::is_const<T>::value>
struct match_const;
template<typename T>
struct match_const<T, true> {
  using char_type = const char;
  using tricksy_type = const tricksy;
};
template<typename T>
struct match_const<T, false> {
  using char_type = char;
  using tricksy_type = tricksy;
};

// this test always compiles statically with a device to detect compile-time bugs, 
// even in builds where no device is runtime-enabled (tracked by gpu_enabled)
bool gpu_enabled;
upcxx::device_allocator<Device> gpu_alloc;

namespace perverse {
  namespace std { // check for insufficiently qualified macro use of ::std
    void addressof() { assert(0 && "oops!"); }
    template<typename T>
    struct is_standard_layout {};
    template<typename T>
    struct remove_reference {};
    template<typename T>
    void declval();
  }
  template<typename C, typename GP>
  bool check(GP gp) {
    upcxx::global_ptr<C> gp_f1 = upcxx_memberof(gp, f1);
    upcxx::global_ptr<C> gp_f2 = upcxx_experimental_memberof_unsafe(gp, f2);
    return gp_f1 && gp_f2;
  }
  template<typename C, typename GP>
  bool check_general(GP gp) {
    auto fut1 = upcxx_memberof_general(gp, f1);
    upcxx::global_ptr<C> gp_f1 = fut1.wait();
    return !!gp_f1;
  }
} // perverse


template<typename T, bool stdlayout>
struct calc { static void _(upcxx::global_ptr<T> gp_o) {
  using char_t = typename match_const<T>::char_type;
  using tricksy_t = typename match_const<T>::tricksy_type;

  if (!upcxx::rank_me()) std::cout << "Testing standard layout..." << std::endl;
  upcxx::global_ptr<char_t> gp_f0 = upcxx_memberof(gp_o, f0);
  upcxx::global_ptr<char_t> gp_f1 = upcxx_memberof(gp_o, f1);
  upcxx::global_ptr<char_t> gp_f2 = upcxx_memberof(gp_o, f2);
  assert(gp_f0 && gp_f1 && gp_f2);
  upcxx::global_ptr<char_t> gp_base = upcxx::reinterpret_pointer_cast<char_t>(gp_o);
  ssize_t d0 = gp_f0 - gp_base;
  ssize_t d1 = gp_f1 - gp_base;
  ssize_t d2 = gp_f2 - gp_base;
  if (!upcxx::rank_me()) {
    std::cout << "memberof offsets:         d0=" << d0 << " d1=" << d1 << " d2=" << d2 << std::endl;
  }
  upcxx::barrier();
  assert(d0 == 0 && d1 == 1 && d2 == 2);
  upcxx::barrier();

  #if 1
  // test some non-trivial expressions
  volatile int zero = 0;

  upcxx::global_ptr<char_t> e1_test = upcxx_memberof(gp_o+zero, f1);
  assert(e1_test == gp_f1);

  upcxx::global_ptr<char_t> e2_test = upcxx_memberof((gp_o), f1);
  assert(e2_test == gp_f1);

  using gp_T_any = upcxx::global_ptr<T, upcxx::memory_kind::any>;
  using gp_F_any = upcxx::global_ptr<char_t, upcxx::memory_kind::any>;
  gp_T_any gp_o_any = gp_o;
  assert(upcxx_memberof((gp_o_any + zero), f0).kind == upcxx::memory_kind::any);
  gp_F_any e3_test = upcxx_memberof((gp_o_any + zero), f1);
  assert(e3_test == gp_f1);

  upcxx::global_ptr<T> const gp_o_c = gp_o; 
  upcxx::global_ptr<char_t> e4_test = upcxx_memberof(gp_o_c, f1);
  assert(e4_test == gp_f1);
  upcxx::barrier();
  #endif

  #if 1
  int se = 0; // test for single-evaluation
  auto func = [&](){se++; return gp_o;};
  upcxx::global_ptr<char_t> se_test = upcxx_memberof(func(), f0);
  assert(se == 1 && se_test);
  upcxx::barrier();
  #endif

  #if 1
  // test misused address-of
  upcxx::global_ptr<tricksy_t> gp_tz = upcxx_memberof(gp_o, z);
  upcxx::global_ptr<char_t> gp_tz_ = upcxx::reinterpret_pointer_cast<char_t>(gp_tz);
  ssize_t dz = gp_tz_ - gp_base;
  assert(dz >= 0 && (size_t)dz < sizeof(T));
  upcxx::global_ptr<char_t> gp_tzz = upcxx_memberof(gp_o, z.z);
  assert(gp_tz_ == gp_tzz);
  #endif

  #if 1
  // test memory kinds
  upcxx::global_ptr<T,Device::kind> gpu_o;
  if (gpu_enabled) {
    gpu_o = gpu_alloc.allocate<T>(1);
  }
  if (gpu_enabled) { // deliberately separated to discourage optimizations that might hide static errors for non-CUDA
    upcxx::global_ptr<char_t,Device::kind> gpu_f0 = upcxx_memberof(gpu_o, f0);
    upcxx::global_ptr<char_t,Device::kind> gpu_f1 = upcxx_memberof(gpu_o, f1);
    upcxx::global_ptr<char_t,Device::kind> gpu_f2 = upcxx_memberof(gpu_o, f2);
    assert(gpu_f0 && gpu_f1 && gpu_f2);
    upcxx::global_ptr<char_t,Device::kind> gpu_base = upcxx::reinterpret_pointer_cast<char_t>(gpu_o);
    ssize_t gd0 = gpu_f0 - gpu_base;
    ssize_t gd1 = gpu_f1 - gpu_base;
    ssize_t gd2 = gpu_f2 - gpu_base;
    if (!upcxx::rank_me()) {
      std::cout << "gpu offsets:              d0=" << gd0 << " d1=" << gd1 << " d2=" << gd2 << std::endl;
    }
    upcxx::barrier();
    assert(gd0 == 0 && gd1 == 1 && gd2 == 2);
    assert(gd0 == d0 && gd1 == d1 && gd2 == d2); // not guaranteed by C++, but true for all known impls
    upcxx::barrier();
     
    gpu_alloc.deallocate(gpu_o);
  }
  #endif

  perverse::check<char_t>(gp_o);

} };
template<typename T>
struct calc<T,false>{ static void _(upcxx::global_ptr<T> gp_o){
  using char_t = typename match_const<T>::char_type;
  //using tricksy_t = typename match_const<T>::tricksy_type;

  if (!upcxx::rank_me()) std::cout << "Testing non-standard layout..." << std::endl;
  // upcxx_experimental_memberof_unsafe is deliberately unspecified
  upcxx::global_ptr<char_t> gp_f0 = upcxx_experimental_memberof_unsafe(gp_o, f0);
  upcxx::global_ptr<char_t> gp_f1 = upcxx_experimental_memberof_unsafe(gp_o, f1);
  upcxx::global_ptr<char_t> gp_f2 = upcxx_experimental_memberof_unsafe(gp_o, f2);
  assert(gp_f0 && gp_f1 && gp_f2);
  upcxx::global_ptr<char_t> gp_base = upcxx::reinterpret_pointer_cast<char_t>(gp_o);
  ssize_t d0 = gp_f0 - gp_base;
  ssize_t d1 = gp_f1 - gp_base;
  ssize_t d2 = gp_f2 - gp_base;
  if (!upcxx::rank_me()) {
    std::cout << "memberof_unsafe offsets:  d0=" << d0 << " d1=" << d1 << " d2=" << d2 << std::endl;
  }
  upcxx::barrier();
  assert(d0 == 0 && d1 == 1 && d2 == 2); // not guaranteed by C++, but true for all known impls
  upcxx::barrier();
} };

template<typename T>
void check_general(bool has_virtual) {
  using char_t = typename match_const<T>::char_type;
  using tricksy_t = typename match_const<T>::tricksy_type;

  upcxx::global_ptr<T> gp_o;
  if (!upcxx::rank_me()) gp_o = upcxx::new_<T>();
  gp_o = upcxx::broadcast(gp_o, 0).wait();
  assert(gp_o);
  if (!upcxx::rank_me()) std::cout << "Testing memberof_general..." << std::endl;

  auto fut0 = upcxx_memberof_general(gp_o, f0);
  auto fut1 = upcxx_memberof_general(gp_o, f1);
  auto fut2 = upcxx_memberof_general(gp_o, f2);
  bool all_ready = fut0.is_ready() && fut1.is_ready() && fut2.is_ready();
  // the following is not guaranteed by spec, just tests the known implementation
  bool expect_ready = std::is_standard_layout<T>::value
                      || gp_o.where() == upcxx::rank_me()
                      #if UPCXXI_UNIFORM_LOCAL_VTABLES
                      || gp_o.is_local()
                      #endif
                      ;
  if (expect_ready) assert(all_ready);
  else assert(!all_ready);

  upcxx::global_ptr<char_t> gp_f0 = fut0.wait();
  upcxx::global_ptr<char_t> gp_f1 = fut1.wait();
  upcxx::global_ptr<char_t> gp_f2 = fut2.wait();
  assert(gp_f0 && gp_f1 && gp_f2);

  upcxx::global_ptr<char_t> gp_base = upcxx::reinterpret_pointer_cast<char_t>(gp_o);
  ssize_t d0 = gp_f0 - gp_base;
  ssize_t d1 = gp_f1 - gp_base;
  ssize_t d2 = gp_f2 - gp_base;
  if (!upcxx::rank_me()) {
    std::cout << "memberof_general offsets: d0=" << d0 << " d1=" << d1 << " d2=" << d2 << std::endl;
  }
  upcxx::barrier();
  assert((size_t)d0 < sizeof(T) && (size_t)d1 < sizeof(T) && (size_t)d2 < sizeof(T));
  assert(d0 != d1 && d0 != d2 && d1 != d2);
  upcxx::barrier();

  #if 1
  // test misused address-of
  auto fz = upcxx_memberof_general(gp_o, z);
  upcxx::global_ptr<tricksy_t> gp_tz = fz.wait();
  upcxx::global_ptr<char_t> gp_tz_ = upcxx::reinterpret_pointer_cast<char_t>(gp_tz);
  ssize_t dz = gp_tz_ - gp_base;
  assert(dz >= 0 && (size_t)dz < sizeof(T));
  auto fzz = upcxx_memberof_general(gp_o, z.z);
  upcxx::global_ptr<char_t> gp_tzz = fzz.wait();
  assert(gp_tz_ == gp_tzz);
  #endif

  perverse::check_general<char_t>(gp_o);

  // objects with virtual bases on a CUDA device cannot be manipulated by the CPU
  // see: https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#virtual-functions
  if (has_virtual) return; 

  #if 1
  // test memory kinds
  upcxx::global_ptr<T,Device::kind> gpu_o;
  if (gpu_enabled) {
    if (!upcxx::rank_me()) gpu_o = gpu_alloc.allocate<T>(1);
    gpu_o = upcxx::broadcast(gpu_o, 0).wait();
  }
  if (gpu_enabled) { // deliberately separated to discourage optimizations that might hide static errors for non-CUDA
    auto fut0 = upcxx_memberof_general(gpu_o, f0);
    auto fut1 = upcxx_memberof_general(gpu_o, f1);
    auto fut2 = upcxx_memberof_general(gpu_o, f2);
    // the following is not guaranteed by spec, just tests the known implementation
    bool expect_ready = std::is_standard_layout<T>::value || gpu_o.where() == upcxx::rank_me();
    bool all_ready = fut0.is_ready() && fut1.is_ready() && fut2.is_ready();
    if (expect_ready) assert(all_ready);
    else assert(!all_ready);
    upcxx::global_ptr<char_t,Device::kind> gpu_f0 = fut0.wait();
    upcxx::global_ptr<char_t,Device::kind> gpu_f1 = fut1.wait();
    upcxx::global_ptr<char_t,Device::kind> gpu_f2 = fut2.wait();
    assert(gpu_f0 && gpu_f1 && gpu_f2);
    upcxx::global_ptr<char_t,Device::kind> gpu_base = upcxx::reinterpret_pointer_cast<char_t>(gpu_o);
    ssize_t gd0 = gpu_f0 - gpu_base;
    ssize_t gd1 = gpu_f1 - gpu_base;
    ssize_t gd2 = gpu_f2 - gpu_base;
    if (!upcxx::rank_me()) {
      std::cout << "gpu general offsets:      d0=" << gd0 << " d1=" << gd1 << " d2=" << gd2 << std::endl;
    }
    upcxx::barrier();
    assert(gd0 != gd1 && gd0 != gd2 && gd1 != gd2);
    assert((size_t)gd0 < sizeof(T) && (size_t)gd1 < sizeof(T) && (size_t)gd2 < sizeof(T));
    assert(gd0 == d0 && gd1 == d1 && gd2 == d2); // not guaranteed by C++, but true for all known impls
    upcxx::barrier();
     
    if (!upcxx::rank_me()) gpu_alloc.deallocate(gpu_o);
  }
  #endif
}

template<typename T>
void check() {
  upcxx::barrier();
  constexpr bool stdlayout = std::is_standard_layout<T>::value;
  T myo;
  ssize_t d1 = uintptr_t(&myo.f0) - uintptr_t(&myo);
  ssize_t d2 = uintptr_t(&myo.f1) - uintptr_t(&myo);
  ssize_t d3 = uintptr_t(&myo.f2) - uintptr_t(&myo);
  ssize_t o1=0,o2=0,o3=0;
  o1 = offsetof(T, f0);
  o2 = offsetof(T, f1);
  o3 = offsetof(T, f2);
  if (!upcxx::rank_me()) {
    std::cout << "delta f0=" << d1 << " offsetof(f0)=" << o1 << std::endl;
    std::cout << "delta f1=" << d2 << " offsetof(f1)=" << o2 << std::endl;
    std::cout << "delta f2=" << d3 << " offsetof(f2)=" << o3 << std::endl;
    if (stdlayout) {
      if (o1 != d1 || o2 != d2 || o3 != d3)
        std::cerr << "ERROR: delta/offsetof mismatch" << std::endl;
      if (o1 != 0)
        std::cerr << "ERROR: first field of standard layout class is not pointer-interconvertible (see C++ [basic.compound])" << std::endl;
    } else {
      if (o1 != d1 || o2 != d2 || o3 != d3)
        std::cerr << "WARNING: delta/offsetof mismatch (non-standard-layout)" << std::endl;
    }
  }
  upcxx::barrier();

  upcxx::global_ptr<T> gp_o;
  if (!upcxx::rank_me()) gp_o = upcxx::allocate<T>(1); // deliberately use uninitialized storage
  gp_o = upcxx::broadcast(gp_o, 0).wait();
  assert(gp_o);
  calc<T, stdlayout>::_( gp_o );
  calc<const T, stdlayout>::_( gp_o );

  check_general<T>(false);
  check_general<const T>(false);

  upcxx::barrier();
}

int main() {
  upcxx::init();
  print_test_header();

  gpu_alloc = upcxx::make_gpu_allocator<Device>(2UL<<20);
  gpu_enabled = gpu_alloc.is_active(); // check if we found a GPU

  T(A); 
  check<A>();
  T(B); 
  check<B>();
  T(C); 
  check<C>();
  T(D); 
  check<D>();

  T(V); 
  check_general<V>(true);
  check_general<const V>(true);

  gpu_alloc.destroy();

  print_test_success();
  upcxx::finalize();
}

