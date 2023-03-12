#ifndef _f0b217aa_607e_4aa4_8147_82a0d66d6303
#define _f0b217aa_607e_4aa4_8147_82a0d66d6303

#include <upcxx/upcxx.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <stdio.h>
#include <unistd.h>

// this is a correctness suite, so assertions default to enabled regarless of codemode:
#undef assert
#define assert UPCXX_ASSERT_ALWAYS

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

// Default GPU device, used by several tests
// DEVICE is defined iff configured with a available device kind,
// and can be overridden on the command-line to force a particular kind
#ifdef DEVICE // user override
  using Device = upcxx::DEVICE;
#else
  #if UPCXX_SPEC_VERSION < 20220300 // old: CUDA was the only kind
    #if UPCXX_KIND_CUDA
      #define DEVICE cuda_device
    #endif
    using Device = upcxx::cuda_device;
  #else // modern - leverage gpu_default_device
    #if UPCXX_KIND_CUDA || UPCXX_KIND_HIP || UPCXX_KIND_ZE
      #define DEVICE gpu_default_device
    #endif
    using Device = upcxx::gpu_default_device;
  #endif
#endif

template<typename=void>
std::string hostname() {
  char hostname[255] = {};
  int result = gethostname(hostname, sizeof(hostname));
  if (result || !hostname[0]) {
    return strerror(errno);
  } else {
    return hostname;
  }
}

template<typename=void>
std::string util_ranktxt() {
  // caches the rank information for this process
  static std::string result;
  static bool valid = false;
  if (valid) return result;
  std::ostringstream oss;
  oss << " (";
  if (upcxx::initialized()) {
    valid = true;
    oss << "rank " << upcxx::rank_me() << "/"  << upcxx::rank_n() << ": ";
  }
  oss << hostname();
  oss << ")";
  result = oss.str();
  return result;
}

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
    say("") << "Test: " << test_name(file);
}

template<typename=void>
void print_test_success_inner(bool success=true) {
    flush_all_output();
    say("") << "Test result: "<< (success?"SUCCESS":"ERROR") << util_ranktxt();
}

template<typename=void>
void print_test_skipped_inner(const char *reason, const char *success_msg="SUCCESS") {
    flush_all_output();
    say("")
        << "Test result: "<< "SKIPPED" << "\n"
        << "UPCXX_TEST_SKIPPED: This test was skipped due to: " << reason << "\n"
        << "Please ignore the following line which placates our automated test infrastructure:\n"
        << success_msg;
}

  template<typename=void>
  void print_test_header_(const char *file) {
      util_ranktxt(); // populate cache
      if(!upcxx::initialized() || !upcxx::rank_me()) {
          print_test_header_inner(file);
      }
      if(upcxx::initialized() && !upcxx::rank_me()) {
          say("") << "Ranks: " << upcxx::rank_n();
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
