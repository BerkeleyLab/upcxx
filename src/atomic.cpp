#include <upcxx/diagnostic.hpp>
#include <upcxx/atomic.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

#if UPCXXI_BACKEND_GASNET
  #include <gasnet_ratomic.h>
#endif

#include <sstream>
#include <string>

namespace gasnet = upcxx::backend::gasnet;
namespace detail = upcxx::detail;

using upcxx::atomic_domain;
using upcxx::atomic_op;
using upcxx::intrank_t;
using upcxx::global_ptr;

using std::int32_t;
using std::uint32_t;
using std::int64_t;
using std::uint64_t;
using std::uintptr_t;

static_assert(
  sizeof(gex_AD_t) == sizeof(uintptr_t), 
  "Mismatch between underying gasnet handle size and UPC++ implementation"
);

#define FORALL_FPOPS(FN) \
        FN(load)    FN(store) \
        FN(compare_exchange) \
        FN(add)     FN(fetch_add) \
        FN(sub)     FN(fetch_sub) \
        FN(inc)     FN(fetch_inc) \
        FN(dec)     FN(fetch_dec) \
        FN(mul)     FN(fetch_mul) \
        FN(min)     FN(fetch_min) \
        FN(max)     FN(fetch_max) 

#define FORALL_INTOPS(FN) \
        FORALL_FPOPS(FN) \
        FN(bit_and) FN(fetch_bit_and) \
        FN(bit_or)  FN(fetch_bit_or)  \
        FN(bit_xor) FN(fetch_bit_xor) 

#define FORALL_OPS FORALL_INTOPS

namespace upcxx { namespace detail {

extern const char *atomic_op_str(upcxx::atomic_op op) {
  switch (op) {
    #define RET_STR(tok) case atomic_op::tok: return #tok;
    FORALL_OPS(RET_STR)
    #undef RET_STR
  }
  return "*unknown op*";
}

extern std::string opset_to_string(gex_OP_t opset) {
  std::stringstream ss;
  int n = 0;
  ss << '{';
  if (opset == 0)
    ss << "<empty!>";
  else {
    #define DO_OP(tok) \
      if (opset & (gex_OP_t)atomic_op::tok) { \
        if (n++) ss << ", "; \
        ss << #tok; \
        opset &= ~(gex_OP_t)atomic_op::tok; \
      }
    FORALL_OPS(DO_OP)
    #undef DO_OP
    if (opset) return "*invalid opset!*";
  } 
  ss << '}';
  return ss.str();
}

/* bit_flavor: 0=unsigned, 1=signed, 2=floating*/
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<4,0>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_U32(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr), 
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<4,1>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_I32(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr), 
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<4,2>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_FLT(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr),
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<8,0>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_U64(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr),
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<8,1>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_I64(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr),
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}
template<>
template<upcxx::atomic_op opcode>
GASNETT_HOT
gex_Event_t atomic_domain_untyped<8,2>::inject<opcode>::doit(
        std::uintptr_t ad, void *result_ptr, intrank_t jobrank, void *raw_ptr,
        proxy_type val1, proxy_type val2, gex_Flags_t flags) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSUME(flags & GEX_FLAG_RANK_IS_JOBRANK);
  return gex_AD_OpNB_DBL(reinterpret_cast<gex_AD_t>(ad), reinterpret_cast<proxy_type*>(result_ptr),
                         jobrank, raw_ptr, (gex_OP_t)opcode, val1, val2, flags);
}


} } // namespace upcxx::detail
  
namespace {

  // check a handful of enum mappings to ensure no insert/delete errors
  static_assert((int)upcxx::atomic_op::mul == GEX_OP_MULT, "Uh-oh");
  static_assert((int)upcxx::atomic_op::fetch_max == GEX_OP_FMAX, "Uh-oh");
  static_assert((int)upcxx::atomic_op::bit_and == GEX_OP_AND, "Uh-oh");
  static_assert((int)upcxx::atomic_op::fetch_bit_xor == GEX_OP_FXOR, "Uh-oh");
  static_assert((int)upcxx::atomic_op::compare_exchange == GEX_OP_CSWAP, "Uh-oh");
  
} // namespace

template<std::size_t size, int bit_flavor>
upcxx::detail::atomic_domain_untyped<size,bit_flavor>::atomic_domain_untyped(
  std::vector<atomic_op> const &ops, const team &tm) {
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXX_ASSERT(!ops.empty(),
               "atomic_domain cannot have an empty set of operations");

  gex_OP_t opmask = 0;
  for (auto next_op : ops) opmask |= static_cast<gex_OP_t>(next_op);
  atomic_gex_ops = opmask;
 
  if (bit_flavor == 2) {
    int prohibited_ops = opmask &
      (GEX_OP_AND|GEX_OP_FAND|GEX_OP_OR|GEX_OP_FOR|GEX_OP_XOR|GEX_OP_FXOR);
    UPCXX_ASSERT_ALWAYS(prohibited_ops == 0,
      "atomic_domain on floating-point types may not use " << opset_to_string(prohibited_ops) << std::endl);
  }

  parent_tm_ = &tm;
  
  #if GASNET_DEBUG
    // spec issue #160: gex_AD_Create currently performs some synchronization in DEBUG mode (only)
    // so perform a user barrier here to ensure no team members are blocked awaiting an RPC response
    backend::quiesce(tm, entry_barrier::user);
  #endif
  // Create the gasnet atomic domain for the world team.
  gex_AD_Create(reinterpret_cast<gex_AD_t*>(&ad_gex_handle),
                gasnet::handle_of(tm), 
                dt, opmask, /*flags=*/0);
  UPCXX_ASSERT(ad_gex_handle, "Error in gex_AD_Create");
}

template<std::size_t size, int bit_flavor>
void upcxx::detail::atomic_domain_untyped<size,bit_flavor>::destroy(entry_barrier eb) {
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  
  backend::quiesce(*parent_tm_, eb);

  UPCXX_ASSERT(ad_gex_handle, "attempted to destroy() and atomic_domain which was not constructed");
  
  gex_AD_Destroy(reinterpret_cast<gex_AD_t>(ad_gex_handle));
  atomic_gex_ops = 0;
  ad_gex_handle = 0;
  parent_tm_ = nullptr;
}

template<std::size_t size, int bit_flavor>
void upcxx::detail::atomic_domain_untyped<size,bit_flavor>::real_destructor() {
  if(backend::init_count > 0) { // we don't assert on leaks after finalization
    UPCXX_ASSERT_ALWAYS(
      atomic_gex_ops == 0,
      "ERROR: `upcxx::atomic_domain::destroy()` must be called collectively before destructor."
    );
  }
}

// instantiate template code for all supported combos
#define UPCXXI_AD_INJECT(op) \
  template struct UPCXXI_CONCAT(base,__LINE__)::inject<atomic_op::op>;

#define UPCXXI_AD_INST(size, flavor, inj) \
  template struct upcxx::detail::atomic_domain_untyped<size,flavor>;  \
  using UPCXXI_CONCAT(base,__LINE__) = typename upcxx::detail::atomic_domain_untyped<size,flavor>; \
  inj

UPCXXI_AD_INST(4,0,FORALL_INTOPS(UPCXXI_AD_INJECT))
UPCXXI_AD_INST(4,1,FORALL_INTOPS(UPCXXI_AD_INJECT))
UPCXXI_AD_INST(4,2,FORALL_FPOPS(UPCXXI_AD_INJECT))
UPCXXI_AD_INST(8,0,FORALL_INTOPS(UPCXXI_AD_INJECT))
UPCXXI_AD_INST(8,1,FORALL_INTOPS(UPCXXI_AD_INJECT))
UPCXXI_AD_INST(8,2,FORALL_FPOPS(UPCXXI_AD_INJECT))

