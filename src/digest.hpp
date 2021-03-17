#ifndef _9545d222_f897_445b_8627_3e4b934b0c18
#define _9545d222_f897_445b_8627_3e4b934b0c18

#include <cstdint>
#include <functional>
#include <iostream>

namespace upcxx {
namespace detail {
  struct digest {
    std::uint64_t w0, w1; // 2 64-bit words
    
    static constexpr digest zero() {
      return digest{0, 0};
    }
    
    digest eat(std::uint64_t x0, std::uint64_t x1=0) const;
    
    digest eat(digest that) const {
      return eat(that.w0, that.w1);
    }
    
    friend bool operator==(digest a, digest b) { return (a.w0 == b.w0) & (a.w1 == b.w1); }
    friend bool operator!=(digest a, digest b) { return (a.w0 != b.w0) | (a.w1 != b.w1); }
    
    friend bool operator< (digest a, digest b) { return (a.w0 <  b.w0) | ((a.w0 == b.w0) & (a.w1 <  b.w1)); }
    friend bool operator<=(digest a, digest b) { return (a.w0 <  b.w0) | ((a.w0 == b.w0) & (a.w1 <= b.w1)); }
    friend bool operator> (digest a, digest b) { return (a.w0 >  b.w0) | ((a.w0 == b.w0) & (a.w1 >  b.w1)); }
    friend bool operator>=(digest a, digest b) { return (a.w0 >  b.w0) | ((a.w0 == b.w0) & (a.w1 >= b.w1)); }
  };
  
  inline std::ostream& operator<<(std::ostream &o, digest x) {
    return o << '{'<<x.w0<<','<<x.w1<<'}';
  }
}
}

namespace std {
  template<>
  struct hash<upcxx::detail::digest> {
    size_t operator()(upcxx::detail::digest x) const {
      return x.w0;
    }
  };
}
#endif
