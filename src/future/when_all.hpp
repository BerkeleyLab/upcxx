#ifndef _eb1a60f5_4086_4689_a513_8486eacfd815
#define _eb1a60f5_4086_4689_a513_8486eacfd815

#include <upcxx/future/core.hpp>
#include <upcxx/future/impl_when_all.hpp>

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  // when_all()
  
  namespace detail {
    template<typename Ans, typename ...ArgDecayed>
    struct when_all_return_cat;
    
    template<typename Ans>
    struct when_all_return_cat<Ans> {
      using type = Ans;
    };

    template<typename AnsKind, typename ...AnsT,
             typename ArgKind, typename ...ArgT,
             typename ...MoreArgs>
    struct when_all_return_cat<
        /*Ans=*/future1<AnsKind, AnsT...>,
        /*ArgDecayed...=*/future1<ArgKind, ArgT...>, MoreArgs...
      > {
      using type = typename when_all_return_cat<
          future1<AnsKind, AnsT..., ArgT...>,
          MoreArgs...
        >::type;
    };

    template<typename Arg>
    struct when_all_arg_t { // non-future argument
      using type = future1<future_kind_result, Arg>;
    };

    template<typename ArgKind, typename ...ArgT>
    struct when_all_arg_t<future1<ArgKind, ArgT...>> { // future argument
      using type = future1<ArgKind, ArgT...>;
    };
    
    // compute return type of when_all
    template<typename ...Arg>
    using when_all_return_t = typename when_all_return_cat<
        future1<
          future_kind_when_all<
            typename when_all_arg_t<typename std::decay<Arg>::type>::type...
          >
          /*, empty T...*/
        >,
        typename when_all_arg_t<typename std::decay<Arg>::type>::type...
      >::type;

    template<typename ...ArgFu>
    when_all_return_t<ArgFu...> when_all_fast(ArgFu &&...arg) {
      return when_all_return_t<ArgFu...>(
        typename when_all_return_t<ArgFu...>::impl_type(
          to_fast_future(static_cast<ArgFu&&>(arg))...
        ),
        detail::internal_only{}
      );
    }
    // single component optimization
    template<typename ArgFu>
    auto when_all_fast(ArgFu &&arg) -> decltype(to_fast_future(arg)) {
      return to_fast_future(static_cast<ArgFu&&>(arg));
    }
    // zero component optimization
    inline auto when_all() -> decltype(detail::make_fast_future()) {
      return detail::make_fast_future();
    }
  }


  // Note: exactly the same as `detail::when_all_fast`. This could suprise users
  // since its what we have come to call a "spooky" type (not exactly the same
  // type as `upcxx::future<T...>`). FWIW I would advocate against forcing the
  // type to a regular future<T...> on the grounds of performance since it
  // seriously degrades the perf of `when_all(a,b).then([](A const&, B const &b) {...})`
  // by introducing an intermdiate heaped thunk (+1 allocation, +1 dispatch)
  // and copies of the A and B values. The spooky type elegantly avoids all that.
  // If we insist on purging spookyness, then I strongly adovcate us taking a look
  // at introducing something like `upcxx::then_when_all(<callable>, <futures>...)`
  // that is equivalent to `detail::when_all_fast(<futures>...).then(<callable>)`
  template<typename ...ArgFu>
  detail::when_all_return_t<ArgFu...> when_all(ArgFu &&...arg) {
    return detail::when_all_return_t<ArgFu...>(
      typename detail::when_all_return_t<ArgFu...>::impl_type(
        detail::to_fast_future(static_cast<ArgFu&&>(arg))...
      ),
      detail::internal_only{}
    );
  }
  // single component optimization
  template<typename ArgFu>
  auto when_all(ArgFu &&arg) -> decltype(detail::to_fast_future(arg)) {
    return detail::to_fast_future(static_cast<ArgFu&&>(arg));
  }
  // zero component optimization
  inline auto when_all() -> decltype(detail::make_fast_future()) {
    return detail::make_fast_future();
  }
  
}
#endif
