#ifndef _251dbe84_2eaa_42c1_9288_81c834900371
#define _251dbe84_2eaa_42c1_9288_81c834900371

#include <tuple>

////////////////////////////////////////////////////////////////////////////////
// Forwards of future API

namespace upcxx {
  namespace detail {
    struct future_header;
    template<typename ...T>
    struct future_header_result;
    template<typename ...T>
    struct future_header_promise;
    struct future_header_dependent;
    
    template<typename FuArg>
    struct future_dependency;
    
    struct future_body;
    struct future_body_proxy_;
    template<typename ...T>
    struct future_body_proxy;
    template<typename FuArg, typename Fn>
    struct future_body_then;
    
    // classes of all-static functions for working with headers
    struct future_header_ops_general;
    struct future_header_ops_result;
    struct future_header_ops_result_ready;
    struct future_header_ops_promise;
    struct future_header_ops_dependent;
    
    // future implementations
    template<typename HeaderOps, bool unique, typename ...T>
    struct future_impl_shref;
    template<typename ...T>
    struct future_impl_result;
    template<typename ArgTuple, typename ...T>
    struct future_impl_when_all;
    template<typename FuArg, typename Fn, typename ...T>
    struct future_impl_then_lazy;
    
    // future1 implementation mappers
    template<typename HeaderOps, bool unique=false>
    struct future_kind_shref {
      template<typename ...T>
      using with_types = future_impl_shref<HeaderOps,unique,T...>;
    };
    
    struct future_kind_result {
      template<typename ...T>
      using with_types = future_impl_result<T...>;
    };
    
    template<typename ...FuArg>
    struct future_kind_when_all {
      template<typename ...T>
      using with_types = future_impl_when_all<std::tuple<FuArg...>, T...>;
    };
    
    template<typename FuArg, typename Fn>
    struct future_kind_then_lazy {
      template<typename ...T>
      using with_types = future_impl_then_lazy<FuArg,Fn,T...>;
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // future1: The type given to users.
  // implemented in: upcxx/future/future1.hpp
  
  namespace detail {
    template<typename Kind, typename ...T>
    struct future1;
  }

  //////////////////////////////////////////////////////////////////////////////
  // future_impl_traits: given the impl type, determine other associated types
  
  namespace detail {
    template<typename Impl>
    struct future_impl_traits {
      static constexpr bool is_impl = false;
    };
    template<typename Impl>
    struct future_impl_traits<Impl&>: future_impl_traits<Impl> {};
    template<typename Impl>
    struct future_impl_traits<Impl&&>: future_impl_traits<Impl> {};
    template<typename Impl>
    struct future_impl_traits<Impl const>: future_impl_traits<Impl> {};
    
    template<typename HeaderOps, bool unique, typename ...T>
    struct future_impl_traits<future_impl_shref<HeaderOps, unique, T...>> {
      static constexpr bool is_impl = true;
      using kind_type = future_kind_shref<HeaderOps, unique>;
      using future1_type = future1<kind_type, T...>;
    };
    template<typename ...T>
    struct future_impl_traits<future_impl_result<T...>> {
      static constexpr bool is_impl = true;
      using kind_type = future_kind_result;
      using future1_type = future1<kind_type, T...>;
    };
    template<typename ...FuArg, typename ...T>
    struct future_impl_traits<future_impl_when_all<std::tuple<FuArg...>, T...>> {
      static constexpr bool is_impl = true;
      using kind_type = future_kind_when_all<FuArg...>;
      using future1_type = future1<kind_type, T...>;
    };
    template<typename FuArg, typename Fn, typename ...T>
    struct future_impl_traits<future_impl_then_lazy<FuArg, Fn, T...>> {
      static constexpr bool is_impl = true;
      using kind_type = future_kind_then_lazy<FuArg, Fn>;
      using future1_type = future1<kind_type, T...>;
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // future: An alias for future1 using a shared reference implementation.

  namespace detail {
    using future_kind_default = detail::future_kind_shref<detail::future_header_ops_general>;
  }
  
  template<typename ...T>
  using future = detail::future1<detail::future_kind_default, T...>;
  
  template<typename ...T>
  class promise;
  
  //////////////////////////////////////////////////////////////////////
  // future_is_trivially_ready: Trait for detecting trivially ready
  // futures. Specializations provided in each future implementation.

  namespace detail {
    template<typename Future>
    struct future_is_trivially_ready/*{
      static constexpr bool value;
    }*/;
  }
  
  //////////////////////////////////////////////////////////////////////
  // Future/continuation function-application support
  // implemented in: upcxx/future/apply.hpp
  
  namespace detail {
    /* The following apply_xxx_as_future types are stateless callables intended
     * to be specialized on all sorts of static properties of the types involved.
     * The types "FnRef" & "ArgRef" are intended to be reference or value types
     * reflecting exactly how the parameter is passed.
     * 
     * Defined in: future/apply.hpp
     */

    // Apply function to arguments lifting return to future.
    template<typename FnRef, typename ...ArgRef>
    struct apply_variadic_as_future/*{
      typedef future1<Kind,U...> return_type;
      return_type operator()(FnRef fn, ArgRef...);
    }*/;

    // Apply function to tupled arguments lifting return to future.
    template<typename FnRef, typename ArgRefTupRef/*=std::tuple<ArgRef...> {const} {&|&&}*/>
    struct apply_tupled_as_future1/*{
      typedef future1<Kind,U...> return_type;
      return_type operator()(FnRef fn, ArgRefTupRef args);
    }*/;
    
    // Apply function to results of future with return lifted to future.
    template<typename FnRef, typename ArgRef/*=future1<...> {const} {&|&&}*/>
    struct apply_futured_as_future/*{
      typedef future1<Kind,U...> return_type;
      return_type operator()(FnRef fn, ArgRef arg);
    }*/;
  }

  //////////////////////////////////////////////////////////////////////
  // detail::future_from_tuple: Generate future1 type from a Kind and
  // result types in a tuple.
  
  namespace detail {
    template<typename Kind, typename Tup>
    struct future_from_tuple;
    template<typename Kind, typename ...T>
    struct future_from_tuple<Kind, std::tuple<T...>> {
      using type = future1<Kind, T...>;
    };
    
    template<typename Kind, typename Tup>
    using future_from_tuple_t = typename future_from_tuple<Kind,Tup>::type;
  }

  //////////////////////////////////////////////////////////////////////
  // detail::future_change_kind

  namespace detail {
    template<typename Fut, typename NewKind, bool condition=true>
    struct future_change_kind;

    template<typename OldKind, typename ...T, typename NewKind>
    struct future_change_kind<future1<OldKind,T...>, NewKind, /*condition=*/true> {
      using type = future1<NewKind,T...>;
    };
    template<typename OldKind, typename ...T, typename NewKind>
    struct future_change_kind<future1<OldKind,T...>, NewKind, /*condition=*/false> {
      using type = future1<OldKind,T...>;
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // detail::future_then()(a,b): implementats `a.then(b)`
  // detail::future_then_pure()(a,b): implementats `a.then_pure(b)`
  // implemented in: upcxx/future/then.hpp
  
  namespace detail {
    template<
      typename Arg, typename Fn, bool return_lazy=false,
      typename FnRet = typename apply_futured_as_future<Fn,Arg>::return_type,
      bool arg_trivial = future_is_trivially_ready<Arg>::value>
    struct future_then;
  }
}
#endif
