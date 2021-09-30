#ifndef _fdec460f_0bc2_4c14_b88c_c85d76235cd3
#define _fdec460f_0bc2_4c14_b88c_c85d76235cd3

#include <upcxx/backend.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/rpc.hpp>
#include <upcxx/team.hpp>
#include <upcxx/utility.hpp>

#include <type_traits>

/* NOTE: Reductions have full completions support, unlike the other
 * collectives, but this hasn't been verified as correct with a test.
 */

namespace upcxx {
  namespace detail {
    /* The function objects for `op_fast_add` etc are actually all instantiations
     * of `op_wrap<OpFn>` where `OpFn` is the type that actually implements the
     * `operator()` logic. We wrap types this way to make it easy to pattern match
     * our function objects in template specializations. We also carry a template
     * argument `fast_demanded` to indicate if this operator should statically
     * assert it's being used in a *possibly* offloadable way.
     */
    template<typename OpFn, bool fast_demanded>
    struct op_wrap: OpFn {
      // inherit `operator()` from `OpFn`
    };
  }
  
  /* `detail::opfn_[add|...]` is the function object which actually implements
   * `operator()` and is used as the value for `OpFn` in `op_wrap`.
   */
  #define UPCXXI_INFIX_OP(tok, tok_bool, name, integral_only1) \
    namespace detail {\
      struct opfn_##name {\
        static constexpr bool integral_only = integral_only1;\
        template<typename T, typename Tb>\
        T operator()(T a, Tb &&b) const {\
          a tok##= std::forward<Tb>(b);\
          return static_cast<T&&>(a);\
        }\
        template<typename Tb>\
        bool operator()(bool a, Tb &&b) const {\
          a tok_bool##= std::forward<Tb>(b);\
          return static_cast<bool&&>(a);\
        }\
      };\
    }\
    namespace experimental {\
      constexpr detail::op_wrap<detail::opfn_##name, /*fast_demanded=*/false> op_##name = {};\
    }\
    constexpr detail::op_wrap<detail::opfn_##name, /*fast_demanded=*/true> op_fast_##name = {};
  
  UPCXXI_INFIX_OP(+, |, add, false)
  UPCXXI_INFIX_OP(*, &, mul, false)
  UPCXXI_INFIX_OP(&, &, bit_and, true)
  UPCXXI_INFIX_OP(|, |, bit_or, true)
  UPCXXI_INFIX_OP(^, ^, bit_xor, true)
  #undef UPCXXI_INFIX_OP
  
  namespace detail {
    template<bool min_not_max>
    struct opfn_min_not_max {
      static constexpr bool integral_only = false;
      
      template<typename T>
      T&& operator()(T &&a, T &&b) const {
        return (min_not_max ? a < b : a > b) ? static_cast<T&&>(a) : static_cast<T&&>(b);
      }
      template<typename T>
      T const& operator()(T const &a, T const &b) const {
        return (min_not_max ? a < b : a > b) ? a : b;
      }
    };
  }
  namespace experimental {
    constexpr detail::op_wrap<detail::opfn_min_not_max<true>, /*fast_demanded=*/false> op_min = {};
    constexpr detail::op_wrap<detail::opfn_min_not_max<false>, /*fast_demanded=*/false> op_max = {};
  }
  constexpr detail::op_wrap<detail::opfn_min_not_max<true>, /*fast_demanded=*/true> op_fast_min = {};
  constexpr detail::op_wrap<detail::opfn_min_not_max<false>, /*fast_demanded=*/true> op_fast_max = {};
  
  namespace detail {
    /* `detail::reduce_op_has_fast<Op,T>` matches Op and T and reports to bools:
     * whether the combo is offloadable in theory (possibly) and in practice (actually).
     */ 
    // default for unknown Op,T is both false
    template<typename Op, typename T>
    struct reduce_op_has_fast {
      static constexpr bool possibly = false;
      static constexpr bool actually = false;
    };
    // match where `Op = op_wrap<OpFn,...>`
    template<typename OpFn, bool fast_demanded, typename T>
    struct reduce_op_has_fast<op_wrap<OpFn, fast_demanded>, T> {
      static constexpr bool possibly = std::is_integral<T>::value || (
          !OpFn::integral_only && std::is_floating_point<T>::value
        );
      
      static constexpr bool actually = possibly && (8*sizeof(T)==32 || 8*sizeof(T)==64);
    };
    
    ////////////////////////////////////////////////////////////////////////////
    // The following is support for reduce_op_best_id<Op,T>:
    
    /* We use const global variables to hold GEX constants. The ".cpp" file
     * defines these since it pulls in GEX headers, we only declare them.
     */
    struct reduce_op_slow_op_id {
      static const std::uintptr_t op_id; // = GEX_OP_USER
    };
    struct reduce_op_slow_ty_id {
      static const std::uintptr_t ty_id; // = GEX_DT_USER
    };
    
    template<typename Op, typename T>
    struct reduce_op_slow_id:
      reduce_op_slow_op_id,
      reduce_op_slow_ty_id {
      
      // The vectorized user provided function for GEX_OP_USER
      static void op_vecfn(const void *arg1, void *arg2_and_out, std::size_t n, const void *data) {
        T const *a = static_cast<T const*>(arg1);
        T *b_out = static_cast<T*>(arg2_and_out);
        Op const *op = static_cast<Op const*>(data);
        
        while(n--) {
          *b_out = (*op)(*a, *b_out);
          a++;
          b_out++;
        }
      }
    };
    
    // Parameterize global const names for GEX_OP_*** using the `opfn_***` types.
    template<typename OpFn>
    struct reduce_op_fast_op_id {
      static const std::uintptr_t op_id; // the GEX OP name for this operation (ex: op_id = GEX_OP_ADD when OpFn = opfn_add)
      static constexpr std::nullptr_t op_vecfn = nullptr; // No vectorized user function necessary
    };
    // reduce_op_fast_op_id implicitly unwraps `op_wrap<OpFn,...>`
    template<typename OpFn, bool fast_demanded>
    struct reduce_op_fast_op_id<op_wrap<OpFn,fast_demanded>>:
      reduce_op_fast_op_id<OpFn> {
    };
    
    // Parameterize global const names for GEX_DT_*** using the measurements of the type.
    template<int bits, bool is_signed>
    struct reduce_op_fast_ty_id_integral {
      static const std::uintptr_t ty_id; // the GEX DT name for this type
    };
    template<int bits>
    struct reduce_op_fast_ty_id_floating {
      static const std::uintptr_t ty_id; // the GEX DT name for this type
    };
    
    template<typename T,
             bool is_integral = std::is_integral<T>::value,
             bool is_floating = std::is_floating_point<T>::value>
    struct reduce_op_fast_ty_id: reduce_op_slow_ty_id {};
    
    template<typename T>
    struct reduce_op_fast_ty_id<T,
        /*is_integral=*/true,
        /*is_floating=*/false
      >: reduce_op_fast_ty_id_integral</*bits=*/8*sizeof(T), std::is_signed<T>::value> {
    };
    template<typename T>
    struct reduce_op_fast_ty_id<T,
        /*is_integral=*/false,
        /*is_floating=*/true
      >: reduce_op_fast_ty_id_floating</*bits=*/8*sizeof(T)> {
    };
    
    ////////////////////////////////////////////////////////////////////////////
    // reduce_op_best_id<Op,T>:
    
    /* `reduce_op_best_id<Op,T>` produces the following members:
     *   static const uintptr_t op_id; // the GEX_OP_***
     *   static const void(*op_vecfn)(...); // function pointer for GEX_OP_USER
     *   static const uintptr_t ty_id; // the GEX_DT_***
     */
    template<typename Op, typename T,
             bool possibly_fast = reduce_op_has_fast<Op,T>::possibly,
             bool actually_fast = reduce_op_has_fast<Op,T>::actually>
    struct reduce_op_best_id;
    
    template<typename Op, typename T>
    struct reduce_op_best_id<Op, T, /*possibly=*/true, /*actually=*/true>:
      reduce_op_fast_op_id<Op>,
      reduce_op_fast_ty_id<T> {
    };
    template<typename Op, typename T>
    struct reduce_op_best_id<Op, T, /*possibly=*/true, /*actually=*/false>:
      reduce_op_slow_id<Op,T> {
    };
    template<typename OpFn, bool fast_demanded, typename T>
    struct reduce_op_best_id<op_wrap<OpFn,fast_demanded>, T, /*possibly=*/false, /*actually=*/false>:
      reduce_op_slow_id<op_wrap<OpFn,fast_demanded>, T> {
      static_assert(!fast_demanded, "upcxx::op_fast_*** cannot be applied to this datatype.");
    };
    template<typename Op, typename T>
    struct reduce_op_best_id<Op, T, /*possibly=*/false, /*actually=*/false>:
      reduce_op_slow_id<Op,T> {
    };
    
    ////////////////////////////////////////////////////////////////////////////
    
    template<typename T>
    struct reduce_scalar_event_values {
      template<typename Event>
      using tuple_t = typename std::conditional<
          std::is_same<Event, operation_cx_event>::value,
            std::tuple<T>,
            void
        >::type;
    };
    struct reduce_vector_event_values {
      template<typename Event>
      using tuple_t = std::tuple<>;
    };
    
    ////////////////////////////////////////////////////////////////////////////
    // Calls out to gex_Coll_ReduceTo{One,All}NB
    
    void reduce_one_or_all_trivial_erased(
        const team &tm, intrank_t root_or_all/*-1 = all*/,
        const void *src, void *dst,
        std::size_t elt_sz, std::size_t elt_n,
        std::uintptr_t ty_id,
        std::uintptr_t op_id,
        void(*op_vecfn)(const void*, void*, std::size_t, const void*),
        void *op_data,
        backend::gasnet::handle_cb *cb
      );
    
    template<typename T1, typename BinaryOp,
             typename Cxs = detail::operation_cx_as_future_t,
             typename T = typename std::decay<T1>::type>
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_one_or_all_trivial(
        T1 &&value, BinaryOp op, intrank_t root_or_all/*-1 = all*/,
        const team &tm = upcxx::world(),
        Cxs &&cxs = detail::operation_cx_as_future_t{{}}
      ) {
      
      using CxsDecayed = typename std::decay<Cxs>::type;
      static_assert(
        upcxx::is_trivially_serializable<T>::value,
        "`upcxx::reduce_[all|one]<T>` only permitted for TriviallySerializable T. "
        "Consider using `upcxx::experimental::reduce_[all|one]_nontrivial<T>` instead "
        "(experimental feature, use at own risk)."
      );
      
      UPCXX_ASSERT_ALWAYS(
        (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
        "Not requesting operation completion is surely an error."
      );
      UPCXX_ASSERT_ALWAYS(
        (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
         !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
        "Reductions do not support source or remote completion."
      );
      
      using cxs_state_here_t = detail::completions_state<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        CxsDecayed>;
      
      struct my_cb final: backend::gasnet::handle_cb {
        T in_out;
        BinaryOp op;
        cxs_state_here_t cxs_st;
        
        my_cb(T1 &&in, BinaryOp op, cxs_state_here_t cxs_st):
          in_out(std::forward<T1>(in)),
          op(std::move(op)),
          cxs_st(std::move(cxs_st)) {
        }
        
        void execute_and_delete(backend::gasnet::handle_cb_successor) override {
          cxs_st.template operator()<operation_cx_event>(std::move(in_out));
          delete this;
        }
      };
      
      my_cb *cb = new my_cb(
          std::forward<T1>(value),
          std::move(op),
          cxs_state_here_t{std::forward<Cxs>(cxs)}
        );
      
      auto returner = detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::reduce_scalar_event_values<T>,
          CxsDecayed
        >(cb->cxs_st);
      
      reduce_one_or_all_trivial_erased(
          tm, root_or_all,
          &cb->in_out, &cb->in_out, sizeof(T), 1,
          detail::reduce_op_best_id<BinaryOp,T>::ty_id,
          detail::reduce_op_best_id<BinaryOp,T>::op_id,
          detail::reduce_op_best_id<BinaryOp,T>::op_vecfn,
          (void*)&cb->op,
          cb
        );
      
      return returner();
    }
    
    template<typename T, typename BinaryOp,
             typename Cxs = detail::operation_cx_as_future_t>
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_vector_event_values,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_one_or_all_trivial(
        T const *src, T *dst, std::size_t n,
        BinaryOp op, intrank_t root_or_all/*-1 = all*/,
        const team &tm = upcxx::world(),
        Cxs &&cxs = detail::operation_cx_as_future_t{{}}
      ) {
      using CxsDecayed = typename std::decay<Cxs>::type;
      static_assert(
        upcxx::is_trivially_serializable<T>::value,
        "`upcxx::reduce_[all|one]<T>` only permitted for TriviallySerializable T. "
        "Consider using `upcxx::experimental::reduce_[all|one]_nontrivial<T>` instead "
        "(experimental feature, use at own risk)."
      );

      UPCXX_ASSERT_ALWAYS(
        (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
        "Not requesting operation completion is surely an error."
      );
      UPCXX_ASSERT_ALWAYS(
        (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
         !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
        "Reductions do not support source or remote completion."
      );
      
      using cxs_state_here_t = detail::completions_state<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_vector_event_values,
        CxsDecayed>;
      
      struct my_cb final: backend::gasnet::handle_cb {
        BinaryOp op;
        cxs_state_here_t cxs_st;
        
        my_cb(BinaryOp op, cxs_state_here_t cxs_st):
          op(std::move(op)),
          cxs_st(std::move(cxs_st)) {
        }
        
        void execute_and_delete(backend::gasnet::handle_cb_successor) override {
          cxs_st.template operator()<operation_cx_event>();
          delete this;
        }
      };
      
      my_cb *cb = new my_cb(std::move(op), cxs_state_here_t{std::forward<Cxs>(cxs)});
      
      auto returner = detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::reduce_vector_event_values,
          CxsDecayed
        >(cb->cxs_st);
      
      reduce_one_or_all_trivial_erased(
          tm, root_or_all,
          src, dst, sizeof(T), n,
          detail::reduce_op_best_id<BinaryOp,T>::ty_id,
          detail::reduce_op_best_id<BinaryOp,T>::op_id,
          detail::reduce_op_best_id<BinaryOp,T>::op_vecfn,
          (void*)&cb->op,
          cb
        );
      
      return returner();
    }
    
    
    ////////////////////////////////////////////////////////////////////////////
    // Non-GEX reduction implementation. Used for nontrivial types T.
    
    template<typename T, typename Op, bool one_not_all, typename Cxs>
    struct reduce_state {
      intrank_t root; // root rank for reduction tree
      int incoming; // number of ranks sending us a contribution, counts down to zero.
      T accum; // accumulates contributions from local user and other ranks.
      
      using cxs_state_t = detail::completions_state<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        Cxs>;
      
      detail::raw_storage<cxs_state_t> cxs_st_raw;
      
      ~reduce_state() {
        cxs_st_raw.destruct();
      }
      
      template<typename T1>
      static void contribute(const team&, intrank_t root, detail::digest id,
                             Op const &op, T1 &&value, cxs_state_t *cxs_st);
    };
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // upcxx::reduce_one
  
  template<typename T1, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t,
           typename T = typename std::decay<T1>::type>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::reduce_scalar_event_values<T>,
      typename std::decay<Cxs>::type
    >::return_t
  reduce_one(
      T1 value, BinaryOp op, intrank_t root,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_STATIC_ASSERT_VALUE_SIZE(T, reduce_one); // issue 392: prevent large types by-value

    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_one(value)", entry_barrier::internal);
    UPCXX_ASSERT(root >= 0 && root < tm.rank_n(),
      "reduce_one(..., root, team) requires root in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << root);
    
    return detail::reduce_one_or_all_trivial<T1,BinaryOp,Cxs,T>(
        std::move(value), std::move(op), root, tm, std::forward<Cxs>(cxs)
      );
  }
  
  template<typename T, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_vector_event_values,
        typename std::decay<Cxs>::type
      >::return_t
   reduce_one(
      T const *src, T *dst, std::size_t n,
      BinaryOp op, intrank_t root,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_one(bulk)", entry_barrier::internal);
    UPCXX_ASSERT(root >= 0 && root < tm.rank_n(),
      "reduce_one(..., root, team) requires root in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << root);
    
    return detail::reduce_one_or_all_trivial<T,BinaryOp,Cxs>(
        src, dst, n, std::move(op), root, tm, std::forward<Cxs>(cxs)
      );
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // upcxx::experimental::reduce_one_nontrivial
  
  namespace detail {
    template<typename T1, typename BinaryOp,
             typename Cxs,
             typename T = typename std::decay<T1>::type>
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_one_nontrivial(
        T1 &&value, BinaryOp op, intrank_t root,
        const team &tm,
        Cxs &&cxs,
        std::true_type trivial_yes
      ) {
      return reduce_one(std::forward<T1>(value), std::move(op), root, tm, std::forward<Cxs>(cxs));
    }
    
    template<typename T1, typename BinaryOp,
             typename Cxs,
             typename T = typename std::decay<T1>::type>
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_one_nontrivial(
        T1 &&value, BinaryOp op, intrank_t root,
        const team &tm,
        Cxs &&cxs,
        std::false_type trivial_no
      ) {
      using CxsDecayed = typename std::decay<Cxs>::type;
      UPCXX_ASSERT_ALWAYS(
        (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
        "Not requesting operation completion is surely an error."
      );
      UPCXX_ASSERT_ALWAYS(
        (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
         !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
        "Reductions do not support source or remote completion."
      );
      
      using reduce_state = detail::reduce_state<T,BinaryOp,/*one_not_all=*/true,CxsDecayed>;
      
      typename reduce_state::cxs_state_t cxs_st{std::forward<Cxs>(cxs)};
      
      auto returner = detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::reduce_scalar_event_values<T>,
          CxsDecayed
        >(cxs_st);
        
      detail::digest id = const_cast<team*>(&tm)->next_collective_id(detail::internal_only());
      
      reduce_state::contribute(
          tm, root, id, std::move(op), std::forward<T1>(value), &cxs_st
        );
      
      return returner();
    }
  }

  namespace experimental {
  template<typename T1, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t,
           typename T = typename std::decay<T1>::type>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::reduce_scalar_event_values<T>,
      typename std::decay<Cxs>::type
    >::return_t
  reduce_one_nontrivial(
      T1 &&value, BinaryOp op, intrank_t root,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_one_nontrivial()", entry_barrier::internal);
    UPCXX_ASSERT(root >= 0 && root < tm.rank_n(),
      "reduce_one_nontrivial(..., root, team) requires root in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << root);
      
    return detail::reduce_one_nontrivial(
        std::forward<T1>(value), std::move(op), root, tm, std::forward<Cxs>(cxs),
        std::integral_constant<bool, upcxx::is_trivially_serializable<T>::value>()
      );
  }
  } // namespace experimental
  
  //////////////////////////////////////////////////////////////////////////////
  // upcxx::reduce_all
  
  template<typename T1, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t,
           typename T = typename std::decay<T1>::type>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::reduce_scalar_event_values<T>,
      typename std::decay<Cxs>::type
    >::return_t
  reduce_all(
      T1 value, BinaryOp op,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_STATIC_ASSERT_VALUE_SIZE(T, reduce_all); // issue 392: prevent large types by-value
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_all(value)", entry_barrier::internal);
    return detail::reduce_one_or_all_trivial<T1,BinaryOp,Cxs,T>(
        std::move(value), std::move(op), /*all=*/-1, tm, std::forward<Cxs>(cxs)
      );
  }
  
  template<typename T, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::reduce_vector_event_values,
      typename std::decay<Cxs>::type
    >::return_t
  reduce_all(
      T const *src, T *dst, std::size_t n,
      BinaryOp op,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_all(bulk)", entry_barrier::internal);
    return detail::reduce_one_or_all_trivial<T,BinaryOp,Cxs>(
        src, dst, n, std::move(op), /*all=*/-1, tm, std::forward<Cxs>(cxs)
      );
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // upcxx::experimental::reduce_all_nontrivial
  
  namespace detail {
    template<typename T1, typename BinaryOp,
             typename Cxs = detail::operation_cx_as_future_t,
             typename T = typename std::decay<T1>::type>
    UPCXXI_NODISCARD
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_all_nontrivial(
        T1 &&value, BinaryOp op,
        const team &tm,
        Cxs &&cxs,
        std::true_type trivial_yes
      ) {
      UPCXXI_ASSERT_INIT();
      return reduce_all(std::forward<T1>(value), std::move(op), tm, std::forward<Cxs>(cxs));
    }
    
    template<typename T1, typename BinaryOp,
             typename Cxs = detail::operation_cx_as_future_t,
             typename T = typename std::decay<T1>::type>
    UPCXXI_NODISCARD
    typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
    reduce_all_nontrivial(
        T1 &&value, BinaryOp op,
        const team &tm,
        Cxs &&cxs,
        std::false_type trivial_no
      ) {
      using CxsDecayed = typename std::decay<Cxs>::type;
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT_ALWAYS(
        (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
        "Not requesting operation completion is surely an error."
      );
      UPCXX_ASSERT_ALWAYS(
        (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
         !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
        "Reductions do not support source or remote completion."
      );

      using reduce_state = detail::reduce_state<T,BinaryOp,/*one_not_all=*/false,CxsDecayed>;
      
      typename reduce_state::cxs_state_t cxs_st{std::forward<Cxs>(cxs)};
      
      auto returner = detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::reduce_scalar_event_values<T>,
          CxsDecayed
        >(cxs_st);
      
      detail::digest id = const_cast<team*>(&tm)->next_collective_id(detail::internal_only());
      intrank_t root = id.w0 % tm.rank_n();
      
      reduce_state::contribute(
          tm, root, id, std::move(op), std::forward<T1>(value), &cxs_st
        );
      
      return returner();
    }
  }

  namespace experimental {
  template<typename T1, typename BinaryOp,
           typename Cxs = detail::operation_cx_as_future_t,
           typename T = typename std::decay<T1>::type>
  UPCXXI_NODISCARD
  typename detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::reduce_scalar_event_values<T>,
        typename std::decay<Cxs>::type
      >::return_t
  reduce_all_nontrivial(
      T1 &&value, BinaryOp op,
      const team &tm = upcxx::world(),
      Cxs &&cxs = detail::operation_cx_as_future_t{{}}
    ) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::reduce_all_nontrivial()", entry_barrier::internal);
    return detail::reduce_all_nontrivial(
        std::forward<T1>(value), std::move(op), tm, std::forward<Cxs>(cxs),
        std::integral_constant<bool, upcxx::is_trivially_serializable<T>::value>()
      );
  }
  } // namespace experimental
  
  //////////////////////////////////////////////////////////////////////////////
  // reduce_state::contribute
  
  namespace detail {
    template<typename T, typename Op, bool one_not_all, typename Cxs>
    template<typename T1>
    void reduce_state<T,Op,one_not_all,Cxs>::contribute(
        const team &tm, intrank_t root, detail::digest id, Op const &op, T1 &&value,
        typename reduce_state::cxs_state_t *cxs_st
      ) {
      
      UPCXXI_ASSERT_MASTER();
      detail::persona_scope_redundant master_as_top(backend::master, detail::the_persona_tls);
      
      intrank_t rank_n = tm.rank_n();
      
      // Use rank indexing scheme where the root is zero.
      intrank_t rank_me = tm.rank_me() - root;
      rank_me += rank_me < 0 ? rank_n : 0;
      
      auto it_and_inserted = detail::registry.insert({id, nullptr});
      reduce_state *state;
      
      if(it_and_inserted.second) {
        // `id` didn't exist in registry so we're first to contribute.
        
        // Given the parent function which flips the least significant one-bit
        //   parent(r) = r & (r-1)
        // Count number of bitpatterns in [0,rank_n) which when asked for
        // their parent, would produce us (rank_me).
        int incoming = 0;
        while(true) {
          intrank_t child = rank_me | (intrank_t(1)<<incoming);
          if(child == rank_me || rank_n <= child)
            break;
          incoming += 1;
        }
        incoming += 1; // add one for this rank
        
        state = new reduce_state{root, incoming, std::forward<T1>(value), {}};
        it_and_inserted.first->second = state;
      }
      else {
        // Contributions already exist, just add ours to it.
        state = static_cast<reduce_state*>(it_and_inserted.first->second);
        state->accum = op(std::move(state->accum), std::forward<T1>(value));
      }
      
      if(cxs_st)
        ::new(&state->cxs_st_raw) cxs_state_t{std::move(*cxs_st)};
      
      if(0 == --state->incoming) {
        // We have all of our expected contributions.
        if(rank_me == 0) {
          // We are root, time to broadcast result.
          auto bound = detail::bind([=](T &&value) {
                auto it = detail::registry.find(id);
                reduce_state *me = static_cast<reduce_state*>(it->second);
                
                detail::registry.erase(it);
                me->cxs_st_raw.value().template operator()<operation_cx_event>(std::move(value));
                delete me;
              },
              std::move(state->accum)
            );
          
          if(!one_not_all)
            backend::bcast_am_master<progress_level::user>(tm, bound);
          
          state->cxs_st_raw.value().template operator()<operation_cx_event>(std::move(std::get<0>(bound.b_)));
          delete state;
          detail::registry.erase(id);
        }
        else {
          // Send our result to parent. Parent rank's index is the same as ours
          // except with the least significant one-bit set to zero.
          intrank_t parent = rank_me & (rank_me-1);
          // Translate back to team's indexing scheme
          parent += root;
          parent -= parent >= rank_n ? rank_n : 0;
          
          team_id tm_id = tm.id();
          
          backend::template send_am_master<progress_level::internal>(
            backend::team_rank_to_world(tm, parent),
            detail::bind(
              //[=](T &value, decltype(detail::globalize_fnptr(std::declval<Op>())) const &op) {
              [=](T &&value, typename detail::globalize_fnptr_return<Op>::type const &op)->void {
                reduce_state::contribute(tm_id.here(), root, id, op, static_cast<T&&>(value), nullptr);
              },
              state->accum, detail::globalize_fnptr(op)
            )
          );
          
          if(one_not_all) {
            // accum has been moved-out from, so this is a bogus (but valid) value
            state->cxs_st_raw.value().template operator()<operation_cx_event>(std::move(state->accum));
            delete state;
            detail::registry.erase(id);
          }
        }
      }
    }
  }
}
#endif
