// NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE 
//
// THIS HEADER FILE IS DEPRECATED - DO NOT ADD NEW USES OF THIS HEADER!
// New code should use upcxx::getenv_console or upcxx::experimental::os_env instead
//
// NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE 

#ifndef _325f0701_4dd7_4cfe_9b01_996de4980b99
#define _325f0701_4dd7_4cfe_9b01_996de4980b99

#include <upcxx/upcxx.hpp>

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace bench {
  template<class T>
  T os_env(const std::string &name);
  template<class T>
  T os_env(const std::string &name, const T &otherwise);

  namespace detail {
    const char *getenv(const char *var) {
      #if UPCXX_VERSION >= 20190307
        if (upcxx::initialized()) return upcxx::getenv_console(var); 
      #endif
      return std::getenv(var);
    }
    template<class T,
             bool is_arithmetic = std::is_arithmetic<T>::value>
    struct os_env_parse;
    
    template<>
    struct os_env_parse<std::string> {
      std::string operator()(std::string x) {
        return x;
      }
    };
    
    template<class T>
    struct os_env_parse<T,/*is_arithmetic=*/false> {
      T operator()(std::string x) {
        std::istringstream ss(x);
        T val;
        ss >> val;
        return val;
      }
    };
    
    template<class T>
    struct os_env_parse<T,/*is_arithmetic=*/true> {
      T operator()(std::string x) {
        std::istringstream ss(x);
        T val;
        ss >> val;
        
        switch(ss.peek()) {
        case 'B':
          break;
        case 'K':
          val *= 1<<10;
          break;
        case 'M':
          val *= 1<<20;
          break;
        case 'G':
          val *= 1<<30;
          break;
        default:
          return val;
        }
        ss.get();
        return val;
      }
    };
    
    template<class T>
    struct os_env_parse<std::vector<T>, false> {
      std::vector<T> operator()(std::string x, std::true_type is_arithmetic) {
        auto dots = x.find("...");
        
        if(dots != std::string::npos) {
          T lb = os_env_parse<T>()(x.substr(0, dots));
          T ub = os_env_parse<T>()(x.substr(dots + 3));
          std::vector<T> ans;
          
          if(ub - lb <= 20) {
            for(T i = lb; i <= ub; i += 1)
              ans.push_back(i);
          }
          else {
            for(T i = lb; i <= ub; i *= 2)
              ans.push_back(i);
          }
          
          return ans;
        }
        else 
          return this->operator()(std::move(x), std::false_type());
      }
      
      std::vector<T> operator()(std::string x, std::false_type is_arithmetic) {
        std::vector<T> ans;
        std::istringstream ss(x);
        std::string val;
        
        while(ss >> val) {
          ans.push_back(os_env_parse<T>()(val));
          
          while(1) {
            char c = ss.peek();
            if(c == ',' || c == ' ')
              ss.ignore();
            else
              break;
          }
        }
        
        return ans;
      }
      
      std::vector<T> operator()(std::string x) {
        return this->operator()(
          std::move(x),
          std::integral_constant<bool, std::is_arithmetic<T>::value>()
        );
      }
    };
  }
}

template<class T>
T bench::os_env(const std::string &name) {
  std::string sval;
  
  char const *p = detail::getenv(name.c_str());
  UPCXX_ASSERT(p != 0x0);
  sval = p;
  
  return detail::os_env_parse<T>()(std::move(sval));
}

template<class T>
T bench::os_env(const std::string &name, const T &otherwise) {
  std::string sval;
  
  char const *p = detail::getenv(name.c_str());
  if(p) sval = p;
  
  if(sval.size() != 0)
    return detail::os_env_parse<T>()(std::move(sval));
  else
    return otherwise;
}

#endif
