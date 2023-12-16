#ifndef _eb5831b3_6325_4936_9ebb_321d97838dee
#define _eb5831b3_6325_4936_9ebb_321d97838dee

#include <upcxx/backend_fwd.hpp>
#include <upcxx/future.hpp>
#include <upcxx/persona.hpp>
#include <upcxx/team.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstdint>
#include <memory>
#include <tuple>

////////////////////////////////////////////////////////////////////////////////

namespace upcxx {
  inline bool initialized() {
    return backend::init_count != 0;
  }
  
  inline persona& master_persona() {
    UPCXXI_ASSERT_INIT();
    return backend::master;
  }
 
  namespace detail {
    void progress_user();
    void progress_internal();
  }
  inline void progress(progress_level level) {
    UPCXXI_ASSERT_INIT();

    if (level == progress_level::user)
      detail::progress_user();
    else {
      UPCXX_ASSERT(level == progress_level::internal);
      detail::progress_internal();
    }
  }

  inline bool in_progress() {
    UPCXXI_ASSERT_INIT();
    return detail::the_persona_tls.get_progressing() >= 0;
  }
  
  inline bool progress_required() {
    UPCXXI_ASSERT_INIT();
    return detail::the_persona_tls.progress_required();
  }
  inline bool progress_required(persona_scope &bottom) {
    UPCXXI_ASSERT_INIT();
    return detail::the_persona_tls.progress_required(bottom);
  }
  
  inline void discharge() {
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT(!in_progress(), "discharge() is forbidden within UPC++ restricted context (upcxx::in_progress())");
    UPCXXI_PROGRESS_UNTIL(!upcxx::progress_required(),   detail::progress_internal);
  }
  inline void discharge(persona_scope &ps) {
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT(!in_progress(), "discharge() is forbidden within UPC++ restricted context (upcxx::in_progress())");
    UPCXXI_PROGRESS_UNTIL(!upcxx::progress_required(ps), detail::progress_internal);
  }
}

////////////////////////////////////////////////////////////////////////////////
// upcxx::backend implementation: non-backend specific

namespace upcxx {
namespace backend {
  // inclusive lower and exclusive upper bounds for local_team ranks
  extern intrank_t pshm_peer_lb_;
  #if UPCXXI_ALL_RANKS_DEFINITELY_LOCAL
    constexpr intrank_t pshm_peer_lb = 0;
  #elif __HIP_DEVICE_COMPILE__
    // issue 554: branch below chokes ROCm/5 device compilation, where code paths using
    // this variable should be unreachable anyhow. Insert an obvious poison
    // value that will trigger assertions if this invariant is ever violated.
    constexpr intrank_t pshm_peer_lb = 0x2152DEAD;
  #else
    static constexpr intrank_t const& pshm_peer_lb = pshm_peer_lb_;
  #endif
  extern intrank_t pshm_peer_ub, pshm_peer_n;
  
  // Given index in local_team:
  //   local_minus_remote: Encodes virtual address translation which is added
  //     to the raw encoding to get local virtual address.
  //   vbase: Local virtual address mapping to beginning of peer's segment
  //   size: Size of peer's segment in bytes.
  extern std::unique_ptr<std::uintptr_t[/*local_team.size()*/]> pshm_local_minus_remote;
  extern std::unique_ptr<std::uintptr_t[/*local_team.size()*/]> pshm_vbase;
  extern std::unique_ptr<std::uintptr_t[/*local_team.size()*/]> pshm_size;
  
  //////////////////////////////////////////////////////////////////////////////
  // fulfill_during_<level=internal>
  
  template<typename ...T>
  void fulfill_during_(
      std::integral_constant<progress_level, progress_level::internal>,
      detail::future_header_promise<T...> *pro, // takes ref
      std::intptr_t anon,
      persona &active_per
    ) {

    /* We don't utilize the lpc characteristic of promises here since we can't
    be sure the promise wasn't also registered against this persona but during
    user level progress, a separate queue, which would clobber the intrusive
    linkage. We address this by always allocating a new lpc to bounce into
    promise fulfillment for `fulfill_during<progress_level::internal>`, which
    allows `fulfill_during<progress_level::user>` to know it can use
    `persona_tls::fulfill_during_user_of_active` unconditionally. Since
    user-level promises are numerous and internal-level are rare (non-existent?
    the implementation tends to use callbacks rather than futures) this is the
    performance smart choice.*/
    
    detail::the_persona_tls.during(
      active_per, progress_level::internal,
      [=]() {
        detail::promise_fulfill_anonymous(pro, anon);
        pro->dropref();
      },
      /*known_active=*/std::true_type()
    );
  }
  
  template<typename ...T>
  void fulfill_during_(
      std::integral_constant<progress_level, progress_level::internal>,
      detail::future_header_promise<T...> *pro, // takes ref
      std::tuple<T...> &&vals,
      persona &active_per
    ) {

    pro->base_header_result.construct_results(std::move(vals));
    
    fulfill_during_(
      std::integral_constant<progress_level, progress_level::internal>(),
      pro, /*anon*/1,
      active_per
    );
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // fulfill_during_<level=user>
  
  template<typename ...T>
  void fulfill_during_(
      std::integral_constant<progress_level, progress_level::user>,
      detail::future_header_promise<T...> *pro, // takes ref
      std::tuple<T...> &&vals,
      persona &active_per
    ) {

    pro->base_header_result.construct_results(std::move(vals));
    
    detail::the_persona_tls.fulfill_during_user_of_active(active_per, /*move ref*/pro, /*deps*/1);
  }
  
  template<typename ...T>
  void fulfill_during_(
      std::integral_constant<progress_level, progress_level::user>,
      detail::future_header_promise<T...> *pro, // takes ref
      std::intptr_t anon,
      persona &active_per
    ) {
    detail::the_persona_tls.fulfill_during_user_of_active(active_per, /*move ref*/pro, anon);
  }
  
  //////////////////////////////////////////////////////////////////////////////
  
  template<progress_level level, typename ...T>
  void fulfill_during(
      detail::future_header_promise<T...> *pro, // takes ref
      std::tuple<T...> vals,
      persona &active_per
    ) {
    fulfill_during_(
        std::integral_constant<progress_level,level>(),
        /*move ref*/pro, std::move(vals), active_per
      );
  }
  
  template<progress_level level, typename ...T>
  void fulfill_during(
      detail::future_header_promise<T...> *pro, // takes ref
      std::intptr_t anon,
      persona &active_per
    ) {
    fulfill_during_(
        std::integral_constant<progress_level,level>(),
        /*move ref*/pro, anon, active_per
      );
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // fulfill_now

  template<typename ...T>
  void fulfill_now(
      detail::future_header_promise<T...> *pro, // takes ref
      std::intptr_t anon
    ) {
    detail::promise_fulfill_anonymous(pro, anon);
    pro->dropref();
  }

  template<typename ...T>
  void fulfill_now(
      detail::future_header_promise<T...> *pro, // takes ref
      std::tuple<T...> &&vals
    ) {
    pro->base_header_result.construct_results(std::move(vals));
    fulfill_now(pro, /*anon*/1);
  }

  //////////////////////////////////////////////////////////////////////////////
  
  inline bool rank_is_local(intrank_t r) {
    UPCXX_ASSERT(r >= 0 && r < backend::rank_n, "Invalid argument to rank_is_local: " << r);
    UPCXXI_ASSERT_VALID_DEFINITELY_LOCAL();

    return /*constexpr*/all_ranks_definitely_local || 
           std::uintptr_t(r) - std::uintptr_t(pshm_peer_lb) < std::uintptr_t(pshm_peer_n);
    // Is equivalent to...
    // return pshm_peer_lb <= r && r < pshm_peer_ub;
  }
  
  inline void* localize_memory_nonnull(intrank_t rank, std::uintptr_t raw) {
    UPCXX_ASSERT(
      pshm_peer_lb <= rank && rank < pshm_peer_ub,
      "Rank "<<rank<<" is not local with current rank ("<<upcxx::rank_me()<<")."
    );
    UPCXXI_ASSERT_VALID_DEFINITELY_LOCAL();

    intrank_t peer = rank - pshm_peer_lb;
    std::uintptr_t u = raw + pshm_local_minus_remote[peer];

    UPCXX_ASSERT(
      u - pshm_vbase[peer] < pshm_size[peer], // unsigned arithmetic handles both sides of the interval test
      "Memory address (raw="<<raw<<", local="<<reinterpret_cast<void*>(u)<<") is not within shared segment of rank "<<rank<<"."
    );

    return reinterpret_cast<void*>(u);
  }
  
  inline void* localize_memory(intrank_t rank, std::uintptr_t raw) {
    if(raw == reinterpret_cast<std::uintptr_t>(nullptr))
      return nullptr;
    
    return localize_memory_nonnull(rank, raw);
  }
  
  inline std::uintptr_t globalize_memory_nonnull(intrank_t rank, void const *addr) {
    UPCXX_ASSERT(
      pshm_peer_lb <= rank && rank < pshm_peer_ub,
      "Rank "<<rank<<" is not local with current rank ("<<upcxx::rank_me()<<")."
    );
    UPCXXI_ASSERT_VALID_DEFINITELY_LOCAL();
    
    std::uintptr_t u = reinterpret_cast<std::uintptr_t>(addr);
    intrank_t peer = rank - pshm_peer_lb;
    std::uintptr_t raw = u - pshm_local_minus_remote[peer];
    
    UPCXX_ASSERT(
      u - pshm_vbase[peer] < pshm_size[peer], // unsigned arithmetic handles both sides of the interval test
      "Memory address (raw="<<raw<<", local="<<addr<<") is not within shared segment of rank "<<rank<<"."
    );
    
    return raw;
  }

  void validate_global_ptr(bool allow_null, intrank_t rank, void *raw_ptr, std::uint32_t heap_idx,
                           memory_kind dynamic_kind, memory_kind Kind, size_t T_align, const char *T_name, 
                           const char *short_context, const char *context);
}}
  
////////////////////////////////////////////////////////////////////////////////
// Include backend-specific headers:

#if UPCXXI_BACKEND_GASNET_SEQ || UPCXXI_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/runtime.hpp>
#else
  #error "Invalid UPCXXI_BACKEND."
#endif

#endif // #ifdef guard
