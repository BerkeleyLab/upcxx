#ifndef _e004d4d0_de3d_4761_a53a_85652b347070
#define _e004d4d0_de3d_4761_a53a_85652b347070

#include <upcxx/future/core.hpp>
#include <upcxx/future/apply.hpp>
#include <upcxx/future/impl_then_lazy.hpp>

namespace upcxx {
  namespace detail {
    ////////////////////////////////////////////////////////////////////
    // future_body_then: Body type for then's
    
    /* future_body_then_base: base class for future_body_then.
     * separates non-lambda specific code away from lambda specific code
     * to encourage code sharing by the compiler.
     */
    struct future_body_then_base: future_body {
      future_body_then_base(void *storage): future_body(storage) {}

      // future_body_then::leave_active calls this after lambda evaluation
      template<typename Kind, typename ...T>
      void leave_active_into_proxy(
          future_header_dependent *hdr,
          void *storage,
          future1<Kind,T...> &&proxied
        ) {
        
        if(1 == hdr->ref_n_) {
          // only reference is the one for being not ready, so we die early
          operator delete(storage);
          delete hdr;
        }
        else {
          future_header *proxied_hdr = static_cast<future1<Kind,T...>&&>(proxied).impl_.steal_header();
          
          if(Kind::template with_types<T...>::header_ops::is_trivially_ready_result) {
            hdr->decref(1); // depedent becoming ready loses ref
            operator delete(storage); // body dead
            // we know proxied_hdr is its own result
            hdr->enter_ready(proxied_hdr);
          }
          else {
            hdr->enter_proxying(
              ::new(storage) future_body_proxy<T...>(storage),
              proxied_hdr
            );
          }
        }
      }
    };
    
    template<typename FuArg, typename Fn>
    struct future_body_then final: future_body_then_base {
      future_dependency<FuArg> dep_;
      Fn fn_;

      template<typename FuArg1, typename Fn1>
      future_body_then(
          void *storage, future_header_dependent *hdr,
          FuArg1 &&arg, Fn1 &&fn
        ):
        future_body_then_base(storage),
        dep_(hdr, static_cast<FuArg1&&>(arg)),
        fn_(static_cast<Fn1&&>(fn)) {
      }
      
      void leave_active(future_header_dependent *hdr) {
        auto proxied = apply_futured_as_future<Fn&&, FuArg&&>()(static_cast<Fn&&>(this->fn_), static_cast<future_dependency<FuArg>&&>(this->dep_));
        
        void *me_mem = this->storage_;
        this->~future_body_then();
        
        this->leave_active_into_proxy(hdr, me_mem, std::move(proxied));
      }

      template<typename Arg1, typename Fn1, typename ...FnRetT>
      static future_header_dependent* make_header(Arg1 &&arg, Fn1 &&fn) {
        future_header_dependent *hdr = new future_header_dependent;
        
        union body_union_t {
          future_body_then<FuArg,Fn> then;
          future_body_proxy<FnRetT...> proxy;
        };
        void *storage = future_body::operator new(sizeof(body_union_t));
        
        future_body_then<FuArg,Fn> *body =
          ::new(storage) future_body_then<FuArg,Fn>(
            storage, hdr,
            static_cast<Arg1&&>(arg),
            static_cast<Fn1&&>(fn)
          );
        hdr->body_ = body;
        
        if(hdr->status_ == future_header::status_active)
          hdr->entered_active();

        return hdr;
      }
    };
  } // namespace detail
  
  //////////////////////////////////////////////////////////////////////
  // future_then:
    
  namespace detail {
    template<typename Arg, typename Fn,
             typename FnRetKind, typename ...FnRetT>
    struct future_then<
        Arg, Fn, /*return_lazy=*/true,
        future1<FnRetKind,FnRetT...>, /*arg_trivial=*/false
      > {
      using return_type = future1<future_kind_then_lazy<Arg,Fn>, FnRetT...>;

      template<typename Arg1, typename Fn1>
      return_type operator()(Arg1 &&arg1, Fn1 &&fn1) {
        return future1<future_kind_then_lazy<Arg,Fn>, FnRetT...>(
          future_impl_then_lazy<Arg,Fn,FnRetT...>(static_cast<Arg1&&>(arg1), static_cast<Fn1&&>(fn1)),
          detail::internal_only{}
        );
      }
    };

    template<typename ArgArg, typename ArgFn, typename ...ArgT,
             typename Fn,
             typename FnRetKind, typename ...FnRetT>
    struct future_then<
        future1<future_kind_then_lazy<ArgArg, ArgFn>, ArgT...>,
        Fn, /*return_lazy=*/true,
        future1<FnRetKind,FnRetT...>, /*arg_trivial=*/false
      > {
      using return_type = typename future_impl_then_lazy<ArgArg,ArgFn,ArgT...>::template compose_under_return_type<Fn,FnRetT...>;

      template<typename Arg1, typename Fn1>
      return_type operator()(Arg1 &&arg1, Fn1 &&fn1) {
        return static_cast<Arg1&&>(arg1).impl_.template compose_under<Fn1, FnRetT...>(static_cast<Fn1&&>(fn1));
      }
    };

    template<typename Arg, typename Fn,
             typename FnRetKind, typename ...FnRetT>
    struct future_then<
        Arg, Fn, /*return_lazy=*/false,
        future1<FnRetKind,FnRetT...>, /*arg_trivial=*/false
      > {
      using return_type = future1<future_kind_shref<future_header_ops_dependent, /*unique=*/false>, FnRetT...>;
      
      template<typename Arg1, typename Fn1>
      return_type operator()(Arg1 &&arg, Fn1 &&fn) {
        auto *hdr = future_body_then<Arg,Fn>::template make_header<Arg1,Fn1,FnRetT...>(static_cast<Arg1&&>(arg), static_cast<Fn1&&>(fn));
        return return_type(
          future_impl_shref<future_header_ops_dependent, /*unique=*/false, FnRetT...>(hdr),
          detail::internal_only{}
        );
      }
    };
    
    template<typename ArgArg, typename ArgFn, typename ...ArgT,
             typename Fn,
             typename FnRetKind, typename ...FnRetT>
    struct future_then<
        future1<future_kind_then_lazy<ArgArg, ArgFn>, ArgT...>,
        Fn, /*return_lazy=*/false,
        future1<FnRetKind,FnRetT...>, /*arg_trivial=*/false
      > {
      using return_type = future1<future_kind_shref<future_header_ops_dependent, /*unique=*/false>, FnRetT...>;

      template<typename Arg1, typename Fn1>
      return_type operator()(Arg1 &&arg1, Fn1 &&fn1) {
        return return_type(
          future_impl_shref<future_header_ops_dependent, /*unique=*/false, FnRetT...>(
            static_cast<Arg1&&>(arg1).impl_.template compose_under<Fn1, FnRetT...>(static_cast<Fn1&&>(fn1)).impl_.steal_header()
          ),
          detail::internal_only{}
        );
      }
    };

    template<typename Arg, typename Fn, bool return_lazy,
             typename FnRetKind, typename ...FnRetT>
    struct future_then<
        Arg, Fn, return_lazy,
        future1<FnRetKind,FnRetT...>, /*arg_trivial=*/true
      > {

      using return_type = future1<FnRetKind,FnRetT...>;
      
      // Return type used to be: future1<future_kind_shref<future_header_ops_general>, FnRetT...>
      // Was there a reason for type-erasing it to the general kind?
      template<typename Arg1, typename Fn1>
      return_type operator()(Arg1 &&arg, Fn1 &&fn) {
        return apply_futured_as_future<Fn1&&, Arg1&&>()(
          static_cast<Fn1&&>(fn),
          static_cast<Arg1&&>(arg)
        );
      }
    };
  } // namespace detail

  // obsolete code for >> and >>= removed post 2021.3.0 release
}
#endif
