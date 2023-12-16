#ifndef _f93ccf7a_35a8_49c6_b7b2_55c3c1a9640c
#define _f93ccf7a_35a8_49c6_b7b2_55c3c1a9640c

#ifndef UPCXXI_BACKEND_GASNET_SEQ
  #define UPCXXI_BACKEND_GASNET_SEQ 0
#endif

#ifndef UPCXXI_BACKEND_GASNET_PAR
  #define UPCXXI_BACKEND_GASNET_PAR 0
#endif

#define UPCXXI_BACKEND_GASNET (UPCXXI_BACKEND_GASNET_SEQ | UPCXXI_BACKEND_GASNET_PAR)
#if UPCXXI_BACKEND_GASNET && !UPCXXI_BACKEND
#error Inconsistent UPCXXI_BACKEND definition!
#endif

/* This header declares some core user-facing API to break include
 * cycles with headers included by the real "backend.hpp". This header
 * does not pull in the implementation of what it exposes, so you can't
 * use anything that has a runtime analog unless guarded by a:
 *   #ifdef UPCXXI_BACKEND
 */

#include <upcxx/future/fwd.hpp>
#include <upcxx/diagnostic.hpp>
#include <upcxx/upcxx_config.hpp>
#include <upcxx/memory_kind.hpp>
#include <upcxx/ccs_fwd.hpp>
#include <gasnet_fwd.h>

#include <cstddef>
#include <cstdint>
#include <string>

////////////////////////////////////////////////////////////////////////////////
// Argument-checking assertions

// These define the maximum-size (in bytes) of an object that by-value API's will accept without an #error
#ifndef UPCXX_MAX_VALUE_SIZE
#define UPCXX_MAX_VALUE_SIZE 512 // user-tunable default
#endif
#ifndef UPCXX_MAX_RPC_ARG_SIZE
#define UPCXX_MAX_RPC_ARG_SIZE 512 // user-tunable default
#endif

#define UPCXXI_STATIC_ASSERT_VALUE_SIZE(T, fnname) \
  static_assert(sizeof(T) <= UPCXX_MAX_VALUE_SIZE, \
    "This program is attempting to pass an object with a large static type (over " UPCXXI_STRINGIFY(UPCXX_MAX_VALUE_SIZE) " bytes) " \
    "to the by-value overload of upcxx::" #fnname ". This is ill-advised because the by-value overload is " \
    "designed and tuned for small scalar values, and will impose significant data copy overheads " \
    "(and possibly program stack overflow) when used with larger types. Please use the bulk upcxx::" \
    #fnname " overload instead, which manipulates the data by pointer, avoiding costly by-value copies. " \
    "The threshold for this error can be adjusted (at your own peril!) via -DUPCXX_MAX_VALUE_SIZE=n" \
  )
namespace upcxx { namespace detail {
  template <typename T>
  struct type_respects_value_size_limit {
    static constexpr bool value = sizeof(T) <= UPCXX_MAX_VALUE_SIZE;
  };
  template <>
  struct type_respects_value_size_limit<void> {
    static constexpr bool value = true;
  };
}}
#define UPCXXI_STATIC_ASSERT_VALUE_RETURN_SIZE(fnname, alternate, ...) \
  static_assert(::upcxx::detail::type_respects_value_size_limit<__VA_ARGS__>::value, \
    "This program is calling upcxx::" fnname " to request the UPC++ library return an object by-value that has "\
    "a large static type (over " UPCXXI_STRINGIFY(UPCXX_MAX_VALUE_SIZE) " bytes). " \
    "This is ill-advised because the by-value return of this function is " \
    "designed and tuned for small scalar values, and will impose significant data copy overheads " \
    "(and possibly program stack overflow) when used with larger types. " \
    "Please call the function upcxx::" alternate " instead, which passes data by reference, " \
    "avoiding costly by-value copies of large data through stack temporaries. " \
    "The threshold for this error can be adjusted (at your own peril!) via -DUPCXX_MAX_VALUE_SIZE=n" \
  )
namespace upcxx { namespace detail {
  template <typename T>
  struct type_respects_static_size_limit {
    static constexpr bool value = sizeof(T) <= UPCXX_MAX_RPC_ARG_SIZE;
  };
}}
#define UPCXXI_STATIC_ASSERT_RPC_MSG(fnname) \
    "This program is attempting to pass an object with a large static type (over " UPCXXI_STRINGIFY(UPCXX_MAX_RPC_ARG_SIZE) " bytes) " \
    "to upcxx::" #fnname ". This is ill-advised because RPC is tuned for top-level argument objects that provide " \
    "fast move operations, and will impose significant data copy overheads (and possibly program stack overflow) " \
    "when used with larger types. Please consider instead passing a Serializable container for your large object, " \
    "such as a upcxx::view (e.g. `upcxx::make_view(&my_large_object, &my_large_object+1)`), to avoid costly data copies. " \
    "The threshold for this error can be adjusted (at your own peril!) via -DUPCXX_MAX_RPC_ARG_SIZE=n" 

//////////////////////////////////////////////////////////////////////
// Public API:

namespace upcxx {
  typedef int intrank_t;
  typedef unsigned int uintrank_t;
  
  enum class progress_level {
    internal,
    user
  };

  enum class entry_barrier {
    none,
    internal,
    user
  };
  
  namespace detail {
    // internal_only: Type used to mark member functions of public types for
    // internal use only. To do so, just make one of the function's arguments
    // of this type. The caller can just pass a default constructed dummy value.
    struct internal_only {
      explicit constexpr internal_only() {}
    };

    #define UPCXXI_CONCAT_(a, b) a ## b
    #define UPCXXI_CONCAT(a, b) UPCXXI_CONCAT_(a, b)
    // Macro for members that are intended to be private.
    #define UPCXXI_INTERNAL_ONLY(name) UPCXXI_CONCAT(private_detail_do_not_use_, name)
  }
    
  class persona;
  class persona_scope;
  class team;
  namespace detail {
    template<typename ...T>
    struct lpc_dormant;
    template<typename ...T>
    struct serialized_raw_tuple;
    template<typename ...T>
    struct deserialized_raw_tuple;
  }
  
  void init();
  bool initialized();
  void finalize();

  namespace experimental {
    void destroy_heap();
    void restore_heap();
  }
  
  intrank_t rank_n();
  intrank_t rank_me();
 
  UPCXXI_NODISCARD 
  void* allocate(std::size_t size,
                 std::size_t alignment = alignof(std::max_align_t));
  void deallocate(void *p);
  namespace detail {
    std::string shared_heap_stats();
  }
  std::int64_t shared_segment_size();
  std::int64_t shared_segment_used();
  
  bool in_progress();
  inline void progress(progress_level level = progress_level::user);
  
  persona& master_persona();
  void liberate_master_persona();
  
  persona& default_persona();
  persona& current_persona();
  persona_scope& default_persona_scope();
  persona_scope& top_persona_scope();
  
  // bool progress_required(persona_scope &ps = default_persona_scope());
  bool progress_required();
  bool progress_required(persona_scope &ps);

  // void discharge(persona_scope &ps = default_persona_scope());
  void discharge();
  void discharge(persona_scope &ps);
  
  namespace detail {
    int progressing();
  }
}

////////////////////////////////////////////////////////////////////////////////
// Backend API:

#if UPCXXI_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/handle_cb.hpp>
#endif

namespace upcxx {
namespace backend {
  extern int init_count;
  extern intrank_t rank_n;
  extern intrank_t rank_me;
  extern intrank_t nbrhd_set_size;
  extern intrank_t nbrhd_set_rank;
  extern bool verbose_noise;
  
  extern persona master;
  extern persona_scope *initial_master_scope;
  
  struct team_base; // backend-specific base class for teams (holds a handle usually)
  
  // This type is contained within `__thread` storage, so it must be:
  //   1. trivially destructible.
  //   2. constexpr constructible equivalent to zero-initialization.
  struct persona_state {
    #if UPCXXI_BACKEND_GASNET_PAR
      // personas carry their list of oustanding gasnet handles
      gasnet::handle_cb_queue hcbs;
    #else
      // personas carry no extra state
    #endif
  };

  void quiesce(const team &tm, entry_barrier eb);

  void warn_empty_rma(const char *fnname);

  // during_level and during_user removed post 2021.3.0 release

  /* fulfill_during: enlists a promise to be fulfilled in the given persona's
   * lpc queue. Since persona headers are lpc's and thus store queue linkage
   * intrusively, they must not be enlisted in multiple queues simultaneously.
   * Being in the queues of multiple personas is a race condition on the promise
   * so that shouldn't happen. Being in different progress-level queues of the
   * same persona is not a race condition, so it is up to the runtime to ensure
   * it doesn't use this mechanism for the same promise in different progress
   * levels. Its not impossible to extend the logic to guard against multi-queue
   * membership and handle it gracefully, but it would add cycles to what we
   * consider a critical path and so we'll eat this tougher invariant.
   */
  template<progress_level level, typename ...T>
  void fulfill_during(
      detail::future_header_promise<T...> *pro, // takes ref
      std::tuple<T...> vals,
      persona &active_per = current_persona()
    );
  template<progress_level level, typename ...T>
  void fulfill_during(
      detail::future_header_promise<T...> *pro, // takes ref
      std::intptr_t anon,
      persona &active_per = current_persona()
    );
  
  template<progress_level level, typename Fn>
  void send_am_master(intrank_t recipient, Fn &&fn);
  
  template<progress_level level, typename Fn>
  void send_am_persona(intrank_t recipient_rank, persona *recipient_persona, Fn &&fn);

  template<typename ...T, typename ...U>
  void send_awaken_lpc(intrank_t recipient, detail::lpc_dormant<T...> *lpc, std::tuple<U...> &&vals);

  template<progress_level level, typename Fn>
  void bcast_am_master(const team &tm, Fn &&fn);
  
  UPCXXI_ATTRIB_PURE
  intrank_t team_rank_from_world(const team &tm, intrank_t rank);
  UPCXXI_ATTRIB_PURE
  intrank_t team_rank_from_world(const team &tm, intrank_t rank, intrank_t otherwise);
  UPCXXI_ATTRIB_PURE
  intrank_t team_rank_to_world(const team &tm, intrank_t peer);

  #ifndef UPCXXI_ALL_RANKS_DEFINITELY_LOCAL
  // smp-conduit statically has exactly one local_team()
  #define UPCXXI_ALL_RANKS_DEFINITELY_LOCAL UPCXX_NETWORK_SMP
  #endif
  #if UPCXXI_ALL_RANKS_DEFINITELY_LOCAL
    constexpr bool all_ranks_definitely_local = true;
  #else
    constexpr bool all_ranks_definitely_local = false;
  #endif
  #define UPCXXI_ASSERT_VALID_DEFINITELY_LOCAL() \
          UPCXX_ASSERT(!::upcxx::backend::all_ranks_definitely_local || \
                       (::upcxx::backend::pshm_peer_lb_ == 0 && \
                        ::upcxx::backend::pshm_peer_n == ::upcxx::backend::rank_n), \
                       "Invalid UPCXXI_ALL_RANKS_DEFINITELY_LOCAL setting!");

  UPCXXI_ATTRIB_CONST
  bool rank_is_local(intrank_t r);
  
  UPCXXI_ATTRIB_PURE
  void* localize_memory(intrank_t rank, std::uintptr_t raw);
  UPCXXI_ATTRIB_PURE
  void* localize_memory_nonnull(intrank_t rank, std::uintptr_t raw);
  
  UPCXXI_ATTRIB_PURE
  std::tuple<intrank_t/*rank*/, std::uintptr_t/*raw*/> globalize_memory(void const *addr);
  UPCXXI_ATTRIB_PURE
  std::tuple<intrank_t/*rank*/, std::uintptr_t/*raw*/> globalize_memory(void const *addr, std::tuple<intrank_t,std::uintptr_t> otherwise);
  std::uintptr_t globalize_memory_nonnull(intrank_t rank, void const *addr);
}}

////////////////////////////////////////////////////////////////////////
// Public API implementations:

#if UPCXXI_BACKEND
  #define UPCXXI_ASSERT_INIT_NAMED(fnname) \
    UPCXX_ASSERT(::upcxx::backend::init_count != 0, \
     "Attempted to invoke " << fnname << " while the UPC++ library was not initialized " \
     "(before the first call to upcxx::init() or after the last call to upcxx::finalize()). " \
     "This function may only be called while the UPC++ library is in the initialized state.")
#else
  #define UPCXXI_ASSERT_INIT_NAMED(fnname) ((void)0)
#endif
#define UPCXXI_ASSERT_INIT() UPCXXI_ASSERT_INIT_NAMED("the library call shown above")

namespace upcxx {
  UPCXXI_ATTRIB_CONST
  inline intrank_t rank_n() {
    UPCXXI_ASSERT_INIT();
    return backend::rank_n;
  }
  UPCXXI_ATTRIB_CONST
  inline intrank_t rank_me() {
    UPCXXI_ASSERT_INIT();
    return backend::rank_me;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Include backend-specific headers:

#if UPCXXI_BACKEND_GASNET_SEQ || UPCXXI_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/runtime_fwd.hpp>
#endif

#endif
