#ifndef _69b9dd2a_13a0_4c70_af49_33621d57d3bf
#define _69b9dd2a_13a0_4c70_af49_33621d57d3bf

#include <upcxx/future/impl_result.hpp>
#include <upcxx/future/impl_shref.hpp>
#include <upcxx/utility.hpp>

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  // make_future()
  
  template<typename ...T>
  future<T...> make_future(T ...values) {
    return future<T...>(
      detail::future_impl_shref<detail::future_header_ops_general, /*unique=*/false, T...>(
        new detail::future_header_result<T...>{
          /*not_ready*/false,
          /*values*/std::tuple<T...>{static_cast<T&&>(values)...}
        }
      ),
      detail::internal_only{}
    );
  }

  template<>
  inline future<> make_future<>() {
    // double check that the_always is sane
    UPCXX_ASSERT(detail::future_header_result<>::always()->ref_n_ == -1);
    return future<>(
      detail::future_impl_shref<detail::future_header_ops_general, /*unique=*/false>(
        detail::future_header_result<>::always() // skip header allocation
      ),
      detail::internal_only{}
    );
  }
  
  //////////////////////////////////////////////////////////////////////
  // detail::make_fast_future()
  
  namespace detail {
    template<typename ...T>
    future1<future_kind_result, T...> make_fast_future(T ...values) {
      return future1<future_kind_result, T...>(
        future_impl_result<T...>(static_cast<T&&>(values)...),
        detail::internal_only{}
      );
    }
  }
  
  //////////////////////////////////////////////////////////////////////
  // to_future()
  
  template<typename T>
  future<T> to_future(T x) {
    return make_future(static_cast<T&&>(x));
  }
  
  template<typename Kind, typename ...T>
  detail::future1<Kind,T...>&& to_future(detail::future1<Kind,T...> &&x) {
    return static_cast<detail::future1<Kind,T...>&&>(x);
  }
  template<typename Kind, typename ...T>
  detail::future1<Kind,T...> const& to_future(detail::future1<Kind,T...> const &x) {
    return x;
  }

  //////////////////////////////////////////////////////////////////////
  // detail::to_fast_future()
  
  namespace detail {
    template<typename T>
    future1<future_kind_result, T> to_fast_future(T x) {
      return future1<future_kind_result, T>(
        future_impl_result<T>(static_cast<T&&>(x)),
        detail::internal_only{}
      );
    }
    
    template<typename Kind, typename ...T>
    future1<Kind,T...>&& to_fast_future(future1<Kind,T...> &&x) {
      return static_cast<future1<Kind,T...>&&>(x);
    }
    template<typename Kind, typename ...T>
    future1<Kind,T...> const& to_fast_future(future1<Kind,T...> const &x) {
      return x;
    }
  }
}
#endif
