#ifndef _1f14e4c6_c238_4eb1_b9aa_5f0732405698
#define _1f14e4c6_c238_4eb1_b9aa_5f0732405698

#include <upcxx/future/core.hpp>
#include <upcxx/future/apply.hpp>
#include <upcxx/utility.hpp>

/* future_impl_then_lazy<FuArg, Fn, ...T>: Statically encodes that this future
 * was produced by a "then" such as "FuArg::then_lazy(Fn)" and does not
 * immediately materialize the callback dependency node but instead just holds
 * the argument and callback as separate umodified fields. The only two ways
 * the callback is actually registered with the runtime are if this impl gets
 * its header stolen via "steal_header()" or it is destructed (this is the lazy
 * aspect for which it is named).
 *
 * By being lazy about materializing runtime nodes, the templates are able to
 * "see" optimizations to avoid intermediate nodes, examples:
 *
 *   1) a.then_lazy(f).then_lazy(g);
 *      If f returns a trivially ready future, then a single callback node can be
 *      used to executed g(f(x)).
 *
 *   2) when_all(a.then_lazy(f), b.then_lazy(g)).then_lazy(h);
 *      If f (or g) returns a trivially ready future, then it (or both) can be
 *      invoked in the outer callback just before invoking h.
 * 
 *   3) a.then_lazy([](arg) { return f(arg).then_lazy(g); }).then_lazy(h);
 *      If g returns a trivially ready future, then h can be moved inside the
 *      lambda and fused with g to remove a callback node.
 *
 * Cases 1 & 3 are handled by specializations of `detail::future_then` where
 * argument future is of kind `future_kind_then_lazy`.
 * 
 * Case 2 (and 1 also, except beaten by above) is handled by
 * `detail::future_dependency<future1<future_kind_then_lazy<...>, T...>>` down in
 * this file.
 *
 * This impl type comes with restrictions and surprises which is why only
 * future1::then_lazy invokes it:
 * 
 *   1. The callback isn't registered until the future is destroyed or casted
 *   to the generic kind, users must take care not to hold on this future
 *   unnecesssarily.
 * 
 *   2. This impl type is not copyable. Copying it before its runtime callback
 *   is materialized would result in registering redundant runtime callbacks
 *   as the copies are destroyed, which would be especially terrible if the
 *   callback had side-effects. The fix to this is possible, we could implement a
 *   tagged-union where the impl is either the sepearated arg & callback, or a
 *   shared reference to a materialized runtime node. Copying would promote the
 *   former to the latter and then just copy the node reference. This would
 *   incur a tag branch in all of the impl's member functions, but a much worse
 *   consequence is that a sloppy user who accidentally copied this impl before
 *   chaining on another "then" or "then_lazy" would inadvertantly thwart the
 *   primary optimization transformation since the copy would obfuscate the
 *   inner callback behind a runtime node. So we make this impl move-only, thus
 *   minimizing runtime branches on a tag bit, and even better, statically
 *   enforcing that the arg and callback can be kept seperate to be fused with
 *   later chained thens which is the whole point.
 *
 * Because of these restrictions, it is not a full-serivce impl and therefor it
 * breaks the implementation of several future1 methods which effectively
 * restricts the future1 as well. Such future1's cannot be copied, they cannot
 * have their results queried, you can only chain more then's, steal the header,
 * or destroy it, all three of which take `this` as &&.
 */

namespace upcxx {
  namespace detail {
    //////////////////////////////////////////////////////////////////////////////
    // future_is_trivially_ready: future_impl_then_lazy specialization

    template<typename FuArg, typename Fn, typename ...T>
    struct future_is_trivially_ready<
        future1<detail::future_kind_then_lazy<FuArg,Fn>,T...>
      > {
      static constexpr bool value = false;
    };
  
    template<typename Fn1, typename Fn2>
    struct future_composite_fn;
    
    ////////////////////////////////////////////////////////////////////////////
    // future_impl_then_lazy:
    
    template<typename FuArg, typename Fn, typename ...T>
    struct future_impl_then_lazy {
      static_assert(!future_is_trivially_ready<FuArg>::value, "Can't make a future_impl_then_lazy from a trivially ready argument.");

      FuArg arg_;
      Fn fn_;
      bool must_materialize_; // are we responsible for materializing the runtime header at destruction?
    
    public:
      template<typename FuArg1, typename Fn1>
      future_impl_then_lazy(FuArg1 &&arg, Fn1 &&fn) noexcept:
        arg_(static_cast<FuArg1&&>(arg)),
        fn_(static_cast<Fn1&&>(fn)),
        must_materialize_(true) {
      }

      future_impl_then_lazy(future_impl_then_lazy const&) noexcept = delete;
      future_impl_then_lazy(future_impl_then_lazy &&that) noexcept:
        arg_(static_cast<FuArg&&>(that.arg_)),
        fn_(static_cast<Fn&&>(that.fn_)),
        must_materialize_(that.must_materialize_) {
        that.must_materialize_ = false;
      }
      
      // THESE ARE NOT IMPLEMENTED! This is intentional since we cant implement them
      // without incurring the requirement that we track (and refcount) the header
      // we materialize. We still have to provide prototypes wo that decltype
      // logic invoking these methods can see the right types.
      bool ready() const;
      std::tuple<T...> result_refs_or_vals() const&;
      //std::tuple<T...> result_refs_or_vals() &&;
      
      using header_ops = future_header_ops_dependent;

      future_header_dependent* steal_header() && {
        UPCXX_ASSERT(must_materialize_);
        must_materialize_ = false;
        return future_body_then<FuArg,Fn>::template make_header<FuArg,Fn,T...>(static_cast<FuArg&&>(arg_), static_cast<Fn&&>(fn_));
      }

      ~future_impl_then_lazy() {
        if(must_materialize_) {
          auto *hdr = static_cast<future_impl_then_lazy&&>(*this).steal_header();
          header_ops::template dropref<T...>(hdr, /*maybe_nil=*/std::false_type());
        }
      }

      template<typename Fn2, typename ...Fn2RetT>
      using compose_under_return_type = future1<future_kind_then_lazy<FuArg, future_composite_fn<Fn,Fn2>>, Fn2RetT...>;

      // Given that we represent the expression `arg.then(fn1)`, build a new one
      // that represents `arg.then(fn1).then(fn2)`, except we actually build
      // `arg.then([](T x) { return fn1(x).then(fn2); })` which is
      // semantically equivalent but allows our templates to do optimizations
      // based on the return type of fn1.
      template<typename Fn2Ref, typename ...Fn2RetT>
      future1<future_kind_then_lazy<FuArg, future_composite_fn<Fn,typename std::decay<Fn2Ref>::type>>, Fn2RetT...>
      compose_under(Fn2Ref &&fn2) && {
        UPCXX_ASSERT(must_materialize_);
        must_materialize_ = false;
        
        return {
          future_impl_then_lazy<FuArg, future_composite_fn<Fn,typename std::decay<Fn2Ref>::type>, Fn2RetT...>(
            static_cast<FuArg&&>(arg_),
            future_composite_fn<Fn,typename std::decay<Fn2Ref>::type>{
              static_cast<Fn&&>(fn_), static_cast<Fn2Ref&&>(fn2)
            }
          ),
          detail::internal_only{}
        };
      }
    };
    
    
    ////////////////////////////////////////////////////////////////////////////
    // future_dependency: `future_impl_then_lazy` specialization. We use the
    // `future_dependency_then_lazy` base class to dispatch on whether the function's
    // result is trivially ready. If so, then the dependency can just invoke the
    // funciton inline in `result_refs_or_vals()`, otherwise the dependency materializes
    // the "then" header and acts like a `future_kind_shref` dependency. This addresses
    // optimizations for examples 1 and 2 from the description at the top.
    
    template<typename FuArg, typename Fn, bool trivially_ready_result, typename ...T>
    struct future_dependency_then_lazy;

    // specialized case for Fn returning trivially ready result
    template<typename FuArg, typename Fn, typename ...T>
    struct future_dependency_then_lazy<FuArg,Fn,/*trivially_ready_result=*/true, T...> {
      future_dependency<FuArg> arg_dep_;
      Fn fn_;
      
      future_dependency_then_lazy(
          future_header_dependent *suc_hdr,
          future1<future_kind_then_lazy<FuArg,Fn>, T...> &&arg
        ):
        arg_dep_(suc_hdr, static_cast<FuArg&&>(arg.impl_.arg_)),
        fn_(static_cast<Fn&&>(arg.impl_.fn_)) {

        // we just stole the arg & fn so disable materialization
        UPCXX_ASSERT(arg.impl_.must_materialize_);
        arg.impl_.must_materialize_ = false;
      }

      std::tuple<T...> result_refs_or_vals() && {
        return detail::apply_futured_as_future<Fn&&,FuArg>()(
          static_cast<Fn&&>(fn_),
          static_cast<future_dependency<FuArg>&&>(arg_dep_)
        ).result_tuple();
      }
    };

    // Specialization when Fn does not return trivially ready result. Materialize
    // the header and defer to future_kind_shref dependency logic.
    template<typename FuArg, typename Fn, typename ...T>
    struct future_dependency_then_lazy<FuArg,Fn,/*trivially_ready_result=*/false, T...>:
      future_dependency<
        future1<future_kind_shref<future_header_ops_dependent,/*unique=*/true>, T...>
      > {
      
      future_dependency_then_lazy(
          future_header_dependent *suc_hdr,
          future1<future_kind_then_lazy<FuArg,Fn>, T...> &&arg
        ):
        future_dependency<
          future1<future_kind_shref<future_header_ops_dependent,/*unique=*/true>, T...>
        >(suc_hdr,
          static_cast<future1<future_kind_then_lazy<FuArg,Fn>, T...>&&>(arg).impl_.steal_header()
        ) {
      }
    };

    // Punt to base class
    template<typename FuArg, typename Fn, typename ...T>
    struct future_dependency<
        future1<future_kind_then_lazy<FuArg,Fn>, T...>
      >:
      future_dependency_then_lazy<
        FuArg, Fn,
        future_is_trivially_ready<typename apply_futured_as_future<Fn,FuArg>::return_type>::value,
        T...
      > {
      
      future_dependency(
          future_header_dependent *suc_hdr,
          future1<future_kind_then_lazy<FuArg,Fn>, T...> &&arg
        ):
        future_dependency_then_lazy<
            FuArg, Fn,
            future_is_trivially_ready<typename apply_futured_as_future<Fn,FuArg>::return_type>::value,
            T...
          >(
          suc_hdr,
          static_cast<future1<future_kind_then_lazy<FuArg,Fn>, T...>&&>(arg)
        ) {
      }
    };

    ////////////////////////////////////////////////////////////////////////////
    // future_composite_fn

    template<typename Fn1, typename Fn2>
    struct future_composite_fn {
      Fn1 fn1;
      Fn2 fn2;

      template<typename ...Arg>
      typename detail::future_then<
          typename detail::apply_variadic_as_future<Fn1&&,Arg&&...>::return_type,
          Fn2, /*return_lazy=*/true
        >::return_type
      operator()(Arg &&...arg) && {
        using apply_t = detail::apply_variadic_as_future<Fn1&&,Arg&&...>;
        return detail::future_then<typename apply_t::return_type, Fn2, /*return_lazy=*/true>()(
          apply_t()(
            static_cast<Fn1&&>(fn1),
            static_cast<Arg&&>(arg)...
          ),
          static_cast<Fn2&&>(fn2)
        );
      }
    };
  }
}
#endif
