#ifndef _7949681d_8a89_4f83_afb9_de702bf1a46b
#define _7949681d_8a89_4f83_afb9_de702bf1a46b

#include <iostream>
#include <sstream>

#include <upcxx/upcxx_config.hpp>

namespace upcxx {
namespace detail {
  UPCXXI_ATTRIB_NORETURN
  void fatal_error(const char *msg, const char *title=nullptr, const char *func=0, const char *file=0, int line=0) noexcept;
  UPCXXI_ATTRIB_NORETURN
  inline void fatal_error(const std::string &msg, const char *title=nullptr, const char *func=0, const char *file=0, int line=0) noexcept {
    fatal_error(msg.c_str(), title, func, file, line);
  }

  UPCXXI_ATTRIB_NORETURN
  void assert_failed(const char *func, const char *file, int line, const char *msg=nullptr) noexcept;
  UPCXXI_ATTRIB_NORETURN
  inline void assert_failed(const char *func, const char *file, int line, const std::string &str) noexcept {
    assert_failed(func, file, line, str.c_str());
  }
}
}

#if (__GNUC__)
#define UPCXXI_FUNC __PRETTY_FUNCTION__
#else
#define UPCXXI_FUNC __func__
#endif

#ifndef UPCXXI_STRINGIFY
#define UPCXXI_STRINGIFY_HELPER(x) #x
#define UPCXXI_STRINGIFY(x) UPCXXI_STRINGIFY_HELPER(x)
#endif

// unconditional fatal error, with file/line and custom message
#define UPCXXI_FATAL_ERROR(ios_msg) \
  ::upcxx::detail::fatal_error(([&]() { ::std::stringstream _upcxx_fatal_ss; \
                                     _upcxx_fatal_ss << ios_msg; \
                                     return _upcxx_fatal_ss.str(); })(), \
                           nullptr, UPCXXI_FUNC, __FILE__, __LINE__)

#define UPCXXI_ASSERT_1(ok) \
 ( UPCXXI_PREDICT_TRUE(bool(ok)) ? (void)0 : \
   ::upcxx::detail::assert_failed(UPCXXI_FUNC, __FILE__, __LINE__, ::std::string("Failed condition: " #ok)) )

#define UPCXXI_ASSERT_2(ok, ios_msg) \
 ( UPCXXI_PREDICT_TRUE(bool(ok)) ? (void)0 : \
   ::upcxx::detail::assert_failed(UPCXXI_FUNC, __FILE__, __LINE__, \
        ([&]() { ::std::stringstream _upcxx_assert_ss; \
                 _upcxx_assert_ss << ios_msg; \
                 return _upcxx_assert_ss.str(); })()) )

#define UPCXXI_ASSERT_DISPATCH(_1, _2, NAME, ...) NAME

#ifndef UPCXXI_ASSERT_ENABLED
  #define UPCXXI_ASSERT_ENABLED 0
#endif

// Assert that will only happen in debug-mode.
#if UPCXXI_ASSERT_ENABLED
  #define UPCXX_ASSERT(...) UPCXXI_ASSERT_DISPATCH(__VA_ARGS__, UPCXXI_ASSERT_2, UPCXXI_ASSERT_1, _DUMMY)(__VA_ARGS__)
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
#define UPCXX_ASSERT_ALWAYS(...) UPCXXI_ASSERT_DISPATCH(__VA_ARGS__, UPCXXI_ASSERT_2, UPCXXI_ASSERT_1, _DUMMY)(__VA_ARGS__)

// In debug mode this will abort. In non-debug this is a nop.
#if UPCXXI_ASSERT_ENABLED
  #define UPCXXI_INVOKE_UB(...) UPCXXI_FATAL_ERROR(__VA_ARGS__)
#else
  #define UPCXXI_INVOKE_UB(...) UPCXXI_UNREACHABLE()
#endif

// static assert that is permitted in expression context
#define UPCXXI_STATIC_ASSERT(cnd, msg) ([=](){static_assert(cnd, msg);}())

// Asserting master persona - note the subtle semantic differences!
//
// * UPCXXI_ASSERT_(ALWAYS_)MASTER():
//   Assert the master persona is held by this thread in DEBUG mode (or always, ie also when assertions disabled)
//   Used for operations the *spec* says require holding the master persona (regardless of threadmode)
// 
// * UPCXXI_ASSERT_MASTER_HELD_IFSEQ():
//   Iff we are in SEQ mode, assert the master persona is held by this thread in DEBUG mode
//   Used for operations docs/implementation-defined.md says require *holding* master persona in SEQ
//
// * UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ():
//   Iff we are in SEQ mode, assert the master persona is current_persona() for this thread in DEBUG mode
//   Used for operations docs/implementation-defined.md says require master persona *current* in SEQ
//
// For operations requiring two of the above, the more generic UPCXXI_ASSERT_(ALWAYS_)MASTER() should appear first.
#define UPCXXI_ASSERT_ALWAYS_MASTER() \
        UPCXX_ASSERT_ALWAYS(backend::master.active_with_caller(), \
                     "This operation requires the master persona to appear in the persona stack of the calling thread")
#if UPCXXI_ASSERT_ENABLED
  #define UPCXXI_ASSERT_MASTER() UPCXXI_ASSERT_ALWAYS_MASTER()
  #define UPCXXI_ASSERT_MASTER_HELD_IFSEQ() (!UPCXXI_BACKEND_GASNET_SEQ ? ((void)0) : \
          UPCXX_ASSERT(::upcxx::master_persona().active_with_caller(), \
               "When compiled in threadmode=seq, this operation requires the primordial thread with the master persona in the persona stack.\n" \
               "Invoking certain UPC++ functions from multiple threads requires compiling with `upcxx -threadmode=par` or `UPCXX_THREADMODE=par`.\n" \
               "For details, please see `docs/implementation-defined.md`"))
  #define UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ() (!UPCXXI_BACKEND_GASNET_SEQ ? ((void)0) : \
          UPCXX_ASSERT(&::upcxx::current_persona() == &::upcxx::master_persona(), \
               "When compiled in threadmode=seq, this operation requires the primordial thread using the master persona as the current persona.\n" \
               "Applications with multi-threaded communication requirements should compile with `upcxx -threadmode=par` or `UPCXX_THREADMODE=par`.\n" \
               "For details, please see `docs/implementation-defined.md`"))
#else
  #define UPCXXI_ASSERT_MASTER() ((void)0)
  #define UPCXXI_ASSERT_MASTER_HELD_IFSEQ() ((void)0)
  #define UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ() ((void)0)
#endif

// asserting collective-safe context
#define UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED(fnname, eb) \
  UPCXX_ASSERT_ALWAYS(!(::upcxx::initialized() && ::upcxx::in_progress()), \
       "Collective operation " << fnname << " invoked within the restricted context. \n" \
       "Initiation of collective operations from within callbacks running inside user-level progress is prohibited.")
#define UPCXXI_ASSERT_COLLECTIVE_SAFE(eb) UPCXXI_ASSERT_COLLECTIVE_SAFE_NAMED("shown above", eb)

#ifndef UPCXX_WARN_EMPTY_RMA
#define UPCXX_WARN_EMPTY_RMA UPCXXI_ASSERT_ENABLED
#endif
#if UPCXX_WARN_EMPTY_RMA
#define UPCXXI_WARN_EMPTY(fnname, count) ( (count) == 0 ? backend::warn_empty_rma(fnname) : (void)0 )
#else
#define UPCXXI_WARN_EMPTY(fnname, count) ((void)0)
#endif

#if UPCXXI_ASSERT_ENABLED
#define UPCXXI_ASSERT_NOEXCEPTIONS_BEGIN try {
#define UPCXXI_ASSERT_NOEXCEPTIONS_END  \
    } catch(std::exception &_e) { \
      UPCXXI_FATAL_ERROR("An exception propagated outward into UPC++ library code, which is prohibited\n" << _e.what()); \
    } catch (...) { \
      UPCXXI_FATAL_ERROR("An exception propagated outward into UPC++ library code, which is prohibited"); \
    }
#else
  #define UPCXXI_ASSERT_NOEXCEPTIONS_BEGIN {
  #define UPCXXI_ASSERT_NOEXCEPTIONS_END   }
#endif

// UPCXXI_NODISCARD: The C++17 [[nodiscard]] attribute, when supported/enabled
// Auto-detection can be overridden by -DUPCXX_USE_NODISCARD=1/0
// issue 491: Some compilers report __has_cpp_attribute(nodiscard) but
// then issue warnings about use of the attribute under certain
// conditions (e.g. clang with -pedantic -std=c++14). You can override
// use of this attribute by #defining UPCXX_USE_NODISCARD=0 before
// including upcxx.hpp
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
  #define UPCXXI_NODISCARD [[nodiscard]]
#else
  #define UPCXXI_NODISCARD 
#endif

// UPCXXI_DEPRECATED("message"): The C++14 [[deprecated]] attribute, when supported/enabled
// Auto-detection can be overridden by -DUPCXX_USE_DEPRECATED=1/0
#ifndef UPCXX_USE_DEPRECATED
  // general case: trust __has_cpp_attribute when available
  // This *should* be sufficient for any C++11-compliant compiler
  #ifdef __has_cpp_attribute
    #if __has_cpp_attribute(deprecated)
      #define UPCXX_USE_DEPRECATED 1
    #endif
  #endif
  // exceptions:
  #if __clang__ && __cplusplus < 201402
    // clang advertises the attribute in -std=c++11 mode and then warns about it with -Wall
    #undef UPCXX_USE_DEPRECATED
  #elif __GNUC__ == 6 && __cplusplus < 201402
    // g++ 6 (only) advertises the attribute in -std=c++11 mode and then warns about it with -pedantic
    #undef UPCXX_USE_DEPRECATED
  #endif
#endif // !defined(UPCXX_USE_DEPRECATED)
#if UPCXX_USE_DEPRECATED
  #define UPCXXI_DEPRECATED(msg) [[deprecated(msg)]]
#else
  #define UPCXXI_DEPRECATED(msg) 
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
