#ifndef _502a1373_151a_4d68_96d9_32ae89053988
#define _502a1373_151a_4d68_96d9_32ae89053988

#include <upcxx/future/core.hpp>
#include <upcxx/future/impl_shref.hpp>

#include <cstdint>
#include <cstddef>

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  // detail::promise_like_t: generate type promise<T...> given some
  // future<T...>
  
  namespace detail {
    template<typename Fu>
    struct promise_like;
    template<typename Kind, typename ...T>
    struct promise_like<future1<Kind,T...>> {
      using type = promise<T...>;
    };
    template<typename Fu>
    using promise_like_t = typename promise_like<Fu>::type;
  }
  
  //////////////////////////////////////////////////////////////////////
  /* detail::promise_shref: Reference counted promise class. Default value
   * is a new promise with 1 outstanding dependency. Has all the members that
   * `upcxx::promise` does, plus the internal-only accessors to get at the
   * underlying header thunks. `upcxx::promise` is then a thin veil inheriting from
   * this but hiding the header accessors. `promise_shref` *could* be used
   * throughout the runtime but actually isn't out of fear that the compiler
   * can't eliminate all the refcounting overhead that we would like it to.
   * So instead, `future_header_promise<T...>*`'s are the usual currency of the
   * runtime with onus on us to handle the refcounting manually.
   *
   * Example of refcounting the compiler *may not* be able to elide:
   *
   * void foo(promise_shref &a) {
   *   promise_shref b(a); // copy ctor: aliases a, increments refcount
   *   return;
   *   // now b's destructor runs which decrements the shared refcount and tests
   *   // its equality to zero. But we know that the refcount can't be zero
   *   // because of the invariant that a's refcount was greater than zero
   *   // upon entry. This can't be expressed to the compiler so it will be
   *   // unlikely to prove such a difficult invariant unguided.
   * }
   *
   * When doing refcounting manually with `future_head_promise*`'s, use these:
   * 
   *   // allocates with 1 oustanding dependency
   *   new future_header_promise<T...>;
   *
   *   // increments refcount by n (non-negative) and returns self "this" pointer
   *   future_header_promise<T...>::incref(int n);
   * 
   *   // decrements refcount by 1 and deletes if zero
   *   future_header_promise<T...>::dropref();
   *
   *   // like like-named upcxx::promise members:
   *   detail::promise_require_anonymous(future_header_promise<T...>*, intptr_t);
   *   detail::promise_fulfill_anonymous(future_header_promise<T...>*, intptr_t);
   *   detail::promise_fulfill_result(future_header_promise<T...>*, tuple<T...>);
   *   future<T...> detail::promise_get_future(future_header_promise<T...>*);
   */
  namespace detail {
    template<typename ...T>
    struct promise_shref;

    // Gain access to the privately inherited detail::promise_shref base class
    // of a upcxx::promise.
    template<typename ...T>
    promise_shref<T...>& promise_as_shref(promise<T...> &pro);
    
    // Do the tricky cast of a promise_meta pointer to its enclosing header.
    template<typename ...T>
    future_header_promise<T...>* promise_header_of(promise_meta *meta) {
      return (future_header_promise<T...>*)((char*)meta - offsetof(future_header_promise<T...>, pro_meta));
    }
    
    template<typename ...T, typename ...U>
    void promise_fulfill_result(future_header_promise<T...> *hdr, std::tuple<U...> &&values) {
      UPCXX_ASSERT(
        hdr->base_header_result.results_constructible(),
        "Attempted to call `fulfill_result` multiple times on the same promise."
      );
      hdr->base_header_result.construct_results(std::move(values));
      hdr->fulfill(1);
    }

    template<typename ...T>
    void promise_require_anonymous(future_header_promise<T...> *hdr, std::intptr_t n) {
      UPCXX_ASSERT(hdr->pro_meta.countdown > 0,
        "Called `require_anonymous()` on a ready promise.");
      UPCXX_ASSERT(n >= 0,
        "Calling `require_anonymous()` with a negative value ("<<n<<") is undefined behavior.");
      
      hdr->pro_meta.countdown += n;
    }

    template<typename ...T>
    void promise_fulfill_anonymous(future_header_promise<T...> *hdr, std::intptr_t n) {
      UPCXX_ASSERT(n >= 0,
        "Calling `fulfill_anonymous()` with a negative value ("<<n<<") is undefined behavior.");
      hdr->fulfill(n);
    }

    template<typename ...T>
    future<T...> promise_get_future(future_header_promise<T...> *hdr) {
      hdr->incref(1);
      return future<T...>(
        detail::future_impl_shref<detail::future_header_ops_general, /*unique=*/false, T...>(&hdr->base_header_result.base_header),
        detail::internal_only{}
      );
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // detail::promise_shref implementation

  namespace detail {
    template<typename ...T>
    struct promise_shref:
        detail::future_impl_shref<detail::future_header_ops_promise, /*unique=*/false, T...> {

      promise_shref(detail::future_header_promise<T...> *hdr/*takes ref*/):
        detail::future_impl_shref<
          detail::future_header_ops_promise, /*unique=*/false, T...
        >(&hdr->base_header_result) {
      }

      promise_shref(std::intptr_t deps=1):
        promise_shref(new detail::future_header_promise<T...>) {
        UPCXX_ASSERT(deps >= 1);
        this->header()->pro_meta.countdown = deps;
      }
      
      future_header_promise<T...>* header() const {
        return reinterpret_cast<future_header_promise<T...>*>(this->hdr_);
      }

      future_header_promise<T...>* header_with_incref(int n) const {
        this->hdr_->incref(n);
        return reinterpret_cast<future_header_promise<T...>*>(this->hdr_);
      }
      
      future_header_promise<T...>* steal_header() && {
        return reinterpret_cast<future_header_promise<T...>*>(
          static_cast<promise_shref&&>(*this).detail::future_impl_shref<detail::future_header_ops_promise, /*unique=*/false, T...>::steal_header()
        );
      }

      void require_anonymous(std::intptr_t n) const {
        detail::promise_require_anonymous(this->header(), n);
      }
      
      void fulfill_anonymous(std::intptr_t n) const {
        detail::promise_fulfill_anonymous(this->header(), n);
      }
      
      template<typename ...U>
      void fulfill_result(U &&...values) const {
        detail::promise_fulfill_result(this->header(), std::tuple<U&&...>(std::forward<U>(values)...));
      }
      
      template<typename ...U>
      void fulfill_result(std::tuple<U...> &&values) const {
        detail::promise_fulfill_result(this->header(), std::move(values));
      }
      
      future<T...> finalize() const {
        future_header_promise<T...> *hdr = this->header();
        hdr->fulfill(1);
        hdr->incref(1);
        return future<T...>(
          typename future<T...>::impl_type(&hdr->base_header_result.base_header),
          detail::internal_only{}
        );
      }
      
      future<T...> get_future() const {
        future_header_promise<T...> *hdr = this->header();
        hdr->incref(1);
        return future<T...>(
          typename future<T...>::impl_type(&hdr->base_header_result.base_header),
          detail::internal_only{}
        );
      }
    };
  }
    
  //////////////////////////////////////////////////////////////////////////////
  // promise implementation
  
  template<typename ...T>
  class promise: private detail::promise_shref<T...> {
    // workaround a bug in cudafe++, see issue #274 for details
    using promise_cfe = upcxx::promise<T...>;

    friend detail::promise_shref<T...>& detail::promise_as_shref<T...>(promise_cfe&);
    
    promise(detail::future_header *hdr): detail::promise_shref<T...>(hdr) {}
    
  public:
    promise(std::intptr_t deps=1): detail::promise_shref<T...>(deps) {}
    
    promise(promise const&) = default;
    promise(promise&&) = default;
    promise& operator=(promise const&) = default;
    promise& operator=(promise&&) = default;
    
    using detail::promise_shref<T...>::require_anonymous;
    using detail::promise_shref<T...>::fulfill_anonymous;
    using detail::promise_shref<T...>::fulfill_result;
    using detail::promise_shref<T...>::finalize;
    using detail::promise_shref<T...>::get_future;
  };
  
  //////////////////////////////////////////////////////////////////////////////
  
  namespace detail {
    template<typename ...T>
    detail::promise_shref<T...>& promise_as_shref(promise<T...> &pro) {
      return static_cast<detail::promise_shref<T...>&>(pro);
    }
  }
}
#endif
