#ifndef _4fd2caba_e406_4f0e_ab34_0e0224ec36a5
#define _4fd2caba_e406_4f0e_ab34_0e0224ec36a5

#include <upcxx/backend.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/global_ptr.hpp>

#include <upcxx/backend/gasnet/runtime.hpp>

#include <gasnet_fwd.h>

#include <cstdint>
#include <vector>
#include <string>
#include <type_traits>

namespace upcxx {
  // All supported atomic operations.
  enum class atomic_op : gex_OP_t { 

       // accessors
       load             = GEX_OP_GET, 
       store            = GEX_OP_SET, 
       compare_exchange = GEX_OP_FCAS,

       // arithmetic
       add              = GEX_OP_ADD,   fetch_add        = GEX_OP_FADD,
       sub              = GEX_OP_SUB,   fetch_sub        = GEX_OP_FSUB,
       inc              = GEX_OP_INC,   fetch_inc        = GEX_OP_FINC,
       dec              = GEX_OP_DEC,   fetch_dec        = GEX_OP_FDEC,
       mul              = GEX_OP_MULT,  fetch_mul        = GEX_OP_FMULT,
       min              = GEX_OP_MIN,   fetch_min        = GEX_OP_FMIN,
       max              = GEX_OP_MAX,   fetch_max        = GEX_OP_FMAX,

       // bitwise operations
       bit_and          = GEX_OP_AND,   fetch_bit_and    = GEX_OP_FAND,
       bit_or           = GEX_OP_OR,    fetch_bit_or     = GEX_OP_FOR,
       bit_xor          = GEX_OP_XOR,   fetch_bit_xor    = GEX_OP_FXOR,
  };
  
  namespace detail {

    extern const char *atomic_op_str(upcxx::atomic_op op);
    extern std::string opset_to_string(gex_OP_t opset);

    inline int memory_order_flags(std::memory_order order) {
      switch (order) {
        case std::memory_order_acquire: return GEX_FLAG_AD_ACQ;
        case std::memory_order_release: return GEX_FLAG_AD_REL;
        case std::memory_order_acq_rel: return (GEX_FLAG_AD_ACQ | GEX_FLAG_AD_REL);
        case std::memory_order_relaxed: return 0;
        case std::memory_order_seq_cst:
          UPCXX_ASSERT(0, "Unsupported memory order: std::memory_order_seq_cst");
          break;
        case std::memory_order_consume:
          UPCXX_ASSERT(0, "Unsupported memory order: std::memory_order_consume");
          break;
        default:
          UPCXX_ASSERT(0, "Unrecognized memory order: " << (int)order);      
      }
      return 0; // unreachable
    }

    template<std::size_t size, int bit_flavor /*0=unsigned, 1=signed, 2=floating*/>
    struct atomic_proxy_help { using type = char; static constexpr gex_DT_t dt = 0; /* for error checking */ };
    template<>
    struct atomic_proxy_help<4,0> { using type = std::uint32_t; static constexpr gex_DT_t dt = GEX_DT_U32; };
    template<>
    struct atomic_proxy_help<4,1> { using type = std::int32_t;  static constexpr gex_DT_t dt = GEX_DT_I32; };
    template<>
    struct atomic_proxy_help<4,2> { using type = float;         static constexpr gex_DT_t dt = GEX_DT_FLT; };
    template<>
    struct atomic_proxy_help<8,0> { using type = std::uint64_t; static constexpr gex_DT_t dt = GEX_DT_U64; };
    template<>
    struct atomic_proxy_help<8,1> { using type = std::int64_t;  static constexpr gex_DT_t dt = GEX_DT_I64; };
    template<>
    struct atomic_proxy_help<8,2> { using type = double;        static constexpr gex_DT_t dt = GEX_DT_DBL; };

    template<typename T>
    inline constexpr int bit_flavor() { // 0=unsigned, 1=signed, 2=floating
      return std::is_floating_point<T>::value ? 2 : (std::is_unsigned<T>::value ? 0 : 1);
    }

    // 6 combinations explicitly instantiated in atomic.cpp TU:
    // {size=4,8} X {bit_flavor=0,1,2}
    template<std::size_t size, int bit_flavor>
    struct atomic_domain_untyped {
      using proxy_type = typename atomic_proxy_help<size, bit_flavor>::type;
      static constexpr gex_DT_t dt = atomic_proxy_help<size, bit_flavor>::dt;
      // backend gasnet function dispatcher
      template<upcxx::atomic_op opcode>
      struct inject {
          static gex_Event_t doit( std::uintptr_t ad, 
            void *result_ptr, intrank_t jobrank, void *raw_ptr, 
            proxy_type val1, proxy_type val2, gex_Flags_t flags);
      };

      // Our encoding:
      // atomic_gex_ops == ad_gex_handle == 0: 
      //   an inactive (default-constructed, moved-from, or destroyed) object.
      // atomic_gex_ops == 0, ad_gex_handle != 0 : 
      //   prohibited.
      // atomic_gex_ops != 0, ad_gex_handle != 0 : 
      //   a live domain constructed by gasnet.

      // The or'd values for the atomic operations.
      gex_OP_t atomic_gex_ops = 0;
      // The opaque gasnet atomic domain handle.
      std::uintptr_t ad_gex_handle = 0;

      const team *parent_tm_ = nullptr;
      
      // default constructor doesn't do anything besides initializing both:
      //   atomic_gex_ops = 0, ad_gex_handle = 0
      atomic_domain_untyped() {}

      // The constructor takes a vector of operations. Currently, flags is currently unsupported.
      atomic_domain_untyped(std::vector<atomic_op> const &ops, const team &tm);

      // Issue 490
      void real_destructor();
      ~atomic_domain_untyped() { real_destructor(); }

      void destroy(entry_barrier eb);
    };

    // declare specializations of atomic_domain_untyped::inject
    #define UPCXXI_DECL_INJECT(sz,bit) \
    template<> \
    template<upcxx::atomic_op opcode> \
    struct atomic_domain_untyped<sz,bit>::inject { \
        static gex_Event_t doit( std::uintptr_t ad, \
          void *result_ptr, intrank_t jobrank, void *raw_ptr,  \
          proxy_type val1, proxy_type val2, gex_Flags_t flags); \
    }
    UPCXXI_DECL_INJECT(4,0);
    UPCXXI_DECL_INJECT(4,1);
    UPCXXI_DECL_INJECT(4,2);
    UPCXXI_DECL_INJECT(8,0);
    UPCXXI_DECL_INJECT(8,1);
    UPCXXI_DECL_INJECT(8,2);
    #undef UPCXXI_DECL_INJECT

  } // namespace detail 
  
  // Atomic domain for any supported type.
  template<typename T>
  class atomic_domain final :
    private detail::atomic_domain_untyped<sizeof(T), detail::bit_flavor<T>()> {
 
    private:
      using ad_untyped = typename detail::atomic_domain_untyped<sizeof(T), detail::bit_flavor<T>()>;
      using proxy_type = typename detail::atomic_proxy_help<sizeof(T), detail::bit_flavor<T>()>::type;

      // for checking type is 32 or 64-bit non-const integral/floating type
      static constexpr bool is_atomic =
        (std::is_integral<T>::value || std::is_floating_point<T>::value) && 
	!std::is_const<T>::value &&
        (sizeof(T) == 4 || sizeof(T) == 8);
      
      static_assert(is_atomic,
          "Atomic domains only supported on non-const 32 and 64-bit integral or floating-point types");

      // event values for operations with a completion value
      struct novalue_aop_event_values {
        template<typename Event>
        using tuple_t = std::tuple<>;
      };
      // event values for operations with no completion value
      struct value_aop_event_values {
        template<typename Event>
        using tuple_t = typename std::conditional<
            std::is_same<Event, operation_cx_event>::value, std::tuple<T>, std::tuple<> >::type;
      };

      // The class that handles the gasnet event. This is for no-value ops.
      // Must be declared final for the 'delete this' call.
      template<typename CxStateHere>
      struct novalue_op_cb final: backend::gasnet::handle_cb {
        CxStateHere state_here;

        novalue_op_cb(CxStateHere state_here) : state_here{std::move(state_here)} {}

        // The callback executed upon event completion.
        void execute_and_delete(backend::gasnet::handle_cb_successor) override {
          this->state_here.template operator()<operation_cx_event>();
          delete this;
        }
      };

      // The class that handles the gasnet event. For value ops.
      template<typename CxStateHere>
      struct value_op_cb final: backend::gasnet::handle_cb {
        CxStateHere state_here;
        T result;

        value_op_cb(CxStateHere state_here) : state_here{std::move(state_here)} {}

        void execute_and_delete(backend::gasnet::handle_cb_successor) override {
          this->state_here.template operator()<operation_cx_event>(std::move(result));
          delete this;
        }
      };

      // convenience declarations
      template<typename Cxs>
      using VALUE_RTYPE = typename detail::completions_returner<detail::event_is_here,
          value_aop_event_values, typename std::decay<Cxs>::type>::return_t;
      template<typename Cxs>
      using NOVALUE_RTYPE = typename detail::completions_returner<detail::event_is_here,
          novalue_aop_event_values, typename std::decay<Cxs>::type>::return_t;
      using FUTURE_CX = detail::operation_cx_as_future_t;

      // generic fetching atomic operation -- completion value
      template<atomic_op aop, typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      VALUE_RTYPE<Cxs> vop(global_ptr<T> gptr, std::memory_order order,
                           T val1 = 0, T val2 = 0, Cxs &&cxs = Cxs{{}}) const {
        using CxsDecayed = typename std::decay<Cxs>::type;
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(this->is_active(), "Atomic domain is not constructed");
        UPCXX_ASSERT((detail::completions_has_event<CxsDecayed, operation_cx_event>::value));
        UPCXXI_GPTR_CHK(gptr);
        UPCXX_ASSERT(gptr != nullptr, "Global pointer for atomic operation is null");
        UPCXX_ASSERT(
          this->parent_tm_->from_world(gptr.UPCXXI_INTERNAL_ONLY(rank_),-1) >= 0,
          "Global pointer must reference a member of the team used to construct atomic_domain"
        );
        UPCXX_ASSERT(static_cast<gex_OP_t>(aop) & this->atomic_gex_ops,
              "Atomic operation '" << detail::atomic_op_str(aop) << "'"
              " not in domain's operation set '" << 
              detail::opset_to_string(this->atomic_gex_ops) << "'\n");
        UPCXX_ASSERT_ALWAYS(
          (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
          "Not requesting operation completion for '" <<
          detail::atomic_op_str(aop) << "' is surely an error."
        );
        UPCXX_ASSERT_ALWAYS(
          (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
           !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
          "Atomic operation '" << detail::atomic_op_str(aop) << "'"
          " does not support source or remote completion."
        );

        
        // we only have local completion, not remote
        using cxs_here_t = detail::completions_state<detail::event_is_here,
            value_aop_event_values, CxsDecayed>;
        
        // Create the callback object
        auto *cb = new value_op_cb<cxs_here_t>{cxs_here_t{std::forward<Cxs>(cxs)}};
        
        // execute the backend gasnet function
        gex_Event_t h = ad_untyped::template inject<aop>::doit( this->ad_gex_handle,
          &cb->result, gptr.UPCXXI_INTERNAL_ONLY(rank_),
          gptr.UPCXXI_INTERNAL_ONLY(raw_ptr_), 
          static_cast<proxy_type>(val1), static_cast<proxy_type>(val2), 
          detail::memory_order_flags(order) | GEX_FLAG_RANK_IS_JOBRANK
        );
        
        // construct returner before post-injection actions potentially
        // destroy cb->state_here
        // we construct the returner after injection for symmetry with
        // the non-value case; in the value case, the
        // empty-future optimization does not actually apply, but
        // there isn't a downside to delaying construction of the
        // returner until after injection
        auto returner = detail::completions_returner<detail::event_is_here,
            value_aop_event_values, CxsDecayed>{cb->state_here,
                                                h == GEX_EVENT_INVALID ?
                                                detail::cx_event_done::operation :
                                                detail::cx_event_done::none};

        if (h != GEX_EVENT_INVALID) { // asynchronous AMO in-flight
          cb->handle = reinterpret_cast<uintptr_t>(h);
          backend::gasnet::register_cb(cb);
          backend::gasnet::after_gasnet();
        } else { // gasnet completed AMO synchronously
          UPCXX_ASSERT(cb->handle == 0);
          backend::gasnet::get_handle_cb_queue().execute_outside(cb);
        }
        
        return returner();
      }

      // generic non-fetching/fetch-into atomic operation -- no
      // completion value
      template<atomic_op aop, typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> nvop(global_ptr<T> gptr, std::memory_order order,
                            T val1 = 0, T val2 = 0, T *dst = nullptr,
                            Cxs &&cxs = Cxs{{}}) const {
        using CxsDecayed = typename std::decay<Cxs>::type;
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(this->is_active(), "Atomic domain is not constructed");
        UPCXX_ASSERT((detail::completions_has_event<CxsDecayed, operation_cx_event>::value));
        UPCXXI_GPTR_CHK(gptr);
        UPCXX_ASSERT(gptr != nullptr, "Global pointer for atomic operation is null");
        UPCXX_ASSERT(
          this->parent_tm_->from_world(gptr.UPCXXI_INTERNAL_ONLY(rank_),-1) >= 0, 
          "Global pointer must reference a member of the team used to construct atomic_domain"
        );
        UPCXX_ASSERT(static_cast<gex_OP_t>(aop) & this->atomic_gex_ops,
              "Atomic operation '" << detail::atomic_op_str(aop) << "'"
              " not in domain's operation set '" << 
              detail::opset_to_string(this->atomic_gex_ops) << "'\n");
        UPCXX_ASSERT_ALWAYS(
          (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
          "Not requesting operation completion for '" <<
          detail::atomic_op_str(aop) << "' is surely an error."
        );
        UPCXX_ASSERT_ALWAYS(
          (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
           !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
          "Atomic operation '" << detail::atomic_op_str(aop) << "'"
          " does not support source or remote completion."
        );
        
        // we only have local completion, not remote
        using cxs_here_t = detail::completions_state<detail::event_is_here,
            novalue_aop_event_values, CxsDecayed>;
        
        // Create the callback object on stack..
        novalue_op_cb<cxs_here_t> cb(cxs_here_t(std::forward<Cxs>(cxs)));
        
        // execute the backend gasnet function
        gex_Event_t h = ad_untyped::template inject<aop>::doit( this->ad_gex_handle,
          dst, gptr.UPCXXI_INTERNAL_ONLY(rank_),
          gptr.UPCXXI_INTERNAL_ONLY(raw_ptr_), 
          static_cast<proxy_type>(val1), static_cast<proxy_type>(val2), 
          detail::memory_order_flags(order) | GEX_FLAG_RANK_IS_JOBRANK
        );

        auto returner = detail::completions_returner<detail::event_is_here,
            novalue_aop_event_values, CxsDecayed>{cb.state_here,
                                                  h == GEX_EVENT_INVALID ?
                                                  detail::cx_event_done::operation :
                                                  detail::cx_event_done::none};

        if (h != GEX_EVENT_INVALID) { // asynchronous AMO in-flight
          cb.handle = reinterpret_cast<uintptr_t>(h);
          // move callback to heap since it lives asynchronously
          backend::gasnet::register_cb(new decltype(cb)(std::move(cb)));
          backend::gasnet::after_gasnet();
        } else { // gasnet completed AMO synchronously
          UPCXX_ASSERT(cb.handle == 0);
          // do callback's execute_and_delete, minus the delete
          cb.state_here.template operator()<operation_cx_event>();
        }
        
        return returner();
      }

    public:
      // default constructor 
      atomic_domain() {}

      atomic_domain(atomic_domain &&that) : atomic_domain() {
        *this = std::move(that);
      }

      atomic_domain &operator=(atomic_domain &&that) {
        if (&that == this) return *this; // see issue 547
        UPCXXI_ASSERT_MASTER();
        // only allow assignment moves onto "dead" object
        UPCXX_ASSERT(!this->is_active(),
                     "Move assignment is only allowed on an inactive atomic_domain");
        this->ad_gex_handle = that.ad_gex_handle;
        this->atomic_gex_ops = that.atomic_gex_ops;
        this->parent_tm_ = that.parent_tm_;
        // revert `that` to non-constructed state
        that.atomic_gex_ops = 0;
        that.ad_gex_handle = 0;
        that.parent_tm_ = nullptr;
        return *this;
      }
      
      // The constructor takes a vector of operations. Currently, flags is currently unsupported.
      atomic_domain(std::vector<atomic_op> const &ops, const team &tm = upcxx::world()) :
        detail::atomic_domain_untyped<sizeof(T), 
           detail::bit_flavor<T>()>((UPCXXI_ASSERT_INIT(),UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user),ops), tm) {}
      
      void destroy(entry_barrier eb = entry_barrier::user) {
        UPCXXI_ASSERT_INIT();
        UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
        UPCXX_ASSERT(is_active());
        detail::atomic_domain_untyped<sizeof(T), detail::bit_flavor<T>()>::destroy(eb);
      }

      ~atomic_domain() {}
      
      UPCXXI_ATTRIB_PURE
      bool is_active() const {
        UPCXX_ASSERT(!!this->atomic_gex_ops == !!this->ad_gex_handle);
        return this->atomic_gex_ops != 0;
      }

      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> store(global_ptr<T> gptr, T val, std::memory_order order,
                               Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return nvop<atomic_op::store>(gptr, order, val, (T)0, nullptr, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      VALUE_RTYPE<Cxs> load(global_ptr<const T> gptr, std::memory_order order, Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return vop<atomic_op::load>(const_pointer_cast<T>(gptr), order, (T)0, (T)0, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> load(global_ptr<const T> gptr, T *dst,
                              std::memory_order order,
                              Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(dst != nullptr, "Destination for atomic operation is null");
        return nvop<atomic_op::load>(const_pointer_cast<T>(gptr), order, (T)0, (T)0,
                  dst, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> inc(global_ptr<T> gptr, std::memory_order order, Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return nvop<atomic_op::inc>(gptr, order, (T)0, (T)0, nullptr, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> dec(global_ptr<T> gptr, std::memory_order order, Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return nvop<atomic_op::dec>(gptr, order, (T)0, (T)0, nullptr, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      VALUE_RTYPE<Cxs> fetch_inc(global_ptr<T> gptr, std::memory_order order, Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return vop<atomic_op::fetch_inc>(gptr, order, (T)0, (T)0, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> fetch_inc(global_ptr<T> gptr, T *dst,
                                   std::memory_order order,
                                   Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(dst != nullptr, "Destination for atomic operation is null");
        return nvop<atomic_op::fetch_inc>(gptr, order, (T)0, (T)0, dst,
                  std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      VALUE_RTYPE<Cxs> fetch_dec(global_ptr<T> gptr, std::memory_order order, Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return vop<atomic_op::fetch_dec>(gptr, order, (T)0, (T)0, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> fetch_dec(global_ptr<T> gptr, T *dst,
                                   std::memory_order order,
                                   Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(dst != nullptr, "Destination for atomic operation is null");
        return nvop<atomic_op::fetch_dec>(gptr, order, (T)0, (T)0, dst,
                  std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      VALUE_RTYPE<Cxs> compare_exchange(global_ptr<T> gptr, T val1, T val2, std::memory_order order,
                                        Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        return vop<atomic_op::compare_exchange>(gptr, order, val1, val2, std::forward<Cxs>(cxs));
      }
      template<typename Cxs = FUTURE_CX>
      UPCXXI_NODISCARD
      NOVALUE_RTYPE<Cxs> compare_exchange(global_ptr<T> gptr, T val1, T val2,
                                          T *dst, std::memory_order order,
                                          Cxs &&cxs = Cxs{{}}) const {
        UPCXXI_ASSERT_INIT();
        UPCXX_ASSERT(dst != nullptr, "Destination for atomic operation is null");
        return nvop<atomic_op::compare_exchange>(gptr, order, val1, val2, dst,
                  std::forward<Cxs>(cxs));
      }
      
      #define UPCXXI_AD_METHODS(name, constraint)\
        template<typename Cxs = FUTURE_CX>\
        UPCXXI_NODISCARD \
        constraint(VALUE_RTYPE<Cxs>) \
	fetch_##name(global_ptr<T> gptr, T val, std::memory_order order,\
                                      Cxs &&cxs = Cxs{{}}) const {\
          UPCXXI_ASSERT_INIT(); \
          return vop<atomic_op::fetch_##name>(gptr, order, val, (T)0, std::forward<Cxs>(cxs));\
        }\
        template<typename Cxs = FUTURE_CX>\
        UPCXXI_NODISCARD \
        constraint(NOVALUE_RTYPE<Cxs>) \
        fetch_##name(global_ptr<T> gptr, T val, T *dst, std::memory_order order, \
                                      Cxs &&cxs = Cxs{{}}) const {\
          UPCXXI_ASSERT_INIT(); \
          UPCXX_ASSERT(dst != nullptr, "Destination for atomic operation is null"); \
          return nvop<atomic_op::fetch_##name>(gptr, order, val, (T)0, dst, std::forward<Cxs>(cxs)); \
        }\
        template<typename Cxs = FUTURE_CX>\
        UPCXXI_NODISCARD \
        constraint(NOVALUE_RTYPE<Cxs>) \
	name(global_ptr<T> gptr, T val, std::memory_order order,\
                                Cxs &&cxs = Cxs{{}}) const {\
          UPCXXI_ASSERT_INIT(); \
          return nvop<atomic_op::name>(gptr, order, val, (T)0, nullptr, std::forward<Cxs>(cxs));\
        }
      // sfinae helpers to disable unsupported type/op combos
      #define UPCXXI_AD_INTONLY(R) typename std::enable_if<std::is_integral<T>::value,R>::type
      #define UPCXXI_AD_ANYTYPE(R) R
      UPCXXI_AD_METHODS(add,    UPCXXI_AD_ANYTYPE)
      UPCXXI_AD_METHODS(sub,    UPCXXI_AD_ANYTYPE)
      UPCXXI_AD_METHODS(mul,    UPCXXI_AD_ANYTYPE)
      UPCXXI_AD_METHODS(min,    UPCXXI_AD_ANYTYPE)
      UPCXXI_AD_METHODS(max,    UPCXXI_AD_ANYTYPE)
      UPCXXI_AD_METHODS(bit_and,UPCXXI_AD_INTONLY)
      UPCXXI_AD_METHODS(bit_or, UPCXXI_AD_INTONLY)
      UPCXXI_AD_METHODS(bit_xor,UPCXXI_AD_INTONLY)
      #undef UPCXXI_AD_METHODS
      #undef UPCXXI_AD_INTONLY
      #undef UPCXXI_AD_ANYTYPE
  };
} // namespace upcxx

#endif
