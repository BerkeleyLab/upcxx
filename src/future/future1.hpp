#ifndef _1e7a65b7_b8d1_4def_98a3_76038c9431cf
#define _1e7a65b7_b8d1_4def_98a3_76038c9431cf

#include <upcxx/future/core.hpp>
#include <upcxx/utility.hpp>
#if UPCXXI_BACKEND
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
    #ifdef UPCXXI_BACKEND
      struct future_wait_upcxx_progress_user {
        void operator()() const {
          UPCXX_ASSERT(
            -1 == detail::progressing(),
            "You have attempted to wait() on a non-ready future within upcxx progress, this is prohibited because it will never complete."
          );
          upcxx::progress();
        }
      };
      struct future_wait_upcxx_progress_internal {
        void operator()() const {
          upcxx::progress(progress_level::internal);
        }
      };
    #else
      struct future_wait_upcxx_progress_user {
        void operator()() const {}
      };
      struct future_wait_upcxx_progress_internal {
        void operator()() const {}
      };
    #endif

    #ifndef UPCXXI_BACKEND
      // Used to mark member function as internal only. Normally defined in
      // <upcxx/backend_fwd.hpp>.
      struct internal_only {
        explicit constexpr internal_only() {}
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
    future1(impl_type1 &&impl, detail::internal_only): impl_(static_cast<impl_type1&&>(impl)) {}
    
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
   
    UPCXXI_DEPRECATED("future::ready() function name is deprecated since 2023.3.5, use future::is_ready() instead")
    bool ready() const {
      return impl_.ready();
    }
  
    bool is_ready() const {
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
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result()", "future::result_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT( is_ready(), nonready_msg("result","wait","result") );
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
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result()", "future::result_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXX_ASSERT( is_ready(), nonready_msg("result","wait","result") );
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
      UPCXX_ASSERT( is_ready(), nonready_msg("result_reference","wait_reference","result reference") );
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
      UPCXX_ASSERT( is_ready(), nonready_msg("result_reference","wait_reference","result reference") );
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
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result_tuple()", 
                                            "future::result_reference()", // result_reference_tuple is unspecified
                                            results_type);
      UPCXX_ASSERT( is_ready(), nonready_msg("result_tuple","wait_tuple","result tuple") );
      return impl_.result_refs_or_vals();
    }
    results_type result_tuple() && {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::result_tuple()", 
                                            "future::result_reference()", // result_reference_tuple is unspecified
                                            results_type);
      UPCXX_ASSERT( is_ready(), nonready_msg("result_tuple","wait_tuple","result tuple") );
      return static_cast<impl_type&&>(impl_).result_refs_or_vals();
    }
    
    clref_results_refs_or_vals_type result_reference_tuple() const& {
      UPCXX_ASSERT( is_ready(), nonready_msg("result_reference_tuple","wait_tuple","result tuple") );
      return impl_.result_refs_or_vals();
    }
    rref_results_refs_or_vals_type result_reference_tuple() && {
      UPCXX_ASSERT( is_ready(), nonready_msg("result_reference_tuple","wait_tuple","result tuple") );
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
    then_lazy(Fn &&fn, detail::internal_only) const& {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true>()(
        *this, static_cast<Fn&&>(fn)
      );
    }

    // Hidden member just like preceding "then_lazy"
    template<typename Fn>
    typename detail::future_then<
        future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true
      >::return_type
    then_lazy(Fn &&fn, detail::internal_only) && {
      return detail::future_then<future1<Kind,T...>, typename std::decay<Fn>::type, /*make_lazy=*/true>()(
        static_cast<future1&&>(*this), static_cast<Fn&&>(fn)
      );
    }

    #if UPCXXI_PLATFORM_ARCH_X86_64
      // x86* has a pause instruction that we want to use inside the future::wait*() 
      // spin-loop to avoid a hazard stall when exiting after a long spin.
      // However the pause instruction itself incurs a measurable delay (up to ~140 cycles), 
      // so we don't want to pause until we are sure we are *actually* spin-waiting,
      // and not just reaping the readiness of a dependency that was already satisfied
      // with signalling deferred to the first progress call.
      // So we peel off the first progress call and readiness check before entering
      // the pausing progress loop.
      #define UPCXXI_PROGRESS_UNTIL(cond, progress) do { \
        if (!(cond)) { \
          progress(); \
          while (!(cond)) { \
            UPCXXI_SPINLOOP_HINT(); \
            progress(); \
          } \
        } \
      } while (0)
    #else
      #define UPCXXI_PROGRESS_UNTIL(cond, progress) do { \
        while (!(cond)) progress(); \
      } while (0)
    #endif

    template<int i=-1>
    auto wait() const&
      -> result_return_select_type<i, results_type> {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait()");
     
      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return this->result<i>();
    }
    
    template<int i=-1>
    auto wait() &&
      -> result_return_select_type<i, results_type> {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait()");
     
      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return std::move(*this).template result<i>();
    }

    template<int i=-1>
    auto wait_internal(detail::internal_only) const&
      -> result_return_select_type<i, results_type>
    {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_internal()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_internal()");

      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_internal{});
      
      return result<i>();
    }

    template<int i=-1>
    auto wait_internal(detail::internal_only) &&
      -> result_return_select_type<i, results_type>
    {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_internal()", "future::wait_reference()",
                                            result_return_select_type<i, results_type>);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_internal()");

      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_internal{});
      
      return std::move(*this).template result<i>();
    }
    
    inline results_type wait_tuple() const&
    {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_tuple()", "future::wait_reference()", results_type);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_tuple()");

      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return this->result_tuple();
    }

    inline results_type wait_tuple() &&
    {
      UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE("future::wait_tuple()", "future::wait_reference()", results_type);
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_tuple()");

      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return std::move(*this).template result_tuple();
    }
    
    template<int i=-1>
    auto wait_reference() const&
      -> result_return_select_type<i, clref_results_refs_or_vals_type> {
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_reference()");
      
      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return this->result_reference<i>();
    }

    template<int i=-1>
    auto wait_reference() &&
      -> result_return_select_type<i, rref_results_refs_or_vals_type> {
      UPCXXI_ASSERT_INIT_NAMED("future<...>::wait_reference()");
      
      UPCXXI_PROGRESS_UNTIL(impl_.ready(), detail::future_wait_upcxx_progress_user{});
      
      return std::move(*this).template result_reference<i>();
    }
  };
  } // namespace detail
}
#endif
