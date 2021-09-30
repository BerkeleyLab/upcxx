#ifndef _3e3a0cc8_bebc_49a7_9f94_53f023c2bd53
#define _3e3a0cc8_bebc_49a7_9f94_53f023c2bd53

#include <upcxx/future/core.hpp>
#include <upcxx/utility.hpp>

#include <initializer_list>

namespace upcxx {
  namespace detail {
    //////////////////////////////////////////////////////////////////////
    // future_is_trivially_ready: future_impl_when_all specialization

    template<typename ...Arg, typename ...T>
    struct future_is_trivially_ready<
        future1<detail::future_kind_when_all<Arg...>, T...>
      > {
      static constexpr bool value =
        detail::trait_forall<detail::future_is_trivially_ready, Arg...>::value;
    };
  
    ////////////////////////////////////////////////////////////////////
    // future_body_identity: Future body that holds a single dependency
    // and whose `leave_active()` routine just returns that dependencies
    // results as its own (like identity function).
    
    template<typename ArgFu>
    struct future_body_identity;

    ////////////////////////////////////////////////////////////////////
    // future_when_all_representative: If all the T... for a
    // future_impl_when_all<ArgTuple, T...> come from a single
    // underlying future, determine the index of that future in
    // ArgTuple. Otherwise produces -1.

    template<typename Indices, typename ArgTuple, typename ...T>
    struct future_when_all_representative {
      static constexpr int value = -1;
    };
    template<int i1, int ...i, typename FuArg1, typename ...FuArgs,
             typename ...T>
    struct future_when_all_representative<
      detail::index_sequence<i1, i...>, std::tuple<FuArg1, FuArgs...>, T...
    > {
      static constexpr int value =
        future_when_all_representative<
          detail::index_sequence<i...>, std::tuple<FuArgs...>, T...
        >::value;
    };
    template<int i1, int ...i, typename Kind, typename ...FuArgs,
             typename ...T>
    struct future_when_all_representative<
      detail::index_sequence<i1, i...>,
      std::tuple<future1<Kind, T...>, FuArgs...>, T...
    > {
      static constexpr int value = i1;
    };
    
    ////////////////////////////////////////////////////////////////////
    // future_impl_when_all: Future implementation concatenating
    // results of multiple futures.
    
    template<typename ArgTuple, typename ...T>
    struct future_impl_when_all;
    
    template<typename ...FuArg, typename ...T>
    struct future_impl_when_all<std::tuple<FuArg...>, T...> {
      std::tuple<FuArg...> args_;
    
    private:
      template<typename Bool0, typename ...Bools>
      static bool all_(Bool0 x0, Bools ...xs) { return x0 & all_(xs...); }
      static bool all_() { return true; }
      
      template<int ...i>
      bool ready_(detail::index_sequence<i...>) const {
        return all_(std::get<i>(this->args_).impl_.ready()...);
      }
      
      template<int ...i>
      auto result_refs_or_vals_(detail::index_sequence<i...>) const&
        UPCXXI_RETURN_DECLTYPE(std::tuple_cat(
            std::get<i>(this->args_).impl_.result_refs_or_vals()...
          )
        ) {
        return std::tuple_cat(
          std::get<i>(this->args_).impl_.result_refs_or_vals()...
        );
      }
      template<int ...i>
      auto result_refs_or_vals_(detail::index_sequence<i...>) &&
        UPCXXI_RETURN_DECLTYPE(std::tuple_cat(
            std::get<i>(std::move(this->args_)).impl_.result_refs_or_vals()...
          )
        ) {
        return std::tuple_cat(
          std::get<i>(std::move(this->args_)).impl_.result_refs_or_vals()...
        );
      }

      // Optimization for when all T... come from a single underlying
      // future. We can steal the header from that future if the other
      // futures are ready, since they do not contribute any values to
      // the result.
      template<int rep, int ...i>
      bool ready_all_but_rep_(detail::index_sequence<i...>) const {
        return all_((i == rep || std::get<i>(this->args_).impl_.ready())...);
      }

      template<int rep>
      future_header* steal_rep_header_(std::integral_constant<int,rep>) {
        if (ready_all_but_rep_<rep>(
              detail::make_index_sequence<sizeof...(FuArg)>())
           ) {
           return std::move(std::get<rep>(args_)).impl_.steal_header();
        }
        return nullptr;
      }

      future_header* steal_rep_header_(std::integral_constant<int,-1>) {
        return nullptr;
      }

      // Special-case optimization for when T... is empty. We see if
      // there is at most one non-ready future, and if so, steal its
      // header (or the first future's header if all of them are
      // ready).
      // We have to do two passes, since we can't call steal_header()
      // speculatively -- we have to first ensure that this
      // optimization is applicable, then do another pass to steal the
      // actual header.

      // Pass 1: simultaneously find the index of a non-ready future
      // and count the number of non-ready futures. The resulting
      // index is 0 if all futures are ready.
      template<typename Pair0, typename ...Pairs>
      static std::pair<int,int> find_empty_rep_(Pair0 x0, Pairs ...xs) {
        std::pair<int,int> rest = find_empty_rep_(xs...);
        return std::pair<int,int>{
          rest.second ? rest.first : x0.first,
          x0.second + rest.second
        };
      }
      static std::pair<int,int> find_empty_rep_() {
        return std::pair<int,int>{0,0};
      }

      // Pass 2: dynamically index into the set of futures and steal
      // the right header.
      template<typename Fu0, typename ...Fus>
      static future_header* steal_index_(int index, Fu0 &&f0, Fus&& ...fs) {
        return index ? steal_index_(index-1, fs...) :
          std::move(f0).impl_.steal_header();
      }
      static future_header* steal_index_(int index) {
        return nullptr;
      }

      // Overall logic for finding and stealing a header when all
      // futures are empty.
      template<int ...i>
      future_header* steal_rep_header_all_empty_(detail::index_sequence<i...>) {
        std::pair<int,int> index_count = find_empty_rep_(
          std::pair<int,int>{i, int(!std::get<i>(args_).impl_.ready())}...
        );
        if (index_count.second <= 1) {
          return steal_index_(index_count.first, std::get<i>(args_)...);
        }
        return nullptr;
      }

      // Special-case of no futures to avoid compilation errors in
      // that case.
      future_header* steal_rep_header_all_empty_(detail::index_sequence<>) {
        return nullptr;
      }
      
    public:
      template<typename ...FuArg1>
      future_impl_when_all(FuArg1 &&...args):
        args_(static_cast<FuArg1&&>(args)...) {
      }
      
      bool ready() const {
        return this->ready_(detail::make_index_sequence<sizeof...(FuArg)>());
      }
      
      auto result_refs_or_vals() const&
        UPCXXI_RETURN_DECLTYPE(this->result_refs_or_vals_(detail::make_index_sequence<sizeof...(FuArg)>())) {
        return this->result_refs_or_vals_(detail::make_index_sequence<sizeof...(FuArg)>());
      }
      auto result_refs_or_vals() &&
        UPCXXI_RETURN_DECLTYPE(std::move(*this).result_refs_or_vals_(detail::make_index_sequence<sizeof...(FuArg)>())) {
        return std::move(*this).result_refs_or_vals_(detail::make_index_sequence<sizeof...(FuArg)>());
      }
      
      typedef future_header_ops_general header_ops;
      
      future_header* steal_header() && {
        future_header *rep_header = nullptr;
        if (sizeof...(T) == 0 && sizeof...(FuArg) >= 1) {
          rep_header = steal_rep_header_all_empty_(
            detail::make_index_sequence<sizeof...(FuArg)>()
          );
        } else {
          constexpr int rep = future_when_all_representative<
            detail::make_index_sequence<sizeof...(FuArg)>,
            std::tuple<FuArg...>, T...
          >::value;
          rep_header = steal_rep_header_(std::integral_constant<int,rep>());
        }
        if (rep_header) return rep_header;

        future_header_dependent *hdr = new future_header_dependent;
        
        using body_type = future_body_identity<future1<future_kind_when_all<FuArg...>,T...>>;
        void *body_mem = body_type::operator new(sizeof(body_type));
        
        hdr->body_ = ::new(body_mem) body_type(
          body_mem, hdr,
          future1<future_kind_when_all<FuArg...>,T...>(
            static_cast<future_impl_when_all&&>(*this),
            detail::internal_only{}
          )
        );
        
        if(hdr->status_ == future_header::status_active)
          hdr->entered_active();
        
        return hdr;
      }
    };
    
    
    //////////////////////////////////////////////////////////////////////
    // future_dependency: future_impl_when_all specialization
    
    template<int i, typename Arg>
    struct future_dependency_when_all_arg {
      future_dependency<Arg> dep_;

      template<typename Arg1>
      future_dependency_when_all_arg(
          future_header_dependent *suc_hdr,
          Arg1 &&arg
        ):
        dep_(suc_hdr, static_cast<Arg1&&>(arg)) {
      }
    };
    
    template<typename AllArg, typename IxSeq>
    struct future_dependency_when_all_base;
    
    template<typename ...Arg, typename ...T, int ...i>
    struct future_dependency_when_all_base<
        future1<future_kind_when_all<Arg...>, T...>,
        detail::index_sequence<i...>
      >:
      // variadically inherit from each future_dependency specialization
      private future_dependency_when_all_arg<i,Arg>... {
      
      template<typename FuArg1>
      future_dependency_when_all_base(
          future_header_dependent *suc_hdr,
          /*future1<future_kind_when_all<Arg...>, T...>*/FuArg1 &&all_args
        ):
        future_dependency_when_all_arg<i,Arg>(
          suc_hdr,
          std::get<i>(static_cast<FuArg1&&>(all_args).impl_.args_)
        )... {
      }

      auto result_refs_or_vals() &&
        UPCXXI_RETURN_DECLTYPE(
          std::tuple_cat(
            static_cast<future_dependency_when_all_arg<i,Arg>&&>(*static_cast<future_dependency_when_all_arg<i,Arg>*>(this)).dep_.result_refs_or_vals()...
          )
        ) {
        return std::tuple_cat(
          static_cast<future_dependency_when_all_arg<i,Arg>&&>(*static_cast<future_dependency_when_all_arg<i,Arg>*>(this)).dep_.result_refs_or_vals()...
        );
      }
    };
    
    template<typename ...Arg, typename ...T>
    struct future_dependency<
        future1<future_kind_when_all<Arg...>, T...>
      >:
      future_dependency_when_all_base<
        future1<future_kind_when_all<Arg...>, T...>,
        detail::make_index_sequence<sizeof...(Arg)>
      > {

      template<typename Arg1>
      future_dependency(
          future_header_dependent *suc_hdr,
          /*future1<future_kind_when_all<Arg...>, T...>*/Arg1 &&arg
        ):
        future_dependency_when_all_base<
            future1<future_kind_when_all<Arg...>, T...>,
            detail::make_index_sequence<sizeof...(Arg)>
          >(suc_hdr, static_cast<Arg1&&>(arg)) {
      }
    };

    ////////////////////////////////////////////////////////////////////////////
    // future_body_identity
    
    template<typename Kind, typename ...T>
    struct future_body_identity<future1<Kind, T...>> final: future_body {
      future_dependency<future1<Kind, T...>> dep_;
      
    public:
      future_body_identity(
          void *storage,
          future_header_dependent *suc_hdr,
          future1<Kind,T...> &&arg
        ):
        future_body{storage},
        dep_(suc_hdr, static_cast<future1<Kind,T...>&&>(arg)) {
      }
      
      future_body_identity(
          void *storage,
          future_header_dependent *suc_hdr,
          future1<Kind,T...> const &arg
        ):
        future_body{storage},
        dep_(suc_hdr, arg) {
      }
      
      void leave_active(future_header_dependent *hdr) {
        void *storage = this->storage_;
        
        if(0 == hdr->decref(1)) { // dependent becoming ready loses ref
          this->~future_body_identity();
          future_body::operator delete(storage);
          delete hdr;
        }
        else {
          future_header *result = &(new future_header_result<T...>(
              /*not_ready=*/false,
              /*values=*/static_cast<future_dependency<future1<Kind,T...>>&&>(this->dep_).result_refs_or_vals()
            ))->base_header;

          this->~future_body_identity();
          future_body::operator delete(storage);
          
          hdr->enter_ready(result);
        }
      }
    };
  }
}
#endif
