#ifndef _c1e666db_a184_42a1_8492_49b8a1d9259d
#define _c1e666db_a184_42a1_8492_49b8a1d9259d

#include <upcxx/future/core.hpp>
#include <upcxx/utility.hpp>

namespace upcxx {
  namespace detail {
    //////////////////////////////////////////////////////////////////////
    // future_is_trivially_ready: future_impl_result specialization

    template<typename ...T>
    struct future_is_trivially_ready<
        future1<detail::future_kind_result, T...>
      > {
      static constexpr bool value = true;
    };
  
    ////////////////////////////////////////////////////////////////////
    // future_impl_result: implementation of a trivially ready value
    
    template<typename ...T>
    struct future_impl_result {
      std::tuple<T...> results_;
      
    public:
      future_impl_result(T &&...values):
        results_{std::forward<T>(values)...} {
      }
      
      bool ready() const { return true; }
      
      detail::tuple_refs_return_t<std::tuple<T...> const&>
      result_refs_or_vals() const& {
        return detail::tuple_refs(results_);
      }
      detail::tuple_refs_return_t<std::tuple<T...>&&>
      result_refs_or_vals() && {
        return detail::tuple_refs(std::move(results_));
      }
      
      typedef future_header_ops_result_ready header_ops;
      
      future_header* steal_header() && {
        return &(new future_header_result<T...>(0, std::move(results_)))->base_header;
      }
    };
    
    template<>
    struct future_impl_result<> {
      bool ready() const { return true; }
      
      std::tuple<> result_refs_or_vals() const { return std::tuple<>(); }
      
      typedef future_header_ops_result_ready header_ops;
      
      future_header* steal_header() && {
        return future_header_result<>::always();
      }
    };
    
    
    ////////////////////////////////////////////////////////////////////
    // future_dependency: future_impl_result specialization
    
    template<typename ...T>
    struct future_dependency<
        future1<future_kind_result, T...>
      > {
      std::tuple<T...> results_;
      
      future_dependency(
          future_header_dependent *suc_hdr,
          future1<future_kind_result, T...> &&arg
        ):
        results_(static_cast<std::tuple<T...>&&>(arg.impl_.results_)) {
      }
      future_dependency(
          future_header_dependent *suc_hdr,
          future1<future_kind_result, T...> const &arg
        ):
        results_(arg.impl_.results_) {
      }
      
      detail::tuple_refs_return_t<std::tuple<T...>&&>
      result_refs_or_vals() && {
        return detail::tuple_refs(std::move(results_));
      }
    };
    
    template<>
    struct future_dependency<
        future1<future_kind_result>
      > {
      future_dependency(
          future_header_dependent *suc_hdr,
          future1<future_kind_result> arg
        ) {
      }
      
      std::tuple<> result_refs_or_vals() && {
        return std::tuple<>();
      }
    };
  }
}  
#endif
