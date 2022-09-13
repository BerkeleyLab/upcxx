#ifndef _661bba4d_9f90_4fbe_b617_4474e1ed8cab
#define _661bba4d_9f90_4fbe_b617_4474e1ed8cab

#include <upcxx/diagnostic.hpp>
#include <upcxx/upcxx_config.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <tuple>
#include <utility>
#include <new> // launder

// RTTI support
#ifndef UPCXXI_HAVE_RTTI
#define UPCXXI_HAVE_RTTI (__GXX_RTTI || __cpp_rtti)
#endif
#if UPCXXI_HAVE_RTTI
#include <typeinfo> // typeid
#endif

#include <cstdlib> // posix_memalign

// UPCXXI_RETURN_DECLTYPE(type): use this inplace of "-> decltype(type)" so that
// for compilers which choke on such return types (icc) it can be elided in
// the presence of C++14.
#if !defined(__INTEL_COMPILER) || __cplusplus <= 201199L
  #define UPCXXI_RETURN_DECLTYPE(...) -> decltype(__VA_ARGS__)
#else
  #define UPCXXI_RETURN_DECLTYPE(...)
#endif

namespace upcxx {
namespace detail {
  //////////////////////////////////////////////////////////////////////
  // detail::nop_function, detail::constant_function removed post
  // 2021.3.0 release

  //////////////////////////////////////////////////////////////////////////////
  // detail::memcpy_aligned

  template<std::size_t align>
  inline void memcpy_aligned(void *dst, void const *src, std::size_t sz) noexcept {
    UPCXX_ASSERT((uintptr_t)src % align == 0);
    UPCXX_ASSERT((uintptr_t)dst % align == 0);
  #if UPCXXI_HAVE___BUILTIN_ASSUME_ALIGNED
    std::memcpy(__builtin_assume_aligned(dst, align),
                __builtin_assume_aligned(src, align), sz);
  #else
    std::memcpy(dst, src, sz);
  #endif
  }

  //////////////////////////////////////////////////////////////////////////////
  // detail::launder & detail::launder_unconstructed

  // *Hopefully*, make it impossible for the compiler to prove to itself that
  // that `p` doesn't point to an object of type T. This can be used when you
  // know the underlying bytes encode a valid T, but it hasn't been actually
  // constructed as such.
  template<typename T>
  T* launder_unconstructed(T *p) noexcept {
    #if __INTEL_COMPILER
      // issue 400: the intel compiler ICEs on gnu-style extended asm below,
      // so use this more convoluted variant that means the same thing:
      // (Note in particular that C++ [basic.lval] aliasing rules permit
      // modification via char type, which we exploit here)
      union faketype { T t; volatile char a[sizeof(T)]; };
      asm volatile ("" : "+m"(((faketype *)p)->a) : "rm"(p) : );
    #else
      // the following says we are "killing" the contents of the bytes in memory
      // for the object pointed-to by p, preventing the compiler analysis from reasoning
      // about its contents across this point.
      asm volatile ("" : "+m"(*(volatile char (*)[sizeof(T)])p) : "rm"(p) : );
    #endif

    return p;
  }

  #if __cpp_lib_launder >= 201606 /* std::launder is not reliably available in C++17 (eg clang 8) */ \
      && !__PGI /* issue #289/#297: std::launder broken for all known versions of PGI */
    template<typename T>
    constexpr T* launder(T *p) {
      return std::launder(p);
    }
  #elif UPCXXI_HAVE___BUILTIN_LAUNDER
    template<typename T>
    constexpr T* launder(T *p) {
      return __builtin_launder(p);
    }
  #else
    template<typename T>
    T* launder(T *p) noexcept {
      return launder_unconstructed(p);
    }
  #endif
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::construct_default: Default constructs a T object if possible,
  // otherwise cheat (UB!) to convince compiler such an object already existed.

  namespace help {
    template<typename T>
    T* construct_default(void *spot, std::true_type deft_ctor) {
      return reinterpret_cast<T*>(::new(spot) T); // extra reinterpret_cast needed for T = U[n] (compiler bug?!)
    }
    template<typename T>
    T* construct_default(void *spot, std::false_type deft_ctor) {
      return detail::launder_unconstructed<T>(reinterpret_cast<T*>(spot));
    }
  }
  
  template<typename T>
  T* construct_default(void *spot) {
    return help::template construct_default<T>(
      spot,
      std::integral_constant<bool, std::is_default_constructible<T>::value>()
    );
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::construct_trivial: Constructs T objects from raw bytes. If
  // T doesn't have the appropriate constructors, this will cheat (UB!) by
  // copying the bytes and then *blessing* the memory as holding a valid T.
  
  namespace help {
    template<typename T>
    T* construct_trivial(void *dest, const void *src, std::true_type deft_ctor, std::true_type triv_copy) {
      using T1 = typename std::remove_const<T>::type;
      T1 *ans = reinterpret_cast<T1*>(::new(dest) T1);
      detail::memcpy_aligned<alignof(T1)>(ans, src, sizeof(T1));
      #if UPCXXI_ISSUE400_WORKAROUND
        // issue #400: memcpy of any type of object is always insufficient to construct a valid object, as it does not
        // perform any of the actions described in [intro.object]/1 that the standard specifies create an object, even in
        // the case of TriviallyCopyable types. P0593 would change this behavior, but has not been accepted into any
        // standard as of writing this comment. The proposed std::start_lifetime_as<T> may be necessary to avoid UB even
        // with this change to the standard. At the present time, the C++ standard does not have mechanisms to avoid this 
        // UB.
        //
        // Additionally, a call to std::launder is necessary to avoid an additional case UB for pointer aliasing even if
        // an object were to be properly constructed as per the standard. As std::launder requires compiler support with a
        // builtin such as __builtin_launder, detail::launder_unconstructed attempts to backport a combination of
        // std::launder and the proposed std::start_lifetime_as<T> as best as possible using inline asm to interrupt the
        // compiler's analysis. Avoiding this UB is only possible in C++17 or by using compiler builtins if available.
        //
        // These undefined behaviors are known to cause incorrect optimizations with GCC 7, 8, and 9 at -O2+.
        //
        // Due to a lack of compiler support, the UB is only worked around when it is known to cause problems as the inline
        // asm can prevent more optimizations than intended.
        return detail::launder_unconstructed<T1>(ans);
      #else
        return ans;
      #endif
    }
    template<typename T>
    T* construct_trivial(void *dest, const void *src, std::true_type deft_ctor, std::false_type triv_copy) {
      using T1 = typename std::remove_const<T>::type;
      ::new(dest) T1;
      detail::memcpy_aligned<alignof(T1)>(dest, src, sizeof(T1));
      return detail::launder<T1>(reinterpret_cast<T1*>(dest));
    }
    template<typename T, bool any>
    T* construct_trivial(void *dest, const void *src, std::false_type deft_ctor, std::integral_constant<bool,any> triv_copy) {
      detail::memcpy_aligned<alignof(T)>(dest, src, sizeof(T));
      return detail::launder_unconstructed(reinterpret_cast<T*>(dest));
    }
    
    template<typename T>
    T* construct_trivial(void *dest, const void *src, std::size_t n, std::true_type deft_ctor, std::true_type triv_copy) {
      using T1 = typename std::remove_const<T>::type;
      T1 *ans = nullptr;
      for(std::size_t i=n; i != 0;)
        ans = ::new((T1*)dest + --i) T1;
      detail::memcpy_aligned<alignof(T1)>(ans, src, n*sizeof(T1));
      return ans;
    }
    template<typename T>
    T* construct_trivial(void *dest, const void *src, std::size_t n, std::true_type deft_ctor, std::false_type triv_copy) {
      using T1 = typename std::remove_const<T>::type;
      for(std::size_t i=n; i != 0;)
        ::new((T1*)dest + --i) T1;
      detail::memcpy_aligned<alignof(T1)>(dest, src, n*sizeof(T1));
      return detail::launder<T1>(reinterpret_cast<T1*>(dest));
    }
    template<typename T, bool any>
    T* construct_trivial(void *dest, const void *src, std::size_t n, std::false_type deft_ctor, std::integral_constant<bool,any> triv_copy) {
      detail::memcpy_aligned<alignof(T)>(dest, src, n*sizeof(T));
      return detail::launder_unconstructed(reinterpret_cast<T*>(dest));
    }
  }
  
  template<typename T>
  T* construct_trivial(void *dest, const void *src) {
    return help::template construct_trivial<T>(
        dest, src, 
        std::integral_constant<bool, std::is_default_constructible<T>::value>(),
        std::integral_constant<bool, std::is_trivially_copyable<T>::value>()
      );
  }
  template<typename T>
  T* construct_trivial(void *dest, const void *src, std::size_t n) {
    return help::template construct_trivial<T>(
        dest, src, n,
        std::integral_constant<bool, std::is_default_constructible<T>::value>(),
        std::integral_constant<bool, std::is_trivially_copyable<T>::value>()
      );
  }

  //////////////////////////////////////////////////////////////////////////////
  // detail::construct_trivial_into_storage: Similar to construct_trivial,
  // but constructs a T into a serialization_storage_wrapper. T must be
  // DefaultConstructible.

  namespace help {
    template<typename T, typename Storage>
    T* construct_trivial_into_storage(Storage storage, const void *src,
                                      std::true_type triv_copy) {
      // Default construct a T, then memcpy into it.
      T *ans = storage.construct();
      detail::memcpy_aligned<alignof(T)>(ans, src, sizeof(T));
      #if UPCXXI_ISSUE400_WORKAROUND
        // See construct_trivial() for a detailed discussion of issue #400.
        return detail::launder_unconstructed<T>(ans);
      #else
        return ans;
      #endif
    }
    template<typename T, typename Storage>
    T* construct_trivial_into_storage(Storage storage, const void *src,
                                      std::false_type triv_copy) {
      // Default construct a T, then memcpy into it.
      T *ans = storage.construct();
      detail::memcpy_aligned<alignof(T)>(ans, src, sizeof(T));
      return detail::launder<T>(ans);
    }
  }

  template<typename T, typename Storage>
  T* construct_trivial_into_storage(Storage storage, const void *src) {
    using T1 = typename std::remove_const<T>::type;
    static_assert(std::is_default_constructible<T1>::value,
                  "Deserializing a TriviallySerializable type T into "
                  "storage requires T to be DefaultConstructible");
    return help::construct_trivial_into_storage<T1>(
      storage, src,
      std::integral_constant<bool, std::is_trivially_copyable<T1>::value>()
    );
  }

  //////////////////////////////////////////////////////////////////////////////
  // detail::destruct

  template<typename T>
  struct destruct_dispatch {
    void operator()(T &x) const noexcept { x.~T(); }
  };
  template<typename T, std::size_t n>
  struct destruct_dispatch<T[n]> {
    void operator()(T (&x)[n]) const noexcept {
      for(std::size_t i=0; i != n; i++)
        destruct_dispatch<T>()(x[i]);
    }
  };
  
  template<typename T>
  void destruct(T &x) noexcept { destruct_dispatch<T>()(x); }
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::is_aligned

  inline bool is_aligned(void const *x, std::size_t align) {
    UPCXX_ASSERT((align & (align-1)) == 0, "align must be a power of 2");
    return 0 == (reinterpret_cast<std::uintptr_t>(x) & (align-1));
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::alloc_aligned

  inline void* alloc_aligned(std::size_t size, std::size_t align) noexcept {
    UPCXX_ASSERT(
      align != 0 && (align & (align-1)) == 0,
      "upcxx::detail::alloc_aligned: Invalid align="<<align
    );

    // Note: do not use std::aligned_alloc (C++17) since it does not *portably*
    // support extended alignments (those greater than std::max_align_t), and
    // thus is no better than ::operator new().

    // Make align = max(align, sizeof(void*))
    align -= 1;
    align |= sizeof(void*)-1;
    align += 1;
    // Round size up to a multiple of alignment
    size = (size + align-1) & -align;
    
    void *p;
    int err = posix_memalign(&p, align, size);
    UPCXX_ASSERT_ALWAYS(err == 0,
      "upcxx::detail::alloc_aligned: posix_memalign(align="<<align<<", size="<<size<<"): failed with return="<<err
    );
    UPCXX_ASSERT(is_aligned(p, align));
    return p;
  }

  //////////////////////////////////////////////////////////////////////////////
  // xaligned_storage: like std::aligned_storage::type except:
  //   1. Supports extended alignemnts greater than alignof(std::max_align_t)
  //   2. Does not guarantee that `sizeof(xaligned_storage<S,A>::type) == S` since it might
  //      include padding space to achieve the alignemnt dynamically.
  //   3. Does not guarantee to be a trivial type.
  //   4. Access to the aligned memory is provided by the `storage()` member,
  //      alignment of the xaligned_storage object itself is unspecified.
  //
  // The intenteded use case is for small stack-allocated serialization buffers.
  // The other potential use case is to hold overly-aligned types, but currently
  // we have none so for that usage we stick to `std::aligned_storage<>::type`.
  
  template<std::size_t size, std::size_t align,
           bool valid = 0 == (align & (align-1)),
           bool extended = (align > alignof(std::max_align_t))>
  struct xaligned_storage;

  template<std::size_t size, std::size_t align>
  struct xaligned_storage<size, align, /*valid=*/true, /*extended=*/false> {
    typename std::aligned_storage<(size + align-1) & -align, align>::type storage_;

    void const* storage() const noexcept { 
      UPCXX_ASSERT(is_aligned(&storage_, align));
      return &storage_; 
    }
    void*       storage()       noexcept { 
      UPCXX_ASSERT(is_aligned(&storage_, align));
      return &storage_; 
    }
  };
  
  template<std::size_t size, std::size_t align>
  struct xaligned_storage<size, align, /*valid=*/true, /*extended=*/true> {
    char xbuf_[size + align-1];
    
    void const* storage() const noexcept {
      std::uintptr_t u = reinterpret_cast<std::uintptr_t>(&xbuf_);
      void const *p = &xbuf_[-u & (align-1)];
      UPCXX_ASSERT(is_aligned(p, align));
      return p;
    }
    void*       storage()       noexcept {
      std::uintptr_t u = reinterpret_cast<std::uintptr_t>(&xbuf_);
      void       *p = &xbuf_[-u & (align-1)];
      UPCXX_ASSERT(is_aligned(p, align));
      return p;
    }

    xaligned_storage() noexcept = default;
    xaligned_storage(xaligned_storage const &that) noexcept {
      detail::memcpy_aligned<align>(this->storage(), that.storage(), size);
    }
    xaligned_storage& operator=(xaligned_storage const &that) noexcept {
      if(this != &that)
        detail::memcpy_aligned<align>(this->storage(), that.storage(), size);
      return *this;
    }
  };

  //////////////////////////////////////////////////////////////////////////////
  // detail::raw_storage<T>: Like std::aligned_storage<>::type, except more convenient.
  // The typed value exists in the `value()` member, but isnt implicitly
  // constructed. Construction should be done by user with placement new like:
  //   `::new(&my_storage) T(...)`.
  // Also, the value won't be implicilty destructed either. That too is the user's
  // responsibility.

  template<typename T>
  class raw_storage {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type raw_;

  public:
    void* raw() noexcept {
      return &raw_;
    }
    
    T& value() noexcept {
      return *detail::launder(reinterpret_cast<T*>(&raw_));
    }
    
    // Invoke value's destructor.
    void destruct() noexcept {
      detail::destruct(value());
    }

    // Move value out into return value and destruct a temporary and destruct it.
    T value_and_destruct() noexcept {
      T &val = this->value();
      T ans(std::move(val));
      detail::destruct(val);
      return ans;
    }
  };
  
  //////////////////////////////////////////////////////////////////////
  // detail::invoke_result<T, Args...>: abstract over std::result_of and
  // std::invoke_result.

  #if __cpp_lib_is_invocable >= 201703
    template<typename T, typename ...Args>
    using invoke_result = std::invoke_result<T, Args...>;
    template<typename T, typename ...Args>
    using invoke_result_t = std::invoke_result_t<T, Args...>;
  #else
    template<typename T, typename ...Args>
    using invoke_result = std::result_of<T(Args...)>;
    template<typename T, typename ...Args>
    using invoke_result_t = typename std::result_of<T(Args...)>::type;
  #endif

  //////////////////////////////////////////////////////////////////////
  // trait_forall: logical conjunction of one trait applied to
  // variadically-many argument types.

  template<template<typename...> class Test, typename ...T>
  struct trait_forall;
  template<template<typename...> class Test>
  struct trait_forall<Test> {
    static constexpr bool value = true;
  };
  template<template<typename...> class Test, typename T, typename ...Ts>
  struct trait_forall<Test,T,Ts...> {
    static constexpr bool value = Test<T>::value && trait_forall<Test,Ts...>::value;
  };
  
  template<template<typename...> class Test, typename Tuple>
  struct trait_forall_tupled;
  template<template<typename...> class Test, typename ...T>
  struct trait_forall_tupled<Test, std::tuple<T...>> {
    static constexpr bool value = trait_forall<Test, T...>::value;
  };
  
  //////////////////////////////////////////////////////////////////////
  // trait_any: disjunction, combines multiple traits into a new trait.
  // trait_all: conjunction, combines multiple traits into a new trait
  // trait_any, trait_all removed post 2021.3.0 release
  
  //////////////////////////////////////////////////////////////////////
  // is_lvalue_or_copyable, is_lvalue_or_movable: trait for whether a
  // type is either an lvalue reference or Copy/MoveConstructible

  template<typename Arg>
  struct is_lvalue_or_copyable {
    static constexpr bool value =
      std::is_lvalue_reference<Arg>::value ||
      std::is_copy_constructible<typename std::decay<Arg>::type>::value;
  };

  template<typename Arg>
  struct is_lvalue_or_movable {
    static constexpr bool value =
      std::is_lvalue_reference<Arg>::value ||
      std::is_move_constructible<typename std::decay<Arg>::type>::value;
  };

  //////////////////////////////////////////////////////////////////////

  template<typename Tuple, template<typename...> class Into>
  struct tuple_types_into;
  template<typename ...T, template<typename...> class Into>
  struct tuple_types_into<std::tuple<T...>, Into> {
    typedef Into<T...> type;
  };
  template<typename Tuple, template<typename...> class Into>
  using tuple_types_into_t = typename tuple_types_into<Tuple,Into>::type;
  
  //////////////////////////////////////////////////////////////////////

  template<int...>
  struct index_sequence {};

  //////////////////////////////////////////////////////////////////////

  namespace help {
    template<int n, int ...s>
    struct make_index_sequence: make_index_sequence<n-1, n-1, s...> {};

    template<int ...s>
    struct make_index_sequence<0, s...> {
      typedef index_sequence<s...> type;
    };
  }
  
  template<int n>
  using make_index_sequence = typename help::make_index_sequence<n>::type;

  //////////////////////////////////////////////////////////////////////

  namespace help {
    template<typename T, typename IS>
    struct make_nary_tuple;

    template<typename T, int ...s>
    struct make_nary_tuple<T, index_sequence<s...>> {
      using type = std::tuple<typename std::conditional<bool(s), T, T>::type...>;
    };
  }

  template<typename T, int n>
  using make_nary_tuple = typename help::make_nary_tuple<T, make_index_sequence<n>>::type;

  //////////////////////////////////////////////////////////////////////////////
  // add_lref_if_nonref: Add a lvalue-reference (&) to type T if T isn't already
  // a reference (& or &&) type.
  // add_clref_if_nonref: Add a const-lvalue-reference (const &) to type T if T
  // isn't already a reference (& or &&) type.
  // add_rref_if_nonref: Add a rvalue-reference (&&) to type T if T isn't
  // already a reference (& or &&) type.
  // add_lref_if_nonref, add_clref_if_nonref, add_rref_if_nonref removed
  // post 2021.3.0 release
  
  //////////////////////////////////////////////////////////////////////
  // decay_tupled removed post 2021.3.0 release
 
  //////////////////////////////////////////////////////////////////////
  // decay_tupled_rrefs: decay elements of a tuple, preserving lvalue refs but not rvalue refs
  template<typename Tup>
  struct decay_tupled_rrefs;
  template<typename ...T>
  struct decay_tupled_rrefs<std::tuple<T...>> {
    typedef std::tuple<
      typename std::conditional<
        std::is_lvalue_reference<T>::value,
        T,
        typename std::decay<T>::type
      >::type...> type;
  };

  //////////////////////////////////////////////////////////////////////
  // get_or_void & tuple_element_or_void: analogs of std::get &
  // std::tuple_elemenet which return void for out-of-range indices
  // get_or_void removed post 2021.3.0 release
  
  template<int i, typename Tup,
           bool in_range = 0 <= i && i < std::tuple_size<Tup>::value>
  struct tuple_element_or_void: std::tuple_element<i,Tup> {};
  
  template<int i, typename Tup>
  struct tuple_element_or_void<i,Tup,/*in_range=*/false> {
    using type = void;
  };
  
  //////////////////////////////////////////////////////////////////////
  // tuple_refs: Get individual references to tuple componenets. The
  // reference type of the passed in tuple (&, const&, or &&) determines
  // the type of the references you get back. For components which are
  // already `&` or `&&` types you'll get those back unmodified.
  //
  // tuple_refs_return<Tup>::type: Computes return type of `tuple_refs(Tup)`,
  // where `Tup` must be of the form: `std::tuple<T...> [&, const&, or &&]
  
  template<typename Tup>
  struct tuple_refs_return;
  template<typename ...T>
  struct tuple_refs_return<std::tuple<T...>&> {
    template<typename U>
    using element = typename std::conditional<std::is_reference<U>::value, U, U&>::type;
    using type = std::tuple<element<T>...>;
  };
  template<typename ...T>
  struct tuple_refs_return<std::tuple<T...> const&> {
    template<typename U>
    using element = typename std::conditional<std::is_reference<U>::value, U, U const&>::type;
    using type = std::tuple<element<T>...>;
  };
  template<typename ...T>
  struct tuple_refs_return<std::tuple<T...>&&> {
    template<typename U>
    using element = typename std::conditional<std::is_reference<U>::value, U, U&&>::type;
    using type = std::tuple<element<T>...>;
  };
  
  template<typename Tup>
  using tuple_refs_return_t = typename tuple_refs_return<Tup>::type;
  
  template<typename Tup, typename ...T, int ...i>
  tuple_refs_return_t<Tup&&> tuple_refs_help(Tup &&tup, std::tuple<T...>*, index_sequence<i...>) {
    return typename tuple_refs_return<Tup&&>::type(
      static_cast<typename tuple_refs_return<Tup&&>::template element<T>>(std::template get<i>(static_cast<Tup&&>(tup)))...
    );
  }
  
  template<typename Tup>
  tuple_refs_return_t<Tup&&> tuple_refs(Tup &&tup) {
    using TupD = typename std::decay<Tup>::type;
    return tuple_refs_help(
      static_cast<Tup&&>(tup),
      static_cast<TupD*>(nullptr),
      make_index_sequence<std::tuple_size<TupD>::value>()
    );
  }

  //////////////////////////////////////////////////////////////////////
  // tuple_rvals: Get a tuple of rvalue-references to tuple componenets.
  // Components which are already `&` or `&&` are returned unmodified.
  // Non-reference componenets are returned as `&&` only if the tuple is
  // passed by non-const `&`, otherwise the non-reference type is used
  // and the value is moved or copied from the input to output tuple.
  // tuple_rvals removed post 2021.3.0 release
  
  //////////////////////////////////////////////////////////////////////
  // forward_as_tuple_decay_rrefs: like std::forward_as_tuple, but drops
  // rvalue-refs (&&) from components, lvalues (&, const&) are preserved.
  
  template<typename ...T>
  std::tuple<T...> forward_as_tuple_decay_rrefs(T &&...refs) {
    return std::tuple<T...>(static_cast<T&&>(refs)...);
  }
    
  //////////////////////////////////////////////////////////////////////
  // apply_tupled: Apply a callable against an argument list wrapped
  // in a tuple.
  
  namespace help {
    template<typename Fn, typename Tup, int ...i>
    inline auto apply_tupled(
        Fn &&fn, Tup &&args, index_sequence<i...>
      )
      -> decltype(static_cast<Fn&&>(fn)(std::get<i>(static_cast<Tup&&>(args))...)) {
      return static_cast<Fn&&>(fn)(std::get<i>(static_cast<Tup&&>(args))...);
    }
  }
  
  template<typename Fn, typename Tup>
  inline auto apply_tupled(Fn &&fn, Tup &&args)
    -> decltype(
      help::apply_tupled(
        std::forward<Fn>(fn), std::forward<Tup>(args),
        make_index_sequence<std::tuple_size<Tup>::value>()
      )
    ) {
    return help::apply_tupled(
      std::forward<Fn>(fn), std::forward<Tup>(args),
      make_index_sequence<std::tuple_size<Tup>::value>()
    );
  }

  //////////////////////////////////////////////////////////////////////
  // align_of<T>(): returns alignof(T) or 0 for incomplete types

  template <typename T, std::size_t align = alignof(T)>
  constexpr size_t align_of_(T *_) { return align; }
  constexpr size_t align_of_(...) { return 0; }

  template<typename T>
  constexpr size_t align_of() { return align_of_((T*)nullptr); }

  //////////////////////////////////////////////////////////////////////
  // typename_of<T>(): returns typeid(T).name() or null for incomplete types

  template <typename T, std::size_t x = sizeof(T)>
  inline const char *typename_of_(T *_) { 
    #if UPCXXI_HAVE_RTTI
      return typeid(T).name(); 
    #else
      return "";
    #endif
  }
  inline const char *typename_of_(...) { return nullptr; }

  template<typename T>
  inline const char *typename_of() { return typename_of_((T*)nullptr); }

  //////////////////////////////////////////////////////////////////////
  // raii_cleanup: define a lightweight stack unwinding cleanup action
  //
  // This utility replaces code written using this try/catch idiom:
  //
  //   allocate_resource();
  //   try {
  //     something_that_might_throw();
  //   } catch (...) {
  //     cancel_resource();
  //     throw;
  //   }
  //
  // with an RAII idiom that looks like this:
  //
  //   allocate_resource();
  //   auto guard = detail::make_raii_cleanup(cancel_resource);
  //   something_that_might_throw();
  //   guard.reset(); // did not throw, release guard
  //
  // The callable argument to make_raii_cleanup() is invoked iff the guard
  // object is destroyed (generally by leaving scope) before reset() was called.
  //
  // See pull request #376 for performance results, which show this utility
  // often outperforms try/catch and unique_ptr idioms on compilers of interest.
  
  template<typename Fn>
  class raii_cleanup {
    Fn fn;
    bool armed;
   public:
    inline raii_cleanup(Fn &&f) : fn(std::forward<Fn>(f)), armed(true) {}
    inline void reset() { armed = false; }
    inline raii_cleanup(raii_cleanup &&other) : fn(std::move(other.fn)) {
      other.armed = false;
    }
    raii_cleanup(raii_cleanup const &) = delete;
    inline ~raii_cleanup() {
      UPCXXI_IF_PF(armed) fn();
    }
  };
  template<typename Fn>
  raii_cleanup<Fn> make_raii_cleanup(Fn &&f) {
    return raii_cleanup<Fn>(std::forward<Fn>(f));
  } 

} // namespace detail
} // namespace upcxx
#endif
