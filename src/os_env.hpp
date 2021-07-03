#ifndef _7969554b_5147_4e01_a84a_9809eaf22e8a
#define _7969554b_5147_4e01_a84a_9809eaf22e8a

#include <upcxx/diagnostic.hpp>
#include <upcxx/backend_fwd.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cstddef>

namespace upcxx { 

  namespace detail {
    extern char *(*getenv)(const char *key);
    extern void (*getenv_report)(const char *key, const char *val, bool is_dflt);

    template<class T>
    struct os_env_parse;
    
    template<>
    struct os_env_parse<std::string> {
      std::string operator()(std::string x) {
        return x;
      }
    };
    
    template<class T>
    struct os_env_parse {
      T operator()(std::string x) {
        std::istringstream ss{x};
        T val;
        ss >> val;
        return val;
      }
    };

    template<class T>
    struct os_env_tostring;
    
    template<>
    struct os_env_tostring<std::string> {
      std::string operator()(const std::string & x) {
        return x;
      }
    };
    
    template<class T>
    struct os_env_tostring {
      std::string operator()(const T & x) {
        std::ostringstream ss;
	ss << x;
        return ss.str();
      }
    };
  } // namespace detail

 namespace experimental {
  template<class T>
  T os_env(const std::string &name);
  template<class T>
  T os_env(const std::string &name, const T &otherwise);

  template<>
  bool os_env(const std::string &name, const bool &otherwise);
  // mem_size_multiplier = default units (eg 1024=KB), or 0 for not a memory size
  std::int64_t os_env(const std::string &name, const std::int64_t &otherwise, std::size_t mem_size_multiplier);
 } // namespace experimental

  inline char *getenv_console(const char *env_var) {
    #ifdef UPCXXI_BACKEND
      UPCXX_ASSERT(initialized(), "UPC++ is not currently initialized");
    #endif
    UPCXX_ASSERT(detail::getenv);
    return detail::getenv(env_var);
  }

} // namespace upcxx

template<class T>
T upcxx::experimental::os_env(const std::string &name) {
  char const *key = name.c_str();
  std::string sval;
  
  char const *p = detail::getenv(key);
  UPCXX_ASSERT_ALWAYS(p, "Missing required environment variable setting: " << key);
  sval = p;
  
  return detail::os_env_parse<T>()(std::move(sval));
}

template<class T>
T upcxx::experimental::os_env(const std::string &name, const T &otherwise) {
  char const *key = name.c_str();
  char const *p = detail::getenv(key);
  std::string sval;
  if(p) sval = p;
  bool is_dflt = (sval.size() == 0);
 
  T result;
  if (is_dflt) result = otherwise;
  else         result = detail::os_env_parse<T>()(std::move(sval));

  detail::getenv_report(key, detail::os_env_tostring<T>()(result).c_str(), is_dflt);

  return result;
}

#endif
