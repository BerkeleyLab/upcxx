#ifndef _f0b217aa_607e_4aa4_8147_82a0d66d6303
#define _f0b217aa_607e_4aa4_8147_82a0d66d6303

#if UPCXX_BACKEND
  #include <upcxx/upcxx.hpp>
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <stdio.h>

#ifdef UPCXX_USE_COLOR
  // These test programs are not smart enough to properly honor termcap
  // Don't issue color codes by default unless specifically requested
  #define KNORM  "\x1B[0m"
  #define KLRED "\x1B[91m"
  #define KLGREEN "\x1B[92m"
  #define KLBLUE "\x1B[94m"
#else
  #define KNORM  ""
  #define KLRED ""
  #define KLGREEN ""
  #define KLBLUE ""
#endif

// backwards-compatibility hacks for convenience of defect archaeology:
// ensure up-to-date versions of this header (and tests relying on it) still compile unchanged with older releases
#if UPCXX_VERSION >= 20201111
using upcxx::experimental::say;
using upcxx::experimental::os_env;
#elif UPCXX_VERSION >= 20200308
using upcxx::say;
using upcxx::os_env;
#else // before 2020.3.8
using say_ = upcxx::say;
#define say util_say
say_ &&say(const char *_discard="", say_ &&s=say_()) { return std::move(s); } 
  #if UPCXX_VERSION >= 20190900 
  using upcxx::os_env;
  #endif
#endif

#ifndef UTIL_ATTRIB_NOINLINE
#define UTIL_ATTRIB_NOINLINE __attribute__((__noinline__))
#endif

template<typename=void>
std::string test_name(const char *file) {
    size_t pos = std::string{file}.rfind("/");
    if (pos == std::string::npos) return std::string(file);
    return std::string{file + pos + 1};
}

template<typename=void>
inline void flush_all_output() {
    std::cout << std::flush;
    std::cerr << std::flush;
    fflush(0);
}

template<typename=void>
void print_test_header_inner(const char *file) {
    say("") << KLBLUE << "Test: " << test_name(file) << KNORM;
}

template<typename=void>
void print_test_success_inner(bool success=true) {
    flush_all_output();
    say("") << (success?KLGREEN:KLRED) << "Test result: "<< (success?"SUCCESS":"ERROR") << KNORM;
}

template<typename=void>
void print_test_skipped_inner(const char *reason, const char *success_msg="SUCCESS") {
    flush_all_output();
    say("")
        << KLBLUE << "Test result: "<< "SKIPPED" << KNORM << "\n"
        << "UPCXX_TEST_SKIPPED: This test was skipped due to: " << reason << "\n"
        << "Please ignore the following line which placates our automated test infrastructure:\n"
        << success_msg;
}

#if UPCXX_BACKEND
  template<typename=void>
  void print_test_header_(const char *file) {
      if(!upcxx::initialized() || !upcxx::rank_me()) {
          print_test_header_inner(file);
      }
      if(upcxx::initialized() && !upcxx::rank_me()) {
          say("") << KLBLUE << "Ranks: " << upcxx::rank_n() << KNORM;
      }
  }
  #define print_test_header()   print_test_header_(__FILE__)

  template<typename=void>
  void print_test_success(bool success=true) {
      if(upcxx::initialized()) {
          flush_all_output();
          // include a barrier to ensure all other threads have finished working.
          upcxx::barrier();
          // we could do a reduction here, but it's safer for rank 0 and any failing ranks to print
          if (!upcxx::rank_me() || !success) print_test_success_inner(success); 
      } else {
          print_test_success_inner(success); 
      }
  }

  template<typename=void>
  void print_test_skipped(const char *reason, const char *success_msg="SUCCESS") {
      if(upcxx::initialized()) {
          flush_all_output();
          // include a barrier to ensure all other threads have finished working.
          upcxx::barrier();
          if (!upcxx::rank_me()) print_test_skipped_inner(reason, success_msg);
      } else {
          print_test_skipped_inner(reason, success_msg);
      }
  }
#else
  #define print_test_header()      print_test_header_inner(__FILE__)
  #define print_test_success(...)  print_test_success_inner(__VA_ARGS__)
  #define print_test_skipped(...)  print_test_skipped_inner(__VA_ARGS__)
#endif

#define main_test_skipped(.../* reason, success_msg */) \
  int main() { \
    upcxx::init(); \
    print_test_header(); \
    print_test_skipped(__VA_ARGS__); \
    upcxx::finalize(); \
  }

template<typename T1, typename T2>
struct assert_same {
  static_assert(std::is_same<T1, T2>::value, "types differ");
  assert_same(){} // this helps avoid unused-value warnings on use (issue #468)
};

#endif
