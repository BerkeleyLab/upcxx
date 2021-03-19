#ifndef _e1661a2a_f7f6_44f7_97dc_4f3e6f4fd018
#define _e1661a2a_f7f6_44f7_97dc_4f3e6f4fd018

#include <upcxx/future/core.hpp>
#include <upcxx/future/impl_result.hpp>
#include <upcxx/utility.hpp>

namespace upcxx {
namespace detail {
  //////////////////////////////////////////////////////////////////////////////
  
  // dispatches on return type of function call
  template<typename FnRef, typename ArgRefTupRef, typename ArgIxs, typename Return>
  struct apply_tupled_as_future_dispatch;

  template<typename FnRef, typename ArgRefTup, typename Return>
  struct apply_variadic_as_future_dispatch;

  //////////////////////////////////////////////////////////////////////////////
  // detail::apply_***_as_future_dispatch: specializatoin for when lambda
  // return = void

  template<typename FnRef, typename ArgRefTupRef, int ...ai>
  struct apply_tupled_as_future_dispatch<
      FnRef, ArgRefTupRef,
      /*ArgIxs=*/detail::index_sequence<ai...>,
      /*Return=*/void
    > {
    using return_type = future1<future_kind_result>;
    
    return_type operator()(FnRef fn, ArgRefTupRef args) {
      static_cast<FnRef>(fn)(std::get<ai>(static_cast<ArgRefTupRef>(args))...);
      return return_type(future_impl_result<>(), detail::internal_only{});
    }
  };

  template<typename FnRef, typename ...ArgRef>
  struct apply_variadic_as_future_dispatch<
      FnRef, /*ArgRefTup=*/std::tuple<ArgRef...>, /*Return=*/void
    > {
    using return_type = future1<future_kind_result>;
    
    return_type operator()(FnRef fn, ArgRef ...arg) {
      static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...);
      return return_type(future_impl_result<>(), detail::internal_only{});
    }

    template<typename ThenFn>
    using then_lazy_return_type =
      typename apply_variadic_as_future<ThenFn>::return_type;

    template<typename ThenFn>
    then_lazy_return_type<ThenFn&&> call_then_lazy(FnRef fn, ThenFn &&then_fn, ArgRef ...arg) {
      static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...);
      return apply_variadic_as_future<ThenFn&&>()(static_cast<ThenFn&&>(then_fn));
    }
  };
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::apply_***_as_future_dispatch: specializatoins for when lambda
  // return not a future and != void

  template<typename FnRef, typename ArgRefTupRef, int ...ai, typename Return>
  struct apply_tupled_as_future_dispatch<
      FnRef, ArgRefTupRef,
      /*ArgIxs=*/detail::index_sequence<ai...>,
      Return
    > {
    using return_type = future1<future_kind_result, Return>;
    
    return_type operator()(FnRef fn, ArgRefTupRef args) {
      return return_type(
        future_impl_result<Return>(
          static_cast<FnRef>(fn)(std::get<ai>(static_cast<ArgRefTupRef>(args))...)
        ),
        detail::internal_only{}
      );
    }
  };

  template<typename FnRef, typename ...ArgRef, typename Return>
  struct apply_variadic_as_future_dispatch<
      FnRef, /*ArgRefTup=*/std::tuple<ArgRef...>, Return
    > {
    using return_type = future1<future_kind_result, Return>;
    
    return_type operator()(FnRef fn, ArgRef ...arg) {
      return return_type(
        future_impl_result<Return>(
          static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...)
        ),
        detail::internal_only{}
      );
    }

    template<typename ThenFn>
    using then_lazy_return_type =
      typename apply_variadic_as_future<ThenFn, Return&&>::return_type;

    template<typename ThenFn>
    then_lazy_return_type<ThenFn&&> call_then_lazy(FnRef fn, ThenFn &&then_fn, ArgRef ...arg) {
      return apply_variadic_as_future<ThenFn&&, Return&&>()(
        static_cast<ThenFn&&>(then_fn),
        static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...)
      );
    }
  };
  
  //////////////////////////////////////////////////////////////////////////////
  // detail::apply_***_as_future_dispatch: lambda return is a future

  template<typename FnRef, typename ArgRefTupRef, int ...ai, typename Kind, typename ...T>
  struct apply_tupled_as_future_dispatch<
      FnRef, ArgRefTupRef,
      /*ArgIxs=*/detail::index_sequence<ai...>,
      /*Return=*/future1<Kind,T...>
    > {
    using return_type = future1<Kind,T...>;

    return_type operator()(FnRef fn, ArgRefTupRef args) {
      return static_cast<FnRef>(fn)(std::get<ai>(static_cast<ArgRefTupRef>(args))...);
    }
  };
  
  template<typename FnRef, typename ...ArgRef, typename Kind, typename ...T>
  struct apply_variadic_as_future_dispatch<
      FnRef, /*ArgRefTup=*/std::tuple<ArgRef...>, /*Return=*/future1<Kind,T...>
    > {
    using return_type = future1<Kind,T...>;

    return_type operator()(FnRef fn, ArgRef ...arg) {
      return static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...);
    }

    template<typename ThenFn>
    using then_lazy_return_type =
      decltype(std::declval<return_type>().then_lazy(std::declval<ThenFn>(),
                                                     detail::internal_only{}));

    template<typename ThenFn>
    then_lazy_return_type<ThenFn&&>
    call_then_lazy(FnRef fn, ThenFn &&then_fn, ArgRef ...arg) {
      return static_cast<FnRef>(fn)(static_cast<ArgRef>(arg)...).then_lazy(
        static_cast<ThenFn&&>(then_fn),
        detail::internal_only{}
      );
    }
  };

  //////////////////////////////////////////////////////////////////////
  // future/fwd.hpp: detail::apply_variadic_as_future
  
  template<typename FnRef, typename ...ArgRef>
  struct apply_variadic_as_future:
    apply_variadic_as_future_dispatch<
      FnRef, std::tuple<ArgRef...>,
      detail::invoke_result_t<FnRef, ArgRef...>
    > {
  };

  //////////////////////////////////////////////////////////////////////
  // future/fwd.hpp: detail::apply_tupled_as_future1

  template<typename FnRef, typename ArgRefTupRef,
           typename ArgRefTup = typename std::decay<ArgRefTupRef>::type>
  struct apply_tupled_as_future_help;

  template<typename FnRef, typename ArgRefTupRef, typename ...ArgRef>
  struct apply_tupled_as_future_help<
      FnRef, ArgRefTupRef, /*ArgRefTup=*/std::tuple<ArgRef...>
    >:
    apply_tupled_as_future_dispatch<
      FnRef, ArgRefTupRef,
      /*ArgIxs=*/detail::make_index_sequence<sizeof...(ArgRef)>,
      /*Return=*/detail::invoke_result_t<FnRef, ArgRef...>
    > {
  };
  
  template<typename FnRef, typename ArgRefTupRef>
  struct apply_tupled_as_future1: apply_tupled_as_future_help<FnRef,ArgRefTupRef> {};

  //////////////////////////////////////////////////////////////////////
  // future/fwd.hpp: detail::apply_futured_as_future

  template<typename FnRef, typename ArgFuRef,
           typename ArgFu = typename std::decay<ArgFuRef>::type>
  struct apply_futured_as_future_help;

  template<typename FnRef, typename ArgFuRef, typename Kind, typename ...T>
  struct apply_futured_as_future_help<FnRef, ArgFuRef, /*ArgFu=*/future1<Kind,T...>> {
    using tupled = apply_tupled_as_future_help<
      FnRef, /*ArgRefTupRef=*/decltype(std::declval<ArgFuRef>().impl_.result_refs_or_vals())&&
    >;
    
    using return_type = typename tupled::return_type;
    
    return_type operator()(FnRef fn, ArgFuRef arg) {
      return tupled()(
        static_cast<FnRef>(fn),
        static_cast<ArgFuRef>(arg).impl_.result_refs_or_vals()
      );
    }
    
    #if 0
    return_type operator()(FnRef fn, future_dependency<future1<Kind,T...>> const &arg) {
      return tupled()(static_cast<FnRef>(fn), arg.result_refs_or_vals());
    }
    #endif

    return_type operator()(FnRef fn, future_dependency<future1<Kind,T...>> &&arg) {
      return tupled()(static_cast<FnRef>(fn), static_cast<future_dependency<future1<Kind,T...>>&&>(arg).result_refs_or_vals());
    }
  };

  template<typename FnRef, typename ArgFuRef>
  struct apply_futured_as_future: apply_futured_as_future_help<FnRef,ArgFuRef> {};

  //////////////////////////////////////////////////////////////////////
  // detail::apply_as_future

  template<typename Fn, typename ...Arg>
  typename detail::apply_variadic_as_future<Fn&&, Arg&&...>::return_type
  apply_as_future(Fn &&fn, Arg &&...arg) {
    return detail::apply_variadic_as_future<Fn&&, Arg&&...>()(
      static_cast<Fn&&>(fn),
      static_cast<Arg&&>(arg)...
    );
  }

  // similar to apply_as_future(fn, arg...).then_lazy(then_fn), but
  // avoids future overheads when apply_as_future(fn, arg...) does not
  // return a future
  template<typename Fn, typename ThenFn, typename ...Arg>
  typename detail::apply_variadic_as_future<Fn&&, Arg&&...>
    ::template then_lazy_return_type<ThenFn&&>
  apply_as_future_then_lazy(Fn &&fn, ThenFn&& then_fn, Arg &&...arg) {
    return detail::apply_variadic_as_future<Fn&&, Arg&&...>().call_then_lazy(
      static_cast<Fn&&>(fn),
      static_cast<ThenFn&&>(then_fn),
      static_cast<Arg&&>(arg)...
    );
  }

  template<typename Fn, typename Args>
  typename detail::apply_tupled_as_future1<Fn&&,Args&&>::return_type
  apply_tupled_as_future(Fn &&fn, Args &&args) {
    return detail::apply_tupled_as_future1<Fn&&, Args&&>()(
      static_cast<Fn&&>(fn), static_cast<Args&&>(args)
    );
  }
}
}
#endif
