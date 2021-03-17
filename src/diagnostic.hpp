#ifndef _7949681d_8a89_4f83_afb9_de702bf1a46b
#define _7949681d_8a89_4f83_afb9_de702bf1a46b

#include <iostream>
#include <sstream>

namespace upcxx {
namespace detail {
  void fatal_error(const char *msg, const char *title=nullptr, const char *func=0, const char *file=0, int line=0);
  inline void fatal_error(const std::string &msg, const char *title=nullptr, const char *func=0, const char *file=0, int line=0) {
    fatal_error(msg.c_str(), title, func, file, line);
  }

  void assert_failed(const char *func, const char *file, int line, const char *msg=nullptr);
  inline void assert_failed(const char *func, const char *file, int line, const std::string &str) {
    assert_failed(func, file, line, str.c_str());
  }
}
}

#if (__GNUC__)
#define UPCXX_FUNC __PRETTY_FUNCTION__
#else
#define UPCXX_FUNC __func__
#endif

#ifndef UPCXX_STRINGIFY
#define UPCXX_STRINGIFY_HELPER(x) #x
#define UPCXX_STRINGIFY(x) UPCXX_STRINGIFY_HELPER(x)
#endif

// unconditional fatal error, with file/line and custom message
#define UPCXX_FATAL_ERROR(ios_msg) \
  ::upcxx::detail::fatal_error(([&]() { ::std::stringstream _upcxx_fatal_ss; \
                                     _upcxx_fatal_ss << ios_msg; \
                                     return _upcxx_fatal_ss.str(); })(), \
                           nullptr, UPCXX_FUNC, __FILE__, __LINE__)

#define UPCXX_ASSERT_1(ok) \
 ( (ok) ? (void)0 : \
   ::upcxx::detail::assert_failed(UPCXX_FUNC, __FILE__, __LINE__, ::std::string("Failed condition: " #ok)) )

#define UPCXX_ASSERT_2(ok, ios_msg) \
 ( (ok) ? (void)0 : \
   ::upcxx::detail::assert_failed(UPCXX_FUNC, __FILE__, __LINE__, \
        ([&]() { ::std::stringstream _upcxx_assert_ss; \
                 _upcxx_assert_ss << ios_msg; \
                 return _upcxx_assert_ss.str(); })()) )

#define UPCXX_ASSERT_DISPATCH(_1, _2, NAME, ...) NAME

#ifndef UPCXX_ASSERT_ENABLED
  #define UPCXX_ASSERT_ENABLED 0
#endif

// Assert that will only happen in debug-mode.
#if UPCXX_ASSERT_ENABLED
  #define UPCXX_ASSERT(...) UPCXX_ASSERT_DISPATCH(__VA_ARGS__, UPCXX_ASSERT_2, UPCXX_ASSERT_1, _DUMMY)(__VA_ARGS__)
#elif __PGI
  // PGI's warning #174-D "expression has no effect" is too stoopid to ignore `((void)0)` 
  // when it appears in an expression context before a comma operator.
  // This silly replacement seems sufficient to make it shut up, 
  // and should still add no runtime overhead post-inlining.
  namespace upcxx { namespace detail {
    static inline void noop(){}
  }}
  #define UPCXX_ASSERT(...) (::upcxx::detail::noop())
#else
  #define UPCXX_ASSERT(...) ((void)0)
#endif

// Assert that happens regardless of debug-mode.
#define UPCXX_ASSERT_ALWAYS(...) UPCXX_ASSERT_DISPATCH(__VA_ARGS__, UPCXX_ASSERT_2, UPCXX_ASSERT_1, _DUMMY)(__VA_ARGS__)

// In debug mode this will abort. In non-debug this is a nop.
#define UPCXX_INVOKE_UB() UPCXX_ASSERT(false, "Undefined behavior!")

// static assert that is permitted in expression context
#define UPCXX_STATIC_ASSERT(cnd, msg) ([=](){static_assert(cnd, msg);})

// asserting master persona
#define UPCXX_ASSERT_ALWAYS_MASTER() \
        UPCXX_ASSERT(backend::master.active_with_caller(), \
                     "This operation requires the calling thread to have the master persona")
#if UPCXX_ASSERT_ENABLED
  #define UPCXX_ASSERT_MASTER() UPCXX_ASSERT_ALWAYS_MASTER()
  #define UPCXX_ASSERT_MASTER_IFSEQ() (!UPCXX_BACKEND_GASNET_SEQ ? ((void)0) : \
          UPCXX_ASSERT(backend::master.active_with_caller(), \
               "This operation requires the calling thread to have the master persona, when compiled in threadmode=seq.\n" \
               "Multi-threaded applications should compile with `upcxx -threadmode=par` or `UPCXX_THREADMODE=par`.\n" \
               "For details, please see `docs/implementation-defined.md`"))
#else
  #define UPCXX_ASSERT_MASTER() ((void)0)
  #define UPCXX_ASSERT_MASTER_IFSEQ() ((void)0)
#endif

// asserting collective-safe context
#ifndef UPCXX_COLLECTIVES_IN_PROGRESS
#define UPCXX_COLLECTIVES_IN_PROGRESS 1 // whether or not to allow collective invocations in progress
#endif
#if !UPCXX_COLLECTIVES_IN_PROGRESS // hard prohibition against collectives in progress
#define UPCXX_ASSERT_COLLECTIVE_SAFE_NAMED(fnname, eb) \
  UPCXX_ASSERT_ALWAYS(!(::upcxx::initialized() && ::upcxx::in_progress()), \
       "Collective operation " << fnname << " invoked within the restricted context. \n" \
       "Initiation of collective operations from within callbacks running inside user-level progress is prohibited.")
#define UPCXX_ASSERT_COLLECTIVE_SAFE(eb) UPCXX_ASSERT_COLLECTIVE_SAFE_NAMED("shown above", eb)
#else // Allow collectives in progress with a deprecation warning
// eb is an entry_barrier constant actually representing the progress level
// cannot use progress_level enum because it lacks a "none" constant
#define UPCXX_ASSERT_COLLECTIVE_SAFE_NAMED(fnname, eb) ( \
    (::upcxx::initialized() && ::upcxx::in_progress()) ? \
    ::upcxx::backend::warn_collective_in_progress(fnname, eb) \
    : (void)0 )
#define UPCXX_ASSERT_COLLECTIVE_SAFE(eb) UPCXX_ASSERT_COLLECTIVE_SAFE_NAMED(UPCXX_FUNC, eb)
#endif

// UPCXX_NODISCARD: The C++17 [[nodiscard]] attribute, when supported/enabled
// Auto-detection can be overridden by -DUPCXX_USE_NODISCARD=1/0
#ifndef UPCXX_USE_NODISCARD
  // general case: trust __has_cpp_attribute when available
  // This *should* be sufficient for any C++11-compliant compiler
  #ifdef __has_cpp_attribute
    #if __has_cpp_attribute(nodiscard)
      #define UPCXX_USE_NODISCARD 1
    #endif
  #endif
  // exceptions:
  // (currently none in our supported compiler set)
#endif // !defined(UPCXX_USE_NODISCARD)
#if UPCXX_USE_NODISCARD
  #define UPCXX_NODISCARD [[nodiscard]]
#else
  #define UPCXX_NODISCARD 
#endif

namespace upcxx {
 namespace experimental {
  // ostream-like class which will print to the provided stream with an optional prefix and
  // as much atomicity as possible. Includes trailing newline (if missing).
  // usage:
  //   upcxx::experimental::say() << "hello world";
  // prints:
  //   [0] hello world \n
  class say {
    std::stringstream ss;
    std::ostream &target;
  public:
    // Optional arguments are the std::ostream to use,  
    // and the output prefix string, where `%d` (if present) is replaced by the rank number
    say(std::ostream &output, const char *prefix="[%d] ");
    say(const char *prefix="[%d] ") : say(std::cout, prefix) {}
    ~say();
    
    template<typename T>
    say& operator<<(T const &that) {
      ss << that;
      return *this;
    }
  };
 }
}

#endif
