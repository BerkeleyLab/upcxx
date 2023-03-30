#ifndef _7f4d2f35_031e_403e_ac70_7b1cafb357f3
#define _7f4d2f35_031e_403e_ac70_7b1cafb357f3

// This file is adapted from the reference implementation of
// std::optional by Andrzej Krzemienski at
// https://github.com/akrzemi1/Optional and is subject to the Boost
// Software License, Version 1.0. Original copyright notice below.

// Copyright (C) 2011 - 2012 Andrzej Krzemienski.
//
// Use, modification, and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
// The idea and interface is based on Boost.Optional library
// authored by Fernando Luis Cacciola Carballal

#ifndef UPCXXI_USE_STD_OPTIONAL
#define UPCXXI_USE_STD_OPTIONAL (__cplusplus >= 201703L)
#endif

#if UPCXXI_USE_STD_OPTIONAL
// Use std::optional and associated entities.
#include <optional>
namespace upcxx {
  using std::optional;
  using std::make_optional;
  using std::in_place_t;
  using std::in_place;
  using std::nullopt_t;
  using std::nullopt;
  using std::bad_optional_access;
}
#else
# include <utility>
# include <type_traits>
# include <initializer_list>
# include <functional>
# include <string>
# include <stdexcept>
# include <upcxx/backend_fwd.hpp>

# define UPCXXI_TR2_OPTIONAL_REQUIRES(...) typename std::enable_if<__VA_ARGS__::value, bool>::type = false

# if __cplusplus < 201402L || __PGI || __INTEL_COMPILER
// These do not work on PGI or Intel at any language level
#   define UPCXXI_OPTIONAL_HAS_MOVE_ACCESSORS 0
# else
#   define UPCXXI_OPTIONAL_HAS_MOVE_ACCESSORS 1
# endif

# if __cplusplus < 201402L
// In C++11 constexpr implies const, so we need to make non-const members also non-constexpr
#   define UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR
// In C++11, the constructor for std::initializer_list is not constexpr
#   define UPCXXI_OPTIONAL_HAS_CONSTEXPR_INIT_LIST 0
#   define UPCXXI_OPTIONAL_CONSTEXPR_INIT_LIST
# else
#   define UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR constexpr
#   define UPCXXI_OPTIONAL_HAS_CONSTEXPR_INIT_LIST 1
#   define UPCXXI_OPTIONAL_CONSTEXPR_INIT_LIST constexpr
# endif

namespace upcxx{
namespace detail {
namespace optional_impl_{

// 20.5.4, optional for object types
template <class T> class optional;


// workaround: std utility functions aren't constexpr yet
template <class T> inline constexpr T&& constexpr_forward(typename std::remove_reference<T>::type& t) noexcept
{
  return static_cast<T&&>(t);
}

template <class T> inline constexpr T&& constexpr_forward(typename std::remove_reference<T>::type&& t) noexcept
{
    static_assert(!std::is_lvalue_reference<T>::value, "!!");
    return static_cast<T&&>(t);
}

template <class T> inline constexpr typename std::remove_reference<T>::type&& constexpr_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}


#if !UPCXXI_ASSERT_ENABLED
# define UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION(CHECK, EXPR) (EXPR)
#else
// This is used in a constexpr context, so we need to ensure that the
// test expression is constexpr-compatible. Not all compilers treat
// __builtin_expect (used by UPCXX_ASSERT) as constexpr, so we have to
// lift the check out of UPCXX_ASSERT so that the whole conditional is
// constexpr-compatible.
# define UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION(CHECK, EXPR) ((CHECK) ? (EXPR) : (UPCXX_ASSERT(!#CHECK), (EXPR)))
#endif


namespace detail2_
{

// static_addressof: a constexpr version of addressof
template <typename T>
struct has_overloaded_addressof
{
  template <class X>
  constexpr static bool has_overload(...) { return false; }

  template <class X, size_t S = sizeof(std::declval<X&>().operator&()) >
  constexpr static bool has_overload(bool) { return true; }

  constexpr static bool value = has_overload<T>(true);
};

template <typename T, UPCXXI_TR2_OPTIONAL_REQUIRES(!has_overloaded_addressof<T>)>
constexpr T* static_addressof(T& ref)
{
  return &ref;
}

template <typename T, UPCXXI_TR2_OPTIONAL_REQUIRES(has_overloaded_addressof<T>)>
T* static_addressof(T& ref)
{
  return std::addressof(ref);
}


// the call to convert<A>(b) has return type A and converts b to type A iff b decltype(b) is implicitly convertible to A
template <class U>
constexpr U convert(U v) { return v; }


namespace swap_ns
{
  using std::swap;

  template <class T>
  void adl_swap(T& t, T& u) noexcept(noexcept(swap(t, u)))
  {
    swap(t, u);
  }

} // namespace swap_ns

} // namespace detail2_


constexpr struct trivial_init_t{} trivial_init{};

// 20.5.6, In-place construction
constexpr struct in_place_t{} in_place{};


// 20.5.7, Disengaged state indicator
struct nullopt_t
{
  struct init{};
  constexpr explicit nullopt_t(init){}
};
constexpr nullopt_t nullopt{nullopt_t::init()};


// 20.5.8, class bad_optional_access
class bad_optional_access : public std::logic_error {
public:
  explicit bad_optional_access() : logic_error{"bad optional access"} {}
};

template <class T>
union storage_t
{
  unsigned char dummy_;
  T value_;

  constexpr storage_t( trivial_init_t ) noexcept : dummy_() {};

  template <class... Args>
  constexpr storage_t( Args&&... args ) : value_(constexpr_forward<Args>(args)...) {}

  ~storage_t(){}
};


template <class T>
union constexpr_storage_t
{
    unsigned char dummy_;
    T value_;

    constexpr constexpr_storage_t( trivial_init_t ) noexcept : dummy_() {};

    template <class... Args>
    constexpr constexpr_storage_t( Args&&... args ) : value_(constexpr_forward<Args>(args)...) {}

    ~constexpr_storage_t() = default;
};


template <class T>
struct optional_base
{
    bool init_;
    storage_t<T> storage_;

    constexpr optional_base() noexcept : init_(false), storage_(trivial_init) {};

    explicit constexpr optional_base(const T& v) : init_(true), storage_(v) {}

    explicit constexpr optional_base(T&& v) : init_(true), storage_(constexpr_move(v)) {}

    template <class... Args> explicit optional_base(in_place_t, Args&&... args)
        : init_(true), storage_(constexpr_forward<Args>(args)...) {}

    template <class U, class... Args, UPCXXI_TR2_OPTIONAL_REQUIRES(std::is_constructible<T, std::initializer_list<U>>)>
    explicit optional_base(in_place_t, std::initializer_list<U> il, Args&&... args)
        : init_(true), storage_(il, std::forward<Args>(args)...) {}

    ~optional_base() { if (init_) storage_.value_.T::~T(); }
};


template <class T>
struct constexpr_optional_base
{
    bool init_;
    constexpr_storage_t<T> storage_;

    constexpr constexpr_optional_base() noexcept : init_(false), storage_(trivial_init) {};

    explicit constexpr constexpr_optional_base(const T& v) : init_(true), storage_(v) {}

    explicit constexpr constexpr_optional_base(T&& v) : init_(true), storage_(constexpr_move(v)) {}

    template <class... Args> explicit constexpr constexpr_optional_base(in_place_t, Args&&... args)
      : init_(true), storage_(constexpr_forward<Args>(args)...) {}

    template <class U, class... Args, UPCXXI_TR2_OPTIONAL_REQUIRES(std::is_constructible<T, std::initializer_list<U>>)>
    UPCXXI_OPTIONAL_CONSTEXPR_INIT_LIST explicit constexpr_optional_base(in_place_t, std::initializer_list<U> il, Args&&... args)
      : init_(true), storage_(il, std::forward<Args>(args)...) {}

    ~constexpr_optional_base() = default;
};

template <class T>
using OptionalBase = typename std::conditional<
    std::is_trivially_destructible<T>::value,                     // if possible
    constexpr_optional_base<typename std::remove_const<T>::type>, // use base with trivial destructor
    optional_base<typename std::remove_const<T>::type>
>::type;



template <class T>
class optional : private OptionalBase<T>
{
  static_assert( !std::is_same<typename std::decay<T>::type, nullopt_t>::value, "bad T" );
  static_assert( !std::is_same<typename std::decay<T>::type, in_place_t>::value, "bad T" );


  constexpr bool initialized() const noexcept { return OptionalBase<T>::init_; }
  typename std::remove_const<T>::type* dataptr() {  return std::addressof(OptionalBase<T>::storage_.value_); }
  constexpr const T* dataptr() const { return detail2_::static_addressof(OptionalBase<T>::storage_.value_); }

  constexpr const T& contained_val() const& { return OptionalBase<T>::storage_.value_; }
# if UPCXXI_OPTIONAL_HAS_MOVE_ACCESSORS == 1
  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T&& contained_val() && { return std::move(OptionalBase<T>::storage_.value_); }
  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T& contained_val() & { return OptionalBase<T>::storage_.value_; }
# else
  T& contained_val() & { return OptionalBase<T>::storage_.value_; }
  T&& contained_val() && { return std::move(OptionalBase<T>::storage_.value_); }
# endif

  void clear() noexcept {
    if (initialized()) dataptr()->T::~T();
    OptionalBase<T>::init_ = false;
  }

  template <class... Args>
  void initialize(Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...)))
  {
    UPCXX_ASSERT(!OptionalBase<T>::init_);
    ::new (static_cast<void*>(dataptr())) T(std::forward<Args>(args)...);
    OptionalBase<T>::init_ = true;
  }

  template <class U, class... Args>
  void initialize(std::initializer_list<U> il, Args&&... args) noexcept(noexcept(T(il, std::forward<Args>(args)...)))
  {
    UPCXX_ASSERT(!OptionalBase<T>::init_);
    ::new (static_cast<void*>(dataptr())) T(il, std::forward<Args>(args)...);
    OptionalBase<T>::init_ = true;
  }

public:
  typedef T value_type;

  // 20.5.5.1, constructors
  constexpr optional() noexcept : OptionalBase<T>()  {};
  constexpr optional(nullopt_t) noexcept : OptionalBase<T>() {};

  optional(const optional& rhs)
  : OptionalBase<T>()
  {
    if (rhs.initialized()) {
        ::new (static_cast<void*>(dataptr())) T(*rhs);
        OptionalBase<T>::init_ = true;
    }
  }

  optional(optional&& rhs) noexcept(std::is_nothrow_move_constructible<T>::value)
  : OptionalBase<T>()
  {
    if (rhs.initialized()) {
        ::new (static_cast<void*>(dataptr())) T(std::move(*rhs));
        OptionalBase<T>::init_ = true;
    }
  }

  constexpr optional(const T& v) : OptionalBase<T>(v) {}

  constexpr optional(T&& v) : OptionalBase<T>(constexpr_move(v)) {}

  template <class... Args>
  explicit constexpr optional(in_place_t, Args&&... args)
  : OptionalBase<T>(in_place_t{}, constexpr_forward<Args>(args)...) {}

  template <class U, class... Args, UPCXXI_TR2_OPTIONAL_REQUIRES(std::is_constructible<T, std::initializer_list<U>>)>
  UPCXXI_OPTIONAL_CONSTEXPR_INIT_LIST explicit optional(in_place_t, std::initializer_list<U> il, Args&&... args)
  : OptionalBase<T>(in_place_t{}, il, constexpr_forward<Args>(args)...) {}

  // 20.5.4.2, Destructor
  ~optional() = default;

  // 20.5.4.3, assignment
  optional& operator=(nullopt_t) noexcept
  {
    clear();
    return *this;
  }

  optional& operator=(const optional& rhs)
  {
    if      (initialized() == true  && rhs.initialized() == false) clear();
    else if (initialized() == false && rhs.initialized() == true)  initialize(*rhs);
    else if (initialized() == true  && rhs.initialized() == true)  contained_val() = *rhs;
    return *this;
  }

  optional& operator=(optional&& rhs)
  noexcept(std::is_nothrow_move_assignable<T>::value && std::is_nothrow_move_constructible<T>::value)
  {
    if      (initialized() == true  && rhs.initialized() == false) clear();
    else if (initialized() == false && rhs.initialized() == true)  initialize(std::move(*rhs));
    else if (initialized() == true  && rhs.initialized() == true)  contained_val() = std::move(*rhs);
    return *this;
  }

  template <class U>
  auto operator=(U&& v)
  -> typename std::enable_if
  <
    std::is_same<typename std::decay<U>::type, T>::value,
    optional&
  >::type
  {
    if (initialized()) { contained_val() = std::forward<U>(v); }
    else               { initialize(std::forward<U>(v));  }
    return *this;
  }


  template <class... Args>
  T& emplace(Args&&... args)
  {
    clear();
    initialize(std::forward<Args>(args)...);
    return contained_val();
  }

  template <class U, class... Args>
  T& emplace(std::initializer_list<U> il, Args&&... args)
  {
    clear();
    initialize<U, Args...>(il, std::forward<Args>(args)...);
    return contained_val();
  }

  // 20.5.4.4, Swap
  void swap(optional<T>& rhs) noexcept(std::is_nothrow_move_constructible<T>::value
                                       && noexcept(detail2_::swap_ns::adl_swap(std::declval<T&>(), std::declval<T&>())))
  {
    if      (initialized() == true  && rhs.initialized() == false) { rhs.initialize(std::move(**this)); clear(); }
    else if (initialized() == false && rhs.initialized() == true)  { initialize(std::move(*rhs)); rhs.clear(); }
    else if (initialized() == true  && rhs.initialized() == true)  { using std::swap; swap(**this, *rhs); }
  }

  // 20.5.4.5, Observers

  explicit constexpr operator bool() const noexcept { return initialized(); }
  constexpr bool has_value() const noexcept { return initialized(); }

  constexpr T const* operator ->() const noexcept {
    return UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION(initialized(), dataptr());
  }

# if UPCXXI_OPTIONAL_HAS_MOVE_ACCESSORS == 1

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T* operator ->() noexcept {
    UPCXX_ASSERT(initialized());
    return dataptr();
  }

  constexpr T const& operator *() const& noexcept {
    return UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION(initialized(), contained_val());
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T& operator *() & noexcept {
    UPCXX_ASSERT(initialized());
    return contained_val();
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T&& operator *() && noexcept {
    UPCXX_ASSERT(initialized());
    return constexpr_move(contained_val());
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR const T&& operator *() const&& noexcept {
    UPCXX_ASSERT(initialized());
    return constexpr_move(contained_val());
  }

  constexpr T const& value() const& {
    return initialized() ? contained_val() : (throw bad_optional_access(), contained_val());
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T& value() & {
    return initialized() ? contained_val() : (throw bad_optional_access(), contained_val());
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T&& value() && {
    if (!initialized()) throw bad_optional_access();
	return std::move(contained_val());
  }

  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR const T&& value() const&& {
    if (!initialized()) throw bad_optional_access();
	return std::move(contained_val());
  }

# else

  T* operator ->() noexcept {
    UPCXX_ASSERT(initialized());
    return dataptr();
  }

  constexpr T const& operator *() const noexcept {
    return UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION(initialized(), contained_val());
  }

  T& operator *() noexcept {
    UPCXX_ASSERT(initialized());
    return contained_val();
  }

  constexpr T const& value() const {
    return initialized() ? contained_val() : (throw bad_optional_access(), contained_val());
  }

  T& value() {
    return initialized() ? contained_val() : (throw bad_optional_access(), contained_val());
  }

# endif

  template <class V>
  constexpr T value_or(V&& v) const&
  {
    return *this ? **this : detail2_::convert<T>(constexpr_forward<V>(v));
  }

# if UPCXXI_OPTIONAL_HAS_MOVE_ACCESSORS == 1

  template <class V>
  UPCXXI_OPTIONAL_MUTABLE_CONSTEXPR T value_or(V&& v) &&
  {
    return *this ? constexpr_move(const_cast<optional<T>&>(*this).contained_val()) : detail2_::convert<T>(constexpr_forward<V>(v));
  }

# else

  template <class V>
  T value_or(V&& v) &&
  {
    return *this ? constexpr_move(const_cast<optional<T>&>(*this).contained_val()) : detail2_::convert<T>(constexpr_forward<V>(v));
  }

# endif

  // 20.6.3.6, modifiers
  void reset() noexcept { clear(); }
};

// https://en.cppreference.com/w/cpp/utility/optional:
// "There are no optional references; a program is ill-formed if it
// instantiates an optional with a reference type."
template <class T>
class optional<T&>
{
  static_assert( sizeof(T) == 0, "optional references disallowed" );
};


template <class T>
class optional<T&&>
{
  static_assert( sizeof(T) == 0, "optional rvalue references disallowed" );
};


// 20.5.8, Relational operators
template <class T, class U> constexpr bool operator==(const optional<T>& x, const optional<U>& y)
{
  return bool(x) != bool(y) ? false : bool(x) == false ? true : *x == *y;
}

template <class T, class U> constexpr bool operator!=(const optional<T>& x, const optional<U>& y)
{
  return !(x == y);
}

template <class T, class U> constexpr bool operator<(const optional<T>& x, const optional<U>& y)
{
  return (!y) ? false : (!x) ? true : *x < *y;
}

template <class T, class U> constexpr bool operator>(const optional<T>& x, const optional<U>& y)
{
  return (y < x);
}

template <class T, class U> constexpr bool operator<=(const optional<T>& x, const optional<U>& y)
{
  return !(y < x);
}

template <class T, class U> constexpr bool operator>=(const optional<T>& x, const optional<U>& y)
{
  return !(x < y);
}


// 20.5.9, Comparison with nullopt
template <class T> constexpr bool operator==(const optional<T>& x, nullopt_t) noexcept
{
  return (!x);
}

template <class T> constexpr bool operator==(nullopt_t, const optional<T>& x) noexcept
{
  return (!x);
}

template <class T> constexpr bool operator!=(const optional<T>& x, nullopt_t) noexcept
{
  return bool(x);
}

template <class T> constexpr bool operator!=(nullopt_t, const optional<T>& x) noexcept
{
  return bool(x);
}

template <class T> constexpr bool operator<(const optional<T>&, nullopt_t) noexcept
{
  return false;
}

template <class T> constexpr bool operator<(nullopt_t, const optional<T>& x) noexcept
{
  return bool(x);
}

template <class T> constexpr bool operator<=(const optional<T>& x, nullopt_t) noexcept
{
  return (!x);
}

template <class T> constexpr bool operator<=(nullopt_t, const optional<T>&) noexcept
{
  return true;
}

template <class T> constexpr bool operator>(const optional<T>& x, nullopt_t) noexcept
{
  return bool(x);
}

template <class T> constexpr bool operator>(nullopt_t, const optional<T>&) noexcept
{
  return false;
}

template <class T> constexpr bool operator>=(const optional<T>&, nullopt_t) noexcept
{
  return true;
}

template <class T> constexpr bool operator>=(nullopt_t, const optional<T>& x) noexcept
{
  return (!x);
}



// 20.5.10, Comparison with U
template <class T, class U> constexpr bool operator==(const optional<T>& x, const U& v)
{
  return bool(x) ? *x == v : false;
}

template <class T, class U> constexpr bool operator==(const T& v, const optional<U>& x)
{
  return bool(x) ? v == *x : false;
}

template <class T, class U> constexpr bool operator!=(const optional<T>& x, const U& v)
{
  return bool(x) ? *x != v : true;
}

template <class T, class U> constexpr bool operator!=(const T& v, const optional<U>& x)
{
  return bool(x) ? v != *x : true;
}

template <class T, class U> constexpr bool operator<(const optional<T>& x, const U& v)
{
  return bool(x) ? *x < v : true;
}

template <class T, class U> constexpr bool operator>(const T& v, const optional<U>& x)
{
  return bool(x) ? v > *x : true;
}

template <class T, class U> constexpr bool operator>(const optional<T>& x, const U& v)
{
  return bool(x) ? *x > v : false;
}

template <class T, class U> constexpr bool operator<(const T& v, const optional<U>& x)
{
  return bool(x) ? v < *x : false;
}

template <class T, class U> constexpr bool operator>=(const optional<T>& x, const U& v)
{
  return bool(x) ? *x >= v : false;
}

template <class T, class U> constexpr bool operator<=(const T& v, const optional<U>& x)
{
  return bool(x) ? v <= *x : false;
}

template <class T, class U> constexpr bool operator<=(const optional<T>& x, const U& v)
{
  return bool(x) ? *x <= v : true;
}

template <class T, class U> constexpr bool operator>=(const T& v, const optional<U>& x)
{
  return bool(x) ? v >= *x : true;
}


// 20.5.12, Specialized algorithms
template <class T>
void swap(optional<T>& x, optional<T>& y) noexcept(noexcept(x.swap(y)))
{
  x.swap(y);
}


template <class T>
constexpr optional<typename std::decay<T>::type> make_optional(T&& v)
{
  return optional<typename std::decay<T>::type>(constexpr_forward<T>(v));
}

template <class X>
constexpr optional<X&> make_optional(std::reference_wrapper<X> v)
{
  return optional<X&>(v.get());
}


} // namespace optional_impl_
} // namespace detail

// public upcxx API
using detail::optional_impl_::optional;
using detail::optional_impl_::make_optional;
using detail::optional_impl_::in_place_t;
using detail::optional_impl_::in_place;
using detail::optional_impl_::nullopt_t;
using detail::optional_impl_::nullopt;
using detail::optional_impl_::bad_optional_access;

} // namespace upcxx

namespace std
{
  template <typename T>
  struct hash<upcxx::optional<T>>
  {
    typedef typename hash<T>::result_type result_type;
    typedef upcxx::optional<T> argument_type;

    constexpr result_type operator()(argument_type const& arg) const {
      return arg ? std::hash<T>{}(*arg) : result_type{};
    }
  };
}

# undef UPCXXI_TR2_OPTIONAL_REQUIRES
# undef UPCXXI_TR2_OPTIONAL_ASSERTED_EXPRESSION

#endif // UPCXXI_USE_STD_OPTIONAL

#endif // _7f4d2f35_031e_403e_ac70_7b1cafb357f3
