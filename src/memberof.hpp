#ifndef _7b2d1734_0520_46ad_9c2e_bb2fec19144b
#define _7b2d1734_0520_46ad_9c2e_bb2fec19144b


/**
 * memberof.hpp
 */

#include <upcxx/global_ptr.hpp>
#include <upcxx/diagnostic.hpp>
#include <upcxx/memory_kind.hpp>
#include <upcxx/rpc.hpp>
#include <upcxx/future.hpp>
#include <upcxx/upcxx_config.hpp>

#include <cstddef> // ptrdiff_t
#include <cstdint> // uintptr_t
#include <type_traits>

// UPCXX_GPTYPE(global_ptr<T> gp) yields type global_ptr<T> for any expression gp
#define UPCXX_GPTYPE(gp) \
  ::std::remove_reference<decltype(gp)>::type

// UPCXX_ETYPE(global_ptr<E> gp) yields typename E for any expression gp
#define UPCXX_ETYPE(gp)  typename UPCXX_GPTYPE(gp)::element_type

// UPCXX_ETYPE(global_ptr<E> gp) yields typename E* for any expression gp
#define UPCXX_PTYPE(gp)  typename UPCXX_GPTYPE(gp)::pointer_type

// UPCXX_KTYPE(global_ptr<E,kind> gp) yields kind for any expression gp
#define UPCXX_KTYPE(gp)  (UPCXX_GPTYPE(gp)::kind)

// UPCXX_MTYPE(global_ptr<E> gp, FIELD) yields the type of a
// non-reference member FIELD of E for any expression gp, preserving
// constness
#define UPCXX_MTYPE(gp, FIELD) \
  typename ::std::remove_reference<decltype((::std::declval<UPCXX_PTYPE(gp)>()->FIELD))>::type

// detail::decay_array_gp(gp) turns a global pointer of (possibly
// multidimensional) array type to a global pointer of the array's
// element type
// detail::decayed_gp_t<T, Kind> is the type resulting from applying
// decay_array_gp to a global_ptr<T, Kind>
// NOTE: this code relies on the ability to construct a
// global_ptr<T[N]>, which we then decay to a global_ptr<T>
namespace upcxx {
  namespace detail {
    template<typename T>
    struct decay_array {
      using type = T;
    };

    template<typename T>
    struct decay_array<T[]> {
      using type = typename decay_array<T>::type;
    };

    template<typename T, std::size_t N>
    struct decay_array<T[N]> {
      using type = typename decay_array<T>::type;
    };

    template<typename T, memory_kind KindSet>
    using decayed_gp_t = global_ptr<typename decay_array<T>::type, KindSet>;

    template<typename T, memory_kind KindSet>
    decayed_gp_t<T, KindSet>
    decay_array_gp(global_ptr<T, KindSet> gp) {
      return reinterpret_pointer_cast<typename decay_array<T>::type>(gp);
    }
  }
}

// UPCXX_DECAYED_GP(T, Kind, ...) constructs a global_ptr<T, Kind>
// from the remaining arguments and then invokes decay_array_gp on it
#define UPCXX_DECAYED_GP(T, Kind, ...) \
  ::upcxx::detail::decay_array_gp(::upcxx::global_ptr<T, Kind>(__VA_ARGS__))

// UNSPECIFIED MACRO: This variant is not guaranteed by the spec
// upcxx_memberof_unsafe(global_ptr<T> gp, field-designator)
// This variant assumes T is standard layout, or (C++17) is conditionally supported by the compiler for use in offsetof
// Otherwise, the result is undefined behavior
#define upcxx_memberof_unsafe(gp, FIELD) ( \
  UPCXX_STATIC_ASSERT(offsetof(UPCXX_ETYPE(gp), FIELD) < sizeof(UPCXX_ETYPE(gp)), \
                      "offsetof returned a bogus result. This is probably due to an unsupported non-standard-layout type"), \
  UPCXX_ASSERT_INIT_NAMED("upcxx_memberof_unsafe"), \
  UPCXX_DECAYED_GP(UPCXX_MTYPE(gp, FIELD), UPCXX_KTYPE(gp), \
    ::upcxx::detail::internal_only(), \
    (gp),\
    offsetof(UPCXX_ETYPE(gp), FIELD) \
    ) \
  )
    
// upcxx_memberof(global_ptr<T> gp, field-designator)
// This variant asserts T is standard layout, and thus guaranteed by C++11 to produce well-defined results
#define upcxx_memberof(gp, FIELD) ( \
    UPCXX_ASSERT_INIT_NAMED("upcxx_memberof"), \
    UPCXX_STATIC_ASSERT(::std::is_standard_layout<UPCXX_ETYPE(gp)>::value, \
     "upcxx_memberof() requires a global_ptr to a standard-layout type. Perhaps you want upcxx_memberof_unsafe()?"), \
     upcxx_memberof_unsafe(gp, FIELD) \
  )

// UPCXX_UNIFORM_LOCAL_VTABLES: set to non-zero when the C++ vtables for user types (which live in an .rodata segment)
//   are known to be loaded at the same virtual address across the local_team().
///  This enables shared-memory bypass optimization of memberof_general, even in the presence of virtual bases.
//   Note this only applies to user types (and not, eg libstdc++.so), because std:: has no 
//   public non-static data members in classes with virtual bases that could be passed to this macro.
#ifndef UPCXX_UNIFORM_LOCAL_VTABLES
  #if UPCXX_NETWORK_SMP
    // smp-conduit always spawns using fork(), guaranteeing uniform segment layout
    #define UPCXX_UNIFORM_LOCAL_VTABLES 1
  #elif __PIE__ || __PIC__ 
    // this is conservative: -fPIC alone doesn't generate relocatable vtables,
    // but some compilers only define(__PIC__) for -pie -fpie
    #define UPCXX_UNIFORM_LOCAL_VTABLES 0
  #else
    #define UPCXX_UNIFORM_LOCAL_VTABLES 1
  #endif
#endif

namespace upcxx { namespace detail {

template<typename Obj, memory_kind Kind, typename Mbr, typename Get,
         bool standard_layout = std::is_standard_layout<Obj>::value>
struct memberof_general_dispatch;

template<typename Obj, memory_kind Kind, typename Mbr, typename Get>
struct memberof_general_dispatch<Obj, Kind, Mbr, Get, /*standard_layout=*/true> {
  decltype(upcxx::make_future(std::declval<decayed_gp_t<Mbr,Kind>>()))
  operator()(global_ptr<Obj,Kind> gptr, Get getter) const {
    return upcxx::make_future(
            UPCXX_DECAYED_GP(Mbr, Kind, detail::internal_only(),
                             gptr.UPCXX_INTERNAL_ONLY(rank_),
                             getter(gptr.UPCXX_INTERNAL_ONLY(raw_ptr_)),
                             gptr.UPCXX_INTERNAL_ONLY(heap_idx_)));
  }
};

template<typename Obj, memory_kind Kind, typename Mbr, typename Get>
struct memberof_general_dispatch<Obj, Kind, Mbr, Get, /*standard_layout=*/false> {
  future<decayed_gp_t<Mbr,Kind>>
  operator()(global_ptr<Obj,Kind> gptr, Get getter) const {
    if (gptr.UPCXX_INTERNAL_ONLY(rank_) == upcxx::rank_me()) {  // this rank owns - return a ready future
      return upcxx::make_future(
            UPCXX_DECAYED_GP(Mbr, Kind, detail::internal_only(),
                             gptr.UPCXX_INTERNAL_ONLY(rank_),
                             getter(gptr.UPCXX_INTERNAL_ONLY(raw_ptr_)),
                             gptr.UPCXX_INTERNAL_ONLY(heap_idx_)));
    }
  #if UPCXX_UNIFORM_LOCAL_VTABLES
    else if (gptr.dynamic_kind() == memory_kind::host && gptr.is_local()) { // in local_team host segment
      // safe to directly compute the address of the field, without communication
      Obj *lp = gptr.local();
      Mbr *mbr = getter(lp);
      std::intptr_t offset = reinterpret_cast<std::uintptr_t>(mbr) - reinterpret_cast<std::uintptr_t>(lp);
      return upcxx::make_future(UPCXX_DECAYED_GP(Mbr, Kind, detail::internal_only(), gptr, offset));
    }
  #endif
    else // communicate with owner
    return upcxx::rpc(gptr.UPCXX_INTERNAL_ONLY(rank_), [=]() {
      return UPCXX_DECAYED_GP(Mbr, Kind, detail::internal_only(),
                              gptr.UPCXX_INTERNAL_ONLY(rank_),
                              getter(gptr.UPCXX_INTERNAL_ONLY(raw_ptr_)),
                              gptr.UPCXX_INTERNAL_ONLY(heap_idx_));
    });
  }
};

// Infers all template parameters
template<typename Obj, memory_kind Kind, typename Get, 
         typename Mbr = typename std::remove_pointer<decltype(std::declval<Get>()(std::declval<Obj*>()))>::type>
auto memberof_general_helper(global_ptr<Obj,Kind> gptr, Get getter)
  -> decltype(memberof_general_dispatch<Obj,Kind,Mbr,Get>()(gptr, getter)) {
  UPCXX_ASSERT_INIT_NAMED("upcxx_memberof_general");
  UPCXX_GPTR_CHK(gptr);
  UPCXX_ASSERT(gptr, "Global pointer expression to upcxx_memberof_general() may not be null");
  return memberof_general_dispatch<Obj,Kind,Mbr,Get>()(gptr, getter);
}

}} // namespace upcxx::detail

#define upcxx_memberof_general(gp, FIELD) ( \
  UPCXX_STATIC_ASSERT(!::std::is_reference<decltype(::std::declval<UPCXX_ETYPE(gp)>().FIELD)>::value, \
    "upcxx_memberof_general may not be used on fields with reference type or on array elements. " \
    "Note: if your call is of the form upcxx_memberof_general(obj, array_field[idx]), " \
    "instead do: upcxx_memberof_general(obj, array_field).then([=](global_ptr<T> gp) { return gp + idx; })"), \
  ::upcxx::detail::memberof_general_helper((gp), \
    [](UPCXX_ETYPE(gp) *lptr) { return ::std::addressof(lptr->FIELD); } \
  ) \
)


#endif
