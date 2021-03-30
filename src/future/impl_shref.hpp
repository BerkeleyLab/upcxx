#ifndef _9a741fd1_dcc1_4fe9_829f_ae86fa6b86ab
#define _9a741fd1_dcc1_4fe9_829f_ae86fa6b86ab

#include <upcxx/future/core.hpp>
#include <upcxx/utility.hpp>

// TODO: Consider adding debug checks for operations against a moved-out of
// future_impl_shref.

namespace upcxx {
  namespace detail {
    //////////////////////////////////////////////////////////////////////
    // future_is_trivially_ready: future_impl_shref specialization

    template<typename HeaderOps, bool unique, typename ...T>
    struct future_is_trivially_ready<
        future1<detail::future_kind_shref<HeaderOps,unique>, T...>
      > {
      static constexpr bool value = HeaderOps::is_trivially_ready_result;
    };
  
    ////////////////////////////////////////////////////////////////////
    // future_impl_shref: Future implementation using ref-counted
    // pointer to header.
    
    template<typename HeaderOps, bool unique, typename ...T>
    struct future_impl_shref {
      future_header *hdr_;
    
    public:
      future_impl_shref() noexcept {
        this->hdr_ = future_header_nil::nil();
      }
      // hdr comes comes with a reference included for us
      future_impl_shref(future_header *hdr) noexcept {
        this->hdr_ = hdr;
      }
      // hdr comes comes with a reference included for us
      future_impl_shref(future_header_result<T...> *hdr) noexcept {
        this->hdr_ = &hdr->base_header;
      }
      
      template<typename HeaderOps1, bool unique1>
      future_impl_shref(
          future_impl_shref<HeaderOps1,unique1,T...> const &that,
          typename std::enable_if<std::is_base_of<HeaderOps,HeaderOps1>::value && !unique && !unique1>::type* = 0
        ) noexcept {
        this->hdr_ = that.hdr_;
        HeaderOps1::incref(this->hdr_);
      }

      future_impl_shref(future_impl_shref const &that) noexcept:
        future_impl_shref(that, nullptr) {
      }
      
      template<typename HeaderOps1, bool unique1>
      typename std::enable_if<
          std::is_base_of<HeaderOps,HeaderOps1>::value && !unique && !unique1,
          future_impl_shref&
        >::type
      operator=(future_impl_shref<HeaderOps1,unique1,T...> const &that) noexcept {
        future_header *this_hdr = this->hdr_;
        future_header *that_hdr = that.hdr_;
        
        this->hdr_  = that_hdr;
        
        HeaderOps1::incref(that_hdr);
        HeaderOps::template dropref<T...>(this_hdr, /*maybe_nil=*/std::true_type());
        
        return *this;
      }
      
      future_impl_shref& operator=(future_impl_shref const &that) noexcept {
        return this->template operator= <HeaderOps,unique>(that);
      }
      
      template<typename Ops1, bool unique1>
      future_impl_shref(
          future_impl_shref<Ops1,unique1,T...> &&that,
          typename std::enable_if<std::is_base_of<HeaderOps,Ops1>::value && (!unique || unique1)>::type* = 0
        ) noexcept {
        this->hdr_ = that.hdr_;
        that.hdr_ = future_header_nil::nil();
      }
      future_impl_shref(future_impl_shref &&that) noexcept:
        future_impl_shref(static_cast<future_impl_shref&&>(that), nullptr) {
      }
      
      future_impl_shref& operator=(future_impl_shref &&that) noexcept {
        future_header *tmp = this->hdr_;
        this->hdr_ = that.hdr_;
        that.hdr_ = tmp;
        return *this;
      }
      
      // build from some other future implementation
      template<typename Impl,
               typename = typename std::enable_if<
                 future_impl_traits<Impl>::is_impl &&
                 !std::is_lvalue_reference<Impl>::value &&
                 !std::is_const<Impl>::value
               >::type>
      future_impl_shref(Impl &&that) noexcept {
        this->hdr_ = static_cast<Impl&&>(that).steal_header();
      }
      
      ~future_impl_shref() {
        HeaderOps::template dropref<T...>(this->hdr_, /*maybe_nil=*/std::true_type());
      }
    
    public:
      bool ready() const {
        return HeaderOps::is_trivially_ready_result || hdr_->status_ == future_header::status_ready;
      }
      
      detail::tuple_refs_return_t<std::tuple<T...> const&>
      result_refs_or_vals() const& {
        return detail::tuple_refs(
          static_cast<std::tuple<T...> const&>(
            future_header_result<T...>::results_of(hdr_->result_)
          )
        );
      }

      detail::tuple_refs_return_t<
        typename std::conditional<unique, std::tuple<T...>&&, std::tuple<T...> const&>::type
      >
      result_refs_or_vals() && {
        return detail::tuple_refs(
          static_cast<typename std::conditional<unique, std::tuple<T...>&&, std::tuple<T...> const&>::type>(
            future_header_result<T...>::results_of(hdr_->result_)
          )
        );
      }
      
      typedef HeaderOps header_ops;
      
      future_header* steal_header() && {
        future_header *hdr = this->hdr_;
        this->hdr_ = future_header_nil::nil();
        return hdr;
      }
    };
    
    
    //////////////////////////////////////////////////////////////////////
    // future_dependency: future_impl_shref specialization
    
    template<typename HeaderOps,
             bool is_trivially_ready_result = HeaderOps::is_trivially_ready_result>
    struct future_dependency_shref_base;
    
    template<typename HeaderOps>
    struct future_dependency_shref_base<HeaderOps, /*is_trivially_ready_result=*/false> {
      future_header::dependency_link link_;
      
      future_dependency_shref_base(
          future_header_dependent *suc_hdr,
          future_header *arg_hdr
        ) {

        if(HeaderOps::is_possibly_dependent && (
           arg_hdr->status_ == future_header::status_proxying ||
           arg_hdr->status_ == future_header::status_proxying_active
          )) {
          arg_hdr = future_header::drop_for_proxied(arg_hdr);
        }
        
        if(arg_hdr->status_ != future_header::status_ready) {
          UPCXX_ASSERT(arg_hdr != &future_header_nil1<>::the_nil,
            "You have created a future that is waiting on the never-ready future.\n"
            "This is probably not what you intended as this future will never be\n"
            "ready either. If this future was produced by 'then()', then the callback\n"
            "will never be executed and the heap memory dedicated to hold that\n"
            "callback is forever leaked.");

          this->link_.suc = suc_hdr;
          this->link_.dep = arg_hdr;
          this->link_.sucs_next = arg_hdr->sucs_head_;
          arg_hdr->sucs_head_ = &this->link_;
          
          suc_hdr->status_ += 1;
        }
        else
          this->link_.dep = future_header::drop_for_result(arg_hdr);
      }
      
      future_dependency_shref_base(future_dependency_shref_base const&) = delete;
      future_dependency_shref_base(future_dependency_shref_base&&) = delete;
      
      future_header* header_() const { return link_.dep; }
    };
    
    template<typename HeaderOps>
    struct future_dependency_shref_base<HeaderOps, /*is_trivially_ready_result=*/true> {
      future_header *hdr_;
      
      future_dependency_shref_base(
          future_header_dependent *suc_hdr,
          future_header *arg_hdr
        ) {
        hdr_ = arg_hdr;
      }
      
      future_header* header_() const { return hdr_; }
    };
    
    template<typename HeaderOps, bool unique, typename ...T>
    struct future_dependency<
        future1<future_kind_shref<HeaderOps,unique>, T...>
      >:
      future_dependency_shref_base<HeaderOps> {
      
      future_dependency(
          future_header_dependent *suc_hdr, future_header *arg_hdr
        ):
        future_dependency_shref_base<HeaderOps>{suc_hdr, arg_hdr} {
      }
      future_dependency(
          future_header_dependent *suc_hdr,
          future1<future_kind_shref<HeaderOps,unique>, T...> arg
        ):
        future_dependency(
          suc_hdr,
          static_cast<future1<future_kind_shref<HeaderOps,unique>, T...>&&>(arg).impl_.steal_header()
        ) {
      }
      
      ~future_dependency() {
        future_header *h = this->header_();
        future_header_ops_result_ready::template dropref<T...>(h, /*maybe_nil*/std::false_type());
      }

      detail::tuple_refs_return_t<
        typename std::conditional<unique, std::tuple<T...>&&, std::tuple<T...> const&>::type
      >
      result_refs_or_vals() && {
        return detail::tuple_refs(
          static_cast<typename std::conditional<unique, std::tuple<T...>&&, std::tuple<T...> const&>::type>(
            future_header_result<T...>::results_of(this->header_())
          )
        );
      }
    };
  }
}
#endif
