#ifndef _aae7fcf0_f6aa_4389_af48_fa4208738a53
#define _aae7fcf0_f6aa_4389_af48_fa4208738a53
#include <upcxx/ccs_fwd.hpp>
#include <upcxx/persona.hpp>
#include <upcxx/diagnostic.hpp>

namespace upcxx {
  class segment_verification_error : public std::runtime_error {
    using std::runtime_error::runtime_error;
  };

namespace detail {
  std::string verification_failed_message(uintptr_t start, uintptr_t end, uintptr_t uptr);
  std::string tokenization_failed_message(uintptr_t uptr);
  std::string detokenization_failed_message(const function_token_ms& token);

  struct function_token_ss
  {
    template<typename Fp>
    Fp detokenize() const noexcept;

    template<typename R, typename... Args>
    static function_token_ss tokenize(R(*ptr)(Args...))
    {
      return tokenize(fnptr_to_uintptr(ptr));
    }
    static function_token_ss tokenize(uintptr_t ptr);

    void debug_write(int fd = 2, int color = 2) const;

    uintptr_t offset;
  };

  struct function_token_ms
  {
    template<typename Fp>
    Fp detokenize(segmap_cache& = upcxx::detail::the_persona_tls.segcache) const;

    template<typename R, typename... Args>
    static function_token_ms tokenize(R(*ptr)(Args...), const segmap_cache& cache = upcxx::detail::the_persona_tls.segcache)
    {
      return tokenize(fnptr_to_uintptr(ptr), cache);
    }
    static function_token_ms tokenize(uintptr_t ptr, segmap_cache& = upcxx::detail::the_persona_tls.segcache);

    void debug_write(int fd = 2, segmap_cache& = upcxx::detail::the_persona_tls.segcache, int color = 2) const;

    uintptr_t offset;
    segment_hash ident;
  };

  struct function_token
  {
    enum class identifier {
      automatic,
      single,
      multi
    };

    function_token() noexcept = default;
    function_token(std::nullptr_t) noexcept : active(), s() {}
    function_token(function_token_ss t) noexcept
      : active(identifier::single)
      , s(t)
    {}
    function_token(const function_token_ms& t) noexcept
      : active(identifier::multi)
      , m(t)
    {}
    function_token(function_token_ms&& t) noexcept
      : active(identifier::multi)
      , m(std::move(t))
    {}
    function_token(const function_token&) noexcept = default;
    function_token(function_token&&) noexcept = default;
    function_token& operator=(const function_token&) noexcept = default;
    function_token& operator=(function_token&&) noexcept = default;

    inline identifier token_ident() const { return active; }

    template<typename FunctionToken>
    const FunctionToken& get() const noexcept;

    template<typename Fp>
    Fp detokenize(segmap_cache& = upcxx::detail::the_persona_tls.segcache) const;

    template<typename R, typename... Args>
    static function_token tokenize(R(*ptr)(Args...), const segmap_cache& cache = upcxx::detail::the_persona_tls.segcache)
    {
      return tokenize(fnptr_to_uintptr(ptr));
    }
    static function_token tokenize(uintptr_t ptr, segmap_cache& = upcxx::detail::the_persona_tls.segcache);

    void debug_write(int fd = 2, segmap_cache& = upcxx::detail::the_persona_tls.segcache, int color = 2) const;

    identifier active;
    union {
      function_token_ss s;
      function_token_ms m;
    };
  };

  //////////////////////////////////////////////////////////////////////
  // implementation

  inline function_token_ss function_token_ss::tokenize(uintptr_t uptr)
  {
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
    UPCXX_ASSERT(uptr >= segmap_cache::primary().start && uptr < segmap_cache::primary().end, "Function pointer not in primary segment.");
#if UPCXXI_ASSERT_ENABLED
    segmap_cache::check_verification(segmap_cache::primary().start, segmap_cache::primary().end, uptr);
#endif
#endif
    return {uptr - segmap_cache::primary().start};
  }

  template<typename It>
  inline std::tuple<bool, It> segmap_cache::search(It start, It end, uintptr_t uptr)
  {
    auto it = std::upper_bound(start,end,uptr,[](uintptr_t p, const segmap_cache::segment_lookup_ptr& seg) {
      return p <= seg.end;
    });
    return {it != end && uptr >= it->start, it};
  }

  inline std::tuple<bool, typename segmap_cache::cl1_cache_ptr_iterator> segmap_cache::search_l1(uintptr_t uptr) const
  {
    using std::begin;
    using std::end;
    auto l1end = begin(l1_cache_ptr_)+cache_occupancy_;
    return search(begin(l1_cache_ptr_), l1end, uptr);
  }

  inline std::tuple<bool, typename segmap_cache::cl2_cache_ptr_iterator> segmap_cache::search_l2(uintptr_t uptr) const
  {
    using std::begin;
    using std::end;
    return search(begin(l2_cache_ptr_),end(l2_cache_ptr_),uptr);
  }

  inline std::tuple<bool, typename segmap_cache::segment_iterator> segmap_cache::search_all(uintptr_t uptr)
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // First, try searching inactive cache
    auto it = try_inactive(uptr);
    auto& segmap = segment_map();
    if (it != segmap.end())
      return {true, it};
    // Not found in inactive cache.
    // Try rebuilding segment map and search again
    rebuild_segment_map();
    it = try_inactive(uptr);
    if (it != segmap.end())
      return {true, it};
    return {false, segmap.end()};
  }

  inline function_token_ms function_token_ms::tokenize(uintptr_t uptr, segmap_cache& cache)
  {
    bool found;
    {
      typename segmap_cache::cl1_cache_ptr_iterator it;
      std::tie(found, it) = cache.search_l1(uptr);
      if (found)
      {
        // Found in active cache
        segmap_cache::check_verification(it->start, it->end, uptr);
        return {uptr-(it->start), it->ident};
      }
    }

    if (cache.l1_cache_full())
    {
      typename segmap_cache::cl2_cache_ptr_iterator it2;
      std::tie(found, it2) = cache.search_l2(uptr);
      if (found)
      {
        // Found in process-wide cache
        segmap_cache::check_verification(it2->start, it2->end, uptr);
        return {uptr-(it2->start), it2->ident};
      }
    }

    // Not found in active cache.

    {
      typename segmap_cache::csegment_iterator it3;
      std::tie(found, it3) = cache.search_all(uptr);
      if (found)
      {
        segmap_cache::check_verification(it3->start, it3->end, uptr);
        return {uptr-(it3->start), it3->ident};
      }
    }
    UPCXXI_FATAL_ERROR(tokenization_failed_message(uptr));
    return {0,{}};
  }

  inline function_token function_token::tokenize(uintptr_t uptr, segmap_cache& cache)
  {
    if (uptr >= segmap_cache::primary().start && uptr < segmap_cache::primary().end)
    {
      return {function_token_ss::tokenize(uptr)};
    } else {
      return {function_token_ms::tokenize(uptr, cache)};
    }
  }

  template<typename R, typename...Args>
  void segmap_cache::debug_write_ptr(R(*ptr)(Args...), int fd, int color)
  {
    debug_ptr(fnptr_to_uintptr(ptr),fd,color);
  }

  template<typename R, typename...Args>
  void segmap_cache::debug_write_ptr(R(*ptr)(Args...), std::ostream& out, int color)
  {
    debug_ptr(fnptr_to_uintptr(ptr),out,color);
  }

  inline void debug_write_ptr(uintptr_t uptr, int fd = 2, int color = 2);

  void segmap_cache::check_verification(uintptr_t start, uintptr_t end, uintptr_t uptr)
  {
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
    UPCXXI_IF_PF(segmap_cache::enforce_verification_ && !(flag_map_[start] & (uint16_t) segment_flags::verified))
    {
      throw segment_verification_error(verification_failed_message(start, end, uptr));
    }
#endif
  }

  template<typename Fp>
  Fp function_token_ss::detokenize() const noexcept
  {
    return fnptr_from_uintptr<Fp>(segmap_cache::primary().start + offset);
  }

  inline std::tuple<bool, typename segmap_cache::cl1_cache_tkn_iterator> segmap_cache::search_l1(const segment_hash& ident) const
  {
    using std::begin;
    auto l1begin = begin(l1_cache_tkn_);
    auto l1end = l1begin+cache_occupancy_;
    auto it = std::lower_bound(l1begin,l1end,ident,[](const segmap_cache::segment_lookup_tkn& seg, const segment_hash& id) {
      return seg.ident < id;
    });
    return {it != l1end && it->ident == ident, it};
  }

  inline uintptr_t segmap_cache::search_l2(const segment_hash& ident) const
  {
    return l2_cache_tkn_.at(ident);
  }

  inline std::tuple<bool, typename segmap_cache::segment_iterator> segmap_cache::search_all(const segment_hash& ident)
  {
    using std::begin;
    using std::end;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    for (segment_iterator it = begin(segmap); it != end(segmap); ++it)
    {
      if (ident == it->ident)
      {
        activate(*it);
        return {true, it};
      }
    }
    rebuild_segment_map();
    for (segment_iterator it = begin(segmap); it != end(segmap); ++it)
    {
      if (ident == it->ident)
      {
        activate(*it);
        return {true, it};
      }
    }
    return {false, end(segmap)};
  }

  template<typename Fp>
  Fp function_token_ms::detokenize(segmap_cache& cache) const
  {
    bool found;
    {
      typename segmap_cache::cl1_cache_tkn_iterator it;
      std::tie(found, it) = cache.search_l1(ident);
      if (found)
        return fnptr_from_uintptr<Fp>(it->start + offset);
    }

    if (cache.l1_cache_full())
    {
      try {
        uintptr_t uptr = cache.search_l2(ident) + offset;
        return fnptr_from_uintptr<Fp>(uptr);
      } catch (const std::out_of_range& e) {}
    }

    {
      typename segmap_cache::segment_iterator it2;
      std::tie(found, it2) = cache.search_all(ident);
      if (found)
        return fnptr_from_uintptr<Fp>(it2->start + offset);
    }

    UPCXXI_FATAL_ERROR(detokenization_failed_message(*this));
    return nullptr;
  }

  template<typename Fp>
  Fp function_token::detokenize(segmap_cache& cache) const
  {
    if (active == identifier::single)
      return s.detokenize<Fp>();
    else //if (active == identifier::multi)
      return m.detokenize<Fp>(cache);
  }

  template<>
  inline const function_token_ss& function_token::get<function_token_ss>() const noexcept
  {
    return s;
  }

  template<>
  inline const function_token_ms& function_token::get<function_token_ms>() const noexcept
  {
    return m;
  }

  template<>
  inline const function_token& function_token::get<function_token>() const noexcept
  {
    return *this;
  }
} // namespace detail

namespace experimental {
namespace relocation {
  template<typename R, typename... Args>
  void verify_segment(R(*ptr)(Args...), entry_barrier eb = entry_barrier::user)
  {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_ALWAYS_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
    detail::segmap_cache::verify_segment(detail::fnptr_to_uintptr(ptr), eb);
  }

  inline void verify_all(entry_barrier eb = entry_barrier::user)
  {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_ALWAYS_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
    detail::segmap_cache::verify_all(eb);
  }

  inline void rebuild_cache()
  {
    detail::segmap_cache::rebuild_cache();
  }

  inline bool enforce_verification(bool v)
  {
    return detail::segmap_cache::enforce_verification(v);
  }

  inline bool verification_enforced()
  {
    return detail::segmap_cache::verification_enforced();
  }

  template<typename R, typename... Args>
  void debug_write_ptr(R(*ptr)(Args...), std::ostream& out, int color = 2)
  {
    detail::segmap_cache::debug_write_ptr(detail::fnptr_to_uintptr(ptr), out, color);
  }

  inline void debug_write_segment_table(std::ostream& out, int color = 2)
  {
    detail::segmap_cache::debug_write_table(out,color);
  }

  template<typename R, typename... Args>
  void debug_write_ptr(R(*ptr)(Args...), int fd = 2, int color = 2)
  {
    detail::segmap_cache::debug_write_ptr(detail::fnptr_to_uintptr(ptr), fd, color);
  }

  inline void debug_write_segment_table(int fd = 2, int color = 2)
  {
    detail::segmap_cache::debug_write_table(fd,color);
  }

}}} // namespace upcxx::experimental::relocation

namespace std
{
  inline size_t hash<upcxx::detail::segment_hash>::operator()(const upcxx::detail::segment_hash& h) const noexcept
  {
    return *reinterpret_cast<const size_t*>(&h.hash[0]);
  }
}

#endif
