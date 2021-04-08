#ifndef _f93ccf7a_35a8_49c6_b7b2_55c3c1a9640c
#define _f93ccf7a_35a8_49c6_b7b2_55c3c1a9640c

#ifndef UPCXX_BACKEND_GASNET_SEQ
  #define UPCXX_BACKEND_GASNET_SEQ 0
#endif

#ifndef UPCXX_BACKEND_GASNET_PAR
  #define UPCXX_BACKEND_GASNET_PAR 0
#endif

#define UPCXX_BACKEND_GASNET (UPCXX_BACKEND_GASNET_SEQ | UPCXX_BACKEND_GASNET_PAR)
#if UPCXX_BACKEND_GASNET && !UPCXX_BACKEND
#error Inconsistent UPCXX_BACKEND definition!
#endif

/* This header declares some core user-facing API to break include
 * cycles with headers included by the real "backend.hpp". This header
 * does not pull in the implementation of what it exposes, so you can't
 * use anything that has a runtime analog unless guarded by a:
 *   #ifdef UPCXX_BACKEND
 */

#include <upcxx/future/fwd.hpp>
#include <upcxx/diagnostic.hpp>
#include <upcxx/upcxx_config.hpp>
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

#define UPCXX_STATIC_ASSERT_VALUE_SIZE(T, fnname) \
  static_assert(sizeof(T) <= UPCXX_MAX_VALUE_SIZE, \
    "This program is attempting to pass an object with a large static type (over " UPCXX_STRINGIFY(UPCXX_MAX_VALUE_SIZE) " bytes) " \
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
#define UPCXX_STATIC_ASSERT_VALUE_RETURN_SIZE(fnname, alternate, ...) \
  static_assert(::upcxx::detail::type_respects_value_size_limit<__VA_ARGS__>::value, \
    "This program is calling upcxx::" fnname " to request the UPC++ library return an object by-value that has "\
    "a large static type (over " UPCXX_STRINGIFY(UPCXX_MAX_VALUE_SIZE) " bytes). " \
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
#define UPCXX_STATIC_ASSERT_RPC_MSG(fnname) \
    "This program is attempting to pass an object with a large static type (over " UPCXX_STRINGIFY(UPCXX_MAX_RPC_ARG_SIZE) " bytes) " \
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

    #define UPCXX_CONCAT_(a, b) a ## b
    #define UPCXX_CONCAT(a, b) UPCXX_CONCAT_(a, b)
    // Macro for members that are intended to be private.
    #define UPCXX_INTERNAL_ONLY(name) UPCXX_CONCAT(private_detail_do_not_use_, name)
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
 
  UPCXX_NODISCARD 
  void* allocate(std::size_t size,
                 std::size_t alignment = alignof(std::max_align_t));
  void deallocate(void *p);
  namespace detail {
    std::string shared_heap_stats();
  }
  std::int64_t shared_segment_size();
  std::int64_t shared_segment_used();
  
  void progress(progress_level level = progress_level::user);
  
  persona& master_persona();
  void liberate_master_persona();
  
  persona& default_persona();
  persona& current_persona();
  persona_scope& default_persona_scope();
  persona_scope& top_persona_scope();
  
  // bool progress_required(persona_scope &ps = top_persona_scope());
  bool progress_required();
  bool progress_required(persona_scope &ps);

  // void discharge(persona_scope &ps = top_persona_scope());
  void discharge();
  void discharge(persona_scope &ps);
  
  namespace detail {
    int progressing();
  }
}

////////////////////////////////////////////////////////////////////////////////
// Backend API:

#if UPCXX_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/handle_cb.hpp>
#endif

namespace upcxx {
namespace detail { struct device_allocator_base; }
namespace backend {
  extern int init_count;
  extern intrank_t rank_n;
  extern intrank_t rank_me;
  extern bool verbose_noise;
  
  extern persona master;
  extern persona_scope *initial_master_scope;
  
  struct team_base; // backend-specific base class for teams (holds a handle usually)
  
  // This type is contained within `__thread` storage, so it must be:
  //   1. trivially destructible.
  //   2. constexpr constructible equivalent to zero-initialization.
  struct persona_state {
    #if UPCXX_BACKEND_GASNET_PAR
      // personas carry their list of oustanding gasnet handles
      gasnet::handle_cb_queue hcbs;
    #else
      // personas carry no extra state
    #endif
  };

  struct heap_state {
    detail::device_allocator_base *alloc_base;

  #if UPCXX_CUDA_ENABLED && UPCXX_MAXEPS > 1
    static constexpr int max_heaps = UPCXX_MAXEPS;
  #else
    static constexpr int max_heaps = 33;
  #endif
    static_assert(max_heaps > 1, "bad value of UPCXX_MAXEPS");

    enum class memory_kind : std::uint32_t { 
      host = 0x40514051, 
      cuda = 0xC0DAC0DA,
    };
    heap_state(memory_kind k) : my_kind(k) {}
    memory_kind kind() { return my_kind; }

  protected:
    memory_kind my_kind; // serves as both tag and magic
    static heap_state *heaps[max_heaps];
    static int heap_count;
    static bool recycle;
    static bool use_mk_;
    static bool bug4148_workaround_;

  public:
    static void init();
    static bool use_mk() { return use_mk_; }
    static bool bug4148_workaround() { return bug4148_workaround_; }
    static int alloc_index() {
      UPCXX_ASSERT_ALWAYS(heap_count < max_heaps, "exceeded max device opens: " << max_heaps - 1);
      int idx;
      if (recycle) {
        for (idx=1; idx < max_heaps; idx++) {
          if (!heaps[idx]) break;
        }
      } else {
        idx = heap_count;
      }
      UPCXX_ASSERT_ALWAYS(idx < max_heaps && heaps[idx] == nullptr, "internal error on heap creation");
      heap_count++;
      return idx;
    }
    static void free_index(int heap_idx) {
      UPCXX_ASSERT_ALWAYS(heaps[heap_idx] == nullptr && heap_count > 1, "internal error on heap destruction");
      if (recycle) heap_count--;
    }

    // retrieve reference to heap_state pointer at heap_idx, with bounds-checking
    static inline heap_state *&get(std::int32_t heap_idx, bool allow_null = false) {
      UPCXX_ASSERT(heap_count <= max_heaps, "internal error in backend::heap_state::get");
      UPCXX_ASSERT(heap_idx > 0 && heap_idx < max_heaps, "invalid heap_idx (corrupted global_ptr?)");
      heap_state *&hs = heaps[heap_idx];
      UPCXX_ASSERT(hs || allow_null, "heap_idx referenced a null heap");
      return hs;
    }
  };

  void quiesce(const team &tm, entry_barrier eb);

  void warn_collective_in_progress(const char *fnname, entry_barrier eb=entry_barrier::none);

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
  
  intrank_t team_rank_from_world(const team &tm, intrank_t rank);
  intrank_t team_rank_from_world(const team &tm, intrank_t rank, intrank_t otherwise);
  intrank_t team_rank_to_world(const team &tm, intrank_t peer);

  extern const bool all_ranks_definitely_local;
  bool rank_is_local(intrank_t r);
  
  void* localize_memory(intrank_t rank, std::uintptr_t raw);
  void* localize_memory_nonnull(intrank_t rank, std::uintptr_t raw);
  
  std::tuple<intrank_t/*rank*/, std::uintptr_t/*raw*/> globalize_memory(void const *addr);
  std::tuple<intrank_t/*rank*/, std::uintptr_t/*raw*/> globalize_memory(void const *addr, std::tuple<intrank_t,std::uintptr_t> otherwise);
  std::uintptr_t globalize_memory_nonnull(intrank_t rank, void const *addr);
}}

////////////////////////////////////////////////////////////////////////
// Public API implementations:

#if UPCXX_BACKEND
  #define UPCXX_ASSERT_INIT_NAMED(fnname) \
    UPCXX_ASSERT(::upcxx::backend::init_count != 0, \
     "Attempted to invoke " << fnname << " while the UPC++ library was not initialized. " \
     "Please call upcxx::init() to initialize the library before calling this function.")
#else
  #define UPCXX_ASSERT_INIT_NAMED(fnname) ((void)0)
#endif
#define UPCXX_ASSERT_INIT() UPCXX_ASSERT_INIT_NAMED("the library call shown above")

namespace upcxx {
  inline intrank_t rank_n() {
    UPCXX_ASSERT_INIT();
    return backend::rank_n;
  }
  inline intrank_t rank_me() {
    UPCXX_ASSERT_INIT();
    return backend::rank_me;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Include backend-specific headers:

#if UPCXX_BACKEND_GASNET_SEQ || UPCXX_BACKEND_GASNET_PAR
  #include <upcxx/backend/gasnet/runtime_fwd.hpp>
#endif

#endif
