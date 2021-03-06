#ifndef _1e7a65b7_b8d1_4def_98a3_76038c9431cf
#define _1e7a65b7_b8d1_4def_98a3_76038c9431cf

#include <upcxx/future/core.hpp>
#include <upcxx/utility.hpp>
#if UPCXX_BACKEND
  #include <upcxx/backend_fwd.hpp>
#endif

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  /* FutureImpl concept:
   * 
   * struct FutureImpl {
   *   FutureImpl(FutureImpl const&);
   *   FutureImpl& operator=(FutureImpl const&);
   * 
   *   FutureImpl(FutureImpl&&);
   *   FutureImpl& operator=(FutureImpl&&);
   * 
   *   bool ready() const;
   * 
   *   // Return result tuple where each component is either a value or
   *   // reference (const& or &&) depending on whether the reference
   *   // would be valid for as long as this future lives.
   *   // Therefor we guarantee that references will be valid as long
   *   // as this future lives. If any T are already & or && then they
   *   // are returned unaltered.
   *   tuple<(T, T&&, or T const&)...> result_refs_or_vals() [const&, &&];
   *   
   *   // Returns a future_header (with refcount included for consumer)
   *   // for this future. Leaves us in an undefined state (only safe
   *   // operations are (copy/move)-(construction/assignment), and
   *   // destruction).
   *   detail::future_header* steal_header() &&;
   *   
   *   // One of the header operations classes for working with the
   *   // header produced by steal_header().
   *   typedef detail::future_header_ops_? header_ops;
   * };
   */
  
  //////////////////////////////////////////////////////////////////////
  
  namespace detail {
    #ifdef UPCXX_BACKEND
      struct future_wait_upcxx_progress_user {
        void operator()() const {
          UPCXX_ASSERT(
            -1 == detail::progressing(),
            "You have attempted to wait() on a non-ready future within upcxx progress, this is prohibited because it will never complete."
          );
          upcxx::progress();
        }
      };
    #endif
    
    template<typename T>
    struct is_future1: std::false_type {};
    template<typename Kind, typename ...T>
    struct is_future1<future1<Kind,T...>>: std::true_type {};
  }
  
  //////////////////////////////////////////////////////////////////////
  // future1: The actual type users get (aliased as future<>).
  
  namespace detail {
  template<typename Kind, typename ...T>
  struct future1 {
    typedef Kind kind_type;
    typedef std::tuple<T...> results_type;
    typedef typename Kind::template with_types<T...> impl_type; // impl_type is a FutureImpl.
    
    using clref_results_refs_or_vals_type = decltype(std::declval<impl_type const&>().result_refs_or_vals());
    using rref_results_refs_or_vals_type = decltype(std::declval<impl_type&&>().result_refs_or_vals());

    // factored return type computation for future::result*()
    template<int i, typename tuple_type>
    using result_return_select_type = typename std::conditional<
                    (i<0 && sizeof...(T) > 1), // auto mode && multiple elements
                    tuple_type,  // entire tuple (possibly ref-ified)
                    typename detail::tuple_element_or_void<(i<0 ? 0 : i), tuple_type>::type // element or void
                  >::type;

    impl_type impl_;
    
  public:
    future1() = default;
    ~future1() = default;
    
    template<typename impl_type1,
             // Prune from overload resolution if `impl_type1` is not a known
             // future_impl_*** type.
             typename = typename detail::future_impl_traits<impl_type1>::kind_type>
    future1(impl_type1 &&impl): impl_(static_cast<impl_type1&&>(impl)) {}
    
    future1(future1 const&) = default;
    template<typename Kind1>
    future1(future1<Kind1,T...> const &that): impl_(that.impl_) {}
    
    future1(future1&&) = default;
    template<typename Kind1>
    future1(future1<Kind1,T...> &&that):
      impl_(static_cast<future1<Kind1,T...>&&>(that).impl_) {
    }
    
    future1& operator=(future1 const&) = default;
    template<typename Kind1>
    future1& operator=(future1<Kind1,T...> const &that) {
      this->impl_ = that.impl_;
      return *this;
    }
    
    future1& operator=(future1&&) = default;
    template<typename Kind1>
    future1& operator=(future1<Kind1,T...> &&that) {
      this->impl_ = static_cast<future1<Kind1,T...>&&>(that).impl_;
      return *this;
    }
    
    bool ready() const {
      return impl_.ready();
    }
  
  private:
    template<typename Tup>
    static Tup&& get_at_(Tup &&tup, std::integral_constant<int,-1>) {
      return static_cast<Tup&&>(tup);
    }
    template<typename Tup>
    static void get_at_(Tup &&tup, std::integral_constant<int,sizeof...(T)>) {
      return;
    }
    template<typename Tup, int i>
    static typename detail::tuple_element_or_void<i, typename std::decay<Tup>::type>::type
    get_at_(Tup &&tup, std::integral_constant<int,i>) {
      return std::template get<i>(static_cast<Tup&&>(tup));
    }

    static std::string nonready_msg(const char *this_func, const char *wait_func, const char *result_desc) {
      return 
       std::string("Called future::") + this_func + "() on a non-ready future, which has undefined behavior.\n"
       + "Please call future::" + wait_func + "() instead, which blocks invoking progress until readiness,"
       " and then returns the " + result_desc + ".\n"
       "Alternatively, use future::then(...) to schedule an asynchronous callback to execute after the future is readied.";
    }
  
  public:
    template<int i=-1>
    result_return_select_type<i, results_type>
    result() const& {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result()", "future::result_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT( ready(), nonready_msg("result","wait","result") );
      return get_at_(
          impl_.result_refs_or_vals(),
          std::integral_constant<int, (
              i >= (int)sizeof...(T) ? (int)sizeof...(T) :
              i >= 0 ? i :
              sizeof...(T) > 1 ? -1 :
              0
            )>()
        );
    }

    template<int i=-1>
    result_return_select_type<i, results_type>
    result() && {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result()", "future::result_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT( ready(), nonready_msg("result","wait","result") );
      return get_at_(
          static_cast<impl_type&&>(impl_).result_refs_or_vals(),
          std::integral_constant<int, (
              i >= (int)sizeof...(T) ? (int)sizeof...(T) :
              i >= 0 ? i :
              sizeof...(T) > 1 ? -1 :
              0
            )>()
        );
    }
    
    template<int i=-1>
    result_return_select_type<i, clref_results_refs_or_vals_type>
    result_reference() const& {
      UPCXX_ASSERT( ready(), nonready_msg("result_reference","wait_reference","result reference") );
      return get_at_(
          impl_.result_refs_or_vals(),
          std::integral_constant<int, (
              i >= (int)sizeof...(T) ? (int)sizeof...(T) :
              i >= 0 ? i :
              sizeof...(T) > 1 ? -1 :
              0
            )>()
        );
    }

    template<int i=-1>
    result_return_select_type<i, rref_results_refs_or_vals_type>
    result_reference() && {
      UPCXX_ASSERT( ready(), nonready_msg("result_reference","wait_reference","result reference") );
      return get_at_(
          static_cast<impl_type&&>(impl_).result_refs_or_vals(),
          std::integral_constant<int, (
              i >= (int)sizeof...(T) ? (int)sizeof...(T) :
              i >= 0 ? i :
              sizeof...(T) > 1 ? -1 :
              0
            )>()
        );
    }
    
    results_type result_tuple() const& {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result_tuple()", 
                                            "future::result_reference()", // result_reference_tuple is unspecified
                                            results_type);
      UPCXX_ASSERT( ready(), nonready_msg("result_tuple","wait_tuple","result tuple") );
      return impl_.result_refs_or_vals();
    }
    results_type result_tuple() && {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result_tuple()", 
                                            "future::result_reference()", // result_reference_tuple is unspecified
                                            results_type);
      UPCXX_ASSERT( ready(), nonready_msg("result_tuple","wait_tuple","result tuple") );
      return static_cast<impl_type&&>(impl_).result_refs_or_vals();
    }
    
    clref_results_refs_or_vals_type result_reference_tuple() const& {
      UPCXX_ASSERT( ready(), nonready_msg("result_reference_tuple","wait_tuple","result tuple") );
      return impl_.result_refs_or_vals();
    }
    rref_results_refs_or_vals_type result_reference_tuple() && {
      UPCXX_ASSERT( ready(), nonready_msg("result_reference_tuple","wait_tuple","result tuple") );
      return static_cast<impl_type&&>(impl_).result_refs_or_vals();
    }

    // Return type is forced to the default kind (like future<T...>) if "this"
    // is also the default kind so as not to surprise users with spooky types.
    // The optimizations possible for knowing a future came from a (non-lazy)
    // then as opposed to the default kind is minimal, see the difference between
    // future_header_ops_dependent vs future_header_ops_general wrt delete1 &
    // dropref.
    template<typename Fn>
    typename detail::future_change_kind<
        typename detail::future_then<
          future1<Kind, T...>,
          typename std::decay<Fn>::type
        >::return_type,
        /*NewKind=*/detail::future_kind_default,
        /*condition=*/std::is_same<Kind, detail::future_kind_default>::value
      >::type
    then(Fn &&fn) const& {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type>()(
        *this, static_cast<Fn&&>(fn)
      );
    }

    // Return type is forced just like preceding "then" member.
    template<typename Fn>
    typename detail::future_change_kind<
        typename detail::future_then<
          future1<Kind, T...>,
          typename std::decay<Fn>::type
        >::return_type,
        /*NewKind=*/detail::future_kind_default,
        /*condition=*/std::is_same<Kind, detail::future_kind_default>::value
      >::type
    then(Fn &&fn) && {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type>()(
        static_cast<future1&&>(*this), static_cast<Fn&&>(fn)
      );
    }

    // Hidden member: produce a then'd future lazily so that it can be used with
    // later then's. Comes with restrictions, see "src/future/impl_then_lazy.hpp".
    template<typename Fn>
    typename detail::future_then<
        future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true
      >::return_type
    then_lazy(Fn &&fn) const& {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true>()(
        *this, static_cast<Fn&&>(fn)
      );
    }

    // Hidden member just like preceding "then_lazy"
    template<typename Fn>
    typename detail::future_then<
        future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true
      >::return_type
    then_lazy(Fn &&fn) && {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true>()(
        static_cast<future1&&>(*this), static_cast<Fn&&>(fn)
      );
    }

    #ifdef UPCXX_BACKEND
    template<int i=-1, typename Fn=detail::future_wait_upcxx_progress_user>
    auto wait(Fn &&progress = detail::future_wait_upcxx_progress_user{}) const&
    #else
    template<int i=-1, typename Fn>
    auto wait(Fn &&progress) const&
    #endif
      -> result_return_select_type<i, results_type> {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait()");
      
      while(!impl_.ready())
        progress();
      
      return this->template result<i>();
    }
    
    #ifdef UPCXX_BACKEND
    template<int i=-1, typename Fn=detail::future_wait_upcxx_progress_user>
    auto wait(Fn &&progress = detail::future_wait_upcxx_progress_user{}) &&
    #else
    template<int i=-1, typename Fn>
    auto wait(Fn &&progress) &&
    #endif
      -> result_return_select_type<i, results_type> {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait()");
      
      while(!impl_.ready())
        progress();
      
      return static_cast<future1&&>(*this).template result<i>();
    }
    
    #ifdef UPCXX_BACKEND
    template<typename Fn=detail::future_wait_upcxx_progress_user>
    results_type wait_tuple(Fn &&progress = detail::future_wait_upcxx_progress_user{}) const&
    #else
    template<typename Fn>
    results_type wait_tuple(Fn &&progress) const&
    #endif
    {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_tuple()", "future::wait_reference()", results_type);
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait_tuple()");

      while(!impl_.ready())
        progress();
      
      return this->result_tuple();
    }

    #ifdef UPCXX_BACKEND
    template<typename Fn=detail::future_wait_upcxx_progress_user>
    results_type wait_tuple(Fn &&progress = detail::future_wait_upcxx_progress_user{}) &&
    #else
    template<typename Fn>
    results_type wait_tuple(Fn &&progress) &&
    #endif
    {
      UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_tuple()", "future::wait_reference()", results_type);
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait_tuple()");

      while(!impl_.ready())
        progress();
      
      return static_cast<future1&&>(*this).result_tuple();
    }
    
    #ifdef UPCXX_BACKEND
    template<int i=-1, typename Fn=detail::future_wait_upcxx_progress_user>
    auto wait_reference(Fn &&progress = detail::future_wait_upcxx_progress_user{}) const&
    #else
    template<int i=-1, typename Fn>
    auto wait_reference(Fn &&progress) const&
    #endif
      -> result_return_select_type<i, clref_results_refs_or_vals_type> {
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait_reference()");
      
      while(!impl_.ready())
        progress();
      
      return this->template result_reference<i>();
    }

    #ifdef UPCXX_BACKEND
    template<int i=-1, typename Fn=detail::future_wait_upcxx_progress_user>
    auto wait_reference(Fn &&progress = detail::future_wait_upcxx_progress_user{}) &&
    #else
    template<int i=-1, typename Fn>
    auto wait_reference(Fn &&progress) &&
    #endif
      -> result_return_select_type<i, rref_results_refs_or_vals_type> {
      UPCXX_ASSERT_INIT_NAMED("future<...>::wait_reference()");
      
      while(!impl_.ready())
        progress();
      
      return static_cast<future1&&>(*this).template result_reference<i>();
    }
  };
  } // namespace detail
}
#endif
