#include <upcxx/reduce.hpp>
#include <upcxx/backend/gasnet/runtime.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <gasnet_coll.h>

using namespace upcxx;
using namespace std;

namespace gasnet = upcxx::backend::gasnet;

namespace upcxx {
  namespace detail {
    const uintptr_t reduce_op_slow_ty_id::ty_id = GEX_DT_USER;
    
    template<> const uintptr_t reduce_op_fast_ty_id_integral<32, /*signed=*/true>::ty_id = GEX_DT_I32;
    template<> const uintptr_t reduce_op_fast_ty_id_integral<64, /*signed=*/true>::ty_id = GEX_DT_I64;
    template<> const uintptr_t reduce_op_fast_ty_id_integral<32, /*signed=*/false>::ty_id = GEX_DT_U32;
    template<> const uintptr_t reduce_op_fast_ty_id_integral<64, /*signed=*/false>::ty_id = GEX_DT_U64;
    template<> const uintptr_t reduce_op_fast_ty_id_floating<32>::ty_id = GEX_DT_FLT;
    template<> const uintptr_t reduce_op_fast_ty_id_floating<64>::ty_id = GEX_DT_DBL;
    
    const uintptr_t reduce_op_slow_op_id::op_id = GEX_OP_USER;

    template<> const uintptr_t reduce_op_fast_op_id<opfn_add>::op_id = GEX_OP_ADD;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_mul>::op_id = GEX_OP_MULT;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_min_not_max<true>>::op_id = GEX_OP_MIN;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_min_not_max<false>>::op_id = GEX_OP_MAX;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_bit_and>::op_id = GEX_OP_AND;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_bit_or>::op_id = GEX_OP_OR;
    template<> const uintptr_t reduce_op_fast_op_id<opfn_bit_xor>::op_id = GEX_OP_XOR;
  }
}

void upcxx::detail::reduce_one_or_all_trivial_erased(
    const team &tm, intrank_t root_or_all,
    const void *src, void *dst,
    std::size_t elt_sz, std::size_t elt_n,
    std::uintptr_t ty_id,
    std::uintptr_t op_id,
    void(*op_vecfn)(const void*, void*, std::size_t, const void*),
    void *op_data,
    backend::gasnet::handle_cb *cb
  ) {
  
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  
  #if 0
    if(&tm == &upcxx::world() && tm.rank_me()==0)
      upcxx::experimental::say()<<"gex_Coll_ReduceToXxxNB(dt="<<ty_id<<", op="<<op_id<<")";
  #endif
  
  gex_Event_t e = root_or_all >= 0
    ? gex_Coll_ReduceToOneNB(
        gasnet::handle_of(tm), root_or_all,
        dst, src,
        (gex_DT_t)ty_id, elt_sz, elt_n,
        (gex_OP_t)op_id, op_vecfn, op_data,
        /*flags*/0
      )
    : gex_Coll_ReduceToAllNB(
        gasnet::handle_of(tm),
        dst, src,
        (gex_DT_t)ty_id, elt_sz, elt_n,
        (gex_OP_t)op_id, op_vecfn, op_data,
        /*flags*/0
      );

  cb->handle = reinterpret_cast<uintptr_t>(e);
  
  { // We want completions to target master persona, so make it the "current" one
    // while we register. This is safe because we've already asserted that it must
    // be active.
    detail::persona_scope_redundant master_on_top(backend::master, detail::the_persona_tls);
    backend::gasnet::register_cb(cb);
  }
  
  backend::gasnet::after_gasnet();
}
