#ifndef _837545aa_e335_4355_b0ff_18d00c06c69a
#define _837545aa_e335_4355_b0ff_18d00c06c69a
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <iomanip>

#include <upcxx/digest.hpp>
#include <upcxx/utility.hpp>

#define UPCXXI_MAX_SEGCACHE_SIZE 20

#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
  #if UPCXXI_PLATFORM_OS_LINUX || UPCXXI_PLATFORM_OS_CNL || UPCXXI_PLATFORM_OS_WSL
    #define UPCXXI_EXEFORMAT_ELF 1
  #elif UPCXXI_PLATFORM_OS_FREEBSD
    #define UPCXXI_EXEFORMAT_ELF 1
  #elif UPCXXI_PLATFORM_OS_NETBSD
    #define UPCXXI_EXEFORMAT_ELF 1
  #elif UPCXXI_PLATFORM_OS_OPENBSD
    #define UPCXXI_EXEFORMAT_ELF 1
    #define UPCXXI_EXEFORMAT_ELF_UNREADABLE 1
  #elif UPCXXI_PLATFORM_OS_DARWIN
    #define UPCXXI_EXEFORMAT_MACHO 1
  #endif

  #if !UPCXXI_EXEFORMAT_ELF && !UPCXXI_EXEFORMAT_MACHO
    #define UPCXXI_CCS_INCOMPATIBLE 1
  #elif UPCXXI_PLATFORM_ARCH_BIG_ENDIAN && UPCXXI_PLATFORM_ARCH_POWERPC
    #define UPCXXI_CCS_INCOMPATIBLE 1
  #endif

  #if UPCXXI_CCS_INCOMPATIBLE
    #error "CCS not supported for current operating system. The CCS feature can be disabled by configuring UPC++ with --disable-ccs-rpc."
  #endif
#endif

namespace upcxx {
  enum class entry_barrier;
  class team;
  inline team& world();
namespace detail {
  struct segment_hash;

  struct function_token;
  struct function_token_ss;
  struct function_token_ms;
  struct function_token_invalid;

#if UPCXXI_FORCE_LEGACY_RELOCATIONS
  using FunctionTokenType = function_token_ss;
#else
  using FunctionTokenType = function_token;
#endif
}
namespace experimental {
  namespace relocation {}
  namespace relo = relocation;
}}

namespace std
{
  template<> struct hash<upcxx::detail::segment_hash>
  {
    inline size_t operator()(const upcxx::detail::segment_hash& h) const noexcept;
  };
}

namespace upcxx {
namespace detail {
  constexpr size_t elf_hash_size = 20;
  constexpr size_t macho_hash_size = 16;
  constexpr size_t default_hash_size = sizeof(size_t);

  static constexpr std::uintptr_t global_fnptr_null = 0;

  enum class segment_flags : uint16_t
  {
    none = 0x0,
    touched = 0x1,
    verified = 0x2,
    bad_verification = 0x4,
    bad_segment = 0x8,
    upcxx_binary = 0x10,
  };

  template<typename Fp>
  static std::uintptr_t fnptr_to_uintptr(Fp fp) noexcept {
    static_assert(sizeof(Fp) == sizeof(uintptr_t), "Function pointer has incompatible size");
    if(fp == nullptr)
      return global_fnptr_null;
    else {
      std::uintptr_t ans;
      std::memcpy(&ans, &fp, sizeof(Fp));
      return ans;
    }
  }

  template<typename Fp>
  static Fp fnptr_from_uintptr(std::uintptr_t u) noexcept {
    static_assert(sizeof(Fp) == sizeof(uintptr_t), "Function pointer has incompatible size");
    if(u == global_fnptr_null)
      return nullptr;
    else {
      Fp ans;
      std::memcpy(&ans, &u, sizeof(Fp));
      return ans;
    }
  }

  struct segment_hash
  {
#if UPCXXI_EXEFORMAT_ELF
    static constexpr size_t size = elf_hash_size;
#elif UPCXXI_EXEFORMAT_MACHO
    static constexpr size_t size = macho_hash_size;
#else
    static constexpr size_t size = default_hash_size;
#endif
    using array_type = std::array<uint8_t,size>;
    segment_hash() noexcept = default;
    segment_hash(const segment_hash&) noexcept = default;
    segment_hash(segment_hash&&) noexcept = default;
    segment_hash(const array_type& arr, size_t segnum = 0) noexcept
      : hash(arr)
    {
      add_segnum(segnum);
    }
    segment_hash(array_type&& arr, size_t segnum) noexcept
      : hash(std::move(arr))
    {
      add_segnum(segnum);
    }
    segment_hash(const uint8_t* arr, size_t segnum = 0) noexcept
      : hash{}
    {
      for (size_t i = 0; i < size; ++i)
        hash[i] = arr[i];
      add_segnum(segnum);
    }
    segment_hash(const upcxx::detail::fnv128& h) noexcept
      : hash{}
    {
      memcpy(&hash[0],&h.hash,(size > upcxx::detail::fnv128::size) ? upcxx::detail::fnv128::size : size);
    }
    inline void add_segnum(size_t segnum)
    {
      static_assert(size >= sizeof(size_t), "segment hash size must be at least sizeof(size_t)");
      *reinterpret_cast<size_t*>(&hash[0]) += segnum;
    }
    // align for std::hash
    alignas(uint64_t) array_type hash;
    inline uint8_t operator[](size_t i) const noexcept { return hash[i]; }
    friend inline bool operator<(const segment_hash& lhs, const segment_hash& rhs) noexcept
    {
      return lhs.hash < rhs.hash;
    }
    friend inline bool operator<=(const segment_hash& lhs, const segment_hash& rhs) noexcept
    {
      return lhs.hash <= rhs.hash;
    }
    friend inline bool operator==(const segment_hash& lhs, const segment_hash& rhs) noexcept
    {
      return lhs.hash == rhs.hash;
    }
    friend inline bool operator!=(const segment_hash& lhs, const segment_hash& rhs) noexcept
    {
      return lhs.hash != rhs.hash;
    }
    friend inline std::ostream& operator<<(std::ostream& out, const segment_hash& h) {
      out << std::setfill('0') << std::hex;
      for (size_t i = 0; i < size; ++i)
        out << std::setw(2) << static_cast<int>(h.hash[i]);
      out << std::setfill(' ') << std::dec;
      return out;
    }
    inline segment_hash& operator=(const upcxx::detail::fnv128& h)
    {
      if /* constexpr */ (size > upcxx::detail::fnv128::size) {
        memset(&hash[0],0,size);
        memcpy(&hash[0],&h.hash,upcxx::detail::fnv128::size);
      } else {
        memcpy(&hash[0],&h.hash,size);
      }
      return *this;
    }
    inline segment_hash& operator=(const segment_hash&) = default;
    inline segment_hash& operator=(segment_hash&&) = default;
    inline bool is_seghash() const
    {
      for (size_t i = 0; i < size; ++i)
        if (hash[i] != 0)
          return false;
      return true;
    }
  };

  struct segment_info
  {
    std::uintptr_t start;
    std::uintptr_t end;
    segment_hash ident;
    segment_hash lib_hash;
    uint16_t segnum;
    uint16_t flags;
    const char* dlpi_name;
#if UPCXXI_EXEFORMAT_ELF
    std::uintptr_t basis;
    // ElfW(Sym)*
    const void* symtbl;
    const void* symtblend;
    const char* strtbl;
#endif
    inline void set_verified() {
      flags |= static_cast<uint16_t>(segment_flags::verified);
      flags &= ~static_cast<uint16_t>(segment_flags::bad_verification);
    }
    inline void set_bad_verification() {
      flags |= static_cast<uint16_t>(segment_flags::bad_verification);
      flags &= ~static_cast<uint16_t>(segment_flags::verified);
    }
    inline friend bool operator==(const segment_info& lhs, const segment_info& rhs)
    {
      return std::tie(lhs.start, lhs.end, lhs.ident) ==
             std::tie(rhs.start, rhs.end, rhs.ident);
    }
  };

  // This type is contained within `__thread` storage, so it must be:
  //   1. trivially destructible.
  //   2. constexpr constructible equivalent to zero-initialization.
  class segmap_cache
  {
  public:
    static constexpr size_t max_cache_size = UPCXXI_MAX_SEGCACHE_SIZE;
  private:
    struct segment_lookup_ptr
    {
      uintptr_t start;
      uintptr_t end;
      segment_hash ident;
    };
    struct segment_lookup_tkn
    {
      segment_hash ident;
      uintptr_t start;
    };
    std::array<segment_lookup_ptr,max_cache_size> cache_ptr_; // sorted by address. address to token
    std::array<segment_lookup_tkn,max_cache_size> cache_tkn_; // sorted by hash. token to address

    template<typename It>
    static std::tuple<bool, It> search(It start, It end, uintptr_t uptr);
  public:
    using cache_ptr_iterator = typename decltype(cache_ptr_)::iterator;
    using const_cache_ptr_iterator = typename decltype(cache_ptr_)::const_iterator;
    using cache_tkn_iterator = typename decltype(cache_tkn_)::iterator;
    using const_cache_tkn_iterator = typename decltype(cache_tkn_)::const_iterator;
    using segment_iterator = typename std::vector<segment_info>::iterator;
    using const_segment_iterator = typename std::vector<segment_info>::const_iterator;

    static constexpr const char success_start[] = "\033[92m";
    static constexpr const char failure_start[] = "\033[91m";
    static constexpr const char verified[] = "\033[96m";
    static constexpr const char badseg[] = "\033[91m";
    static constexpr const char bold[] = "\033[1m";
    static constexpr const char ccolor_end[] = "\033[0m";
    static constexpr const char lookup_success[] = "FOUND";
    static constexpr const char lookup_failure[] = "FAILURE";
    static constexpr size_t padding = 2;
    static constexpr size_t cwidth_indicator = 2;
    static constexpr size_t cwidth_hash = 2*segment_hash::size + padding;
    static constexpr size_t cwidth_segment = 12 + padding;
    static constexpr size_t cwidth_flags = 8 + padding;
    static constexpr size_t cwidth_pointer = 14 + padding;
    static constexpr size_t cols = 6;

    static inline const segment_info& primary() noexcept { return primary_; }

    std::tuple<bool, const_cache_ptr_iterator> search_cache(uintptr_t uptr) const;
    std::tuple<uintptr_t, uintptr_t, segment_hash>   search_map(uintptr_t uptr);

    std::tuple<bool, const_cache_tkn_iterator> search_cache(const segment_hash& ident) const;
    std::tuple<bool, uintptr_t>   search_map(const segment_hash& ident);

    static std::vector<segment_info> build_segment_map();
    static void rebuild_segment_map();
    static void set_primary_segment(uintptr_t, entry_barrier);

    static inline void check_verification(uintptr_t start, uintptr_t end, uintptr_t uptr);
    inline bool cache_full() const {
      UPCXX_ASSERT(cache_occupancy_ <= max_cache_size);
      return cache_occupancy_ == max_cache_size;
    }

    template<typename R, typename... Args>
    static void debug_write_ptr(R(*)(Args...), std::ostream&, int color = 2);
    static void debug_write_ptr(uintptr_t, std::ostream&, int color = 2);
    static void debug_write_token(const function_token_ms& token, std::ostream&, int color = 2);
    static void debug_write_table(std::ostream&, int color = 2, size_t max_namelen = 0, bool print_top = true, size_t found_index = (size_t)-1, bool buffer = true);
    void debug_write_cache(std::ostream&);
    template<typename R, typename... Args>
    static void debug_write_ptr(R(*)(Args...), int fd = 2, int color = 2);
    static void debug_write_ptr(uintptr_t, int fd = 2, int color = 2);
    static void debug_write_token(const function_token_ms& token, int fd = 2, int color = 2);
    static void debug_write_table(int fd = 2, int color = 2);
    void debug_write_cache(int fd = 2);
    static const char* get_symbol(uintptr_t ptr);

    static void verify_segment(uintptr_t, entry_barrier eb);
    static void verify_all(entry_barrier eb);
    static inline bool enforce_verification(bool v) noexcept { bool prev = enforce_verification_; enforce_verification_ = v; return prev; }
    static inline bool verification_enforced() noexcept { return enforce_verification_; }
    static bool should_debug_color(int,int);
  private:
    static std::recursive_mutex mutex_;
    static segment_info primary_;
    static bool enforce_verification_;
    static std::unordered_map<uintptr_t,uint16_t> flag_map_;

    static size_t find_max_namelen();
    typename std::vector<segment_info>::iterator try_inactive(uintptr_t);
    void activate(segment_info&);
    static segment_info find_primary_upcxx_segment();

    static inline std::vector<segment_info>& segment_map() // in order of program headers
    {
      static std::vector<segment_info> instance = build_segment_map();
      return instance;
    }

    static void fallback_primary_segment_sentinel();

    size_t cache_occupancy_;
    size_t cache_evict_index_;
  };
}} // namespace upcxx::detail

#endif
