#include <upcxx/digest.hpp>

using namespace std;

namespace {
  inline uint64_t bitrotl(uint64_t x, int sh) {
    return (x << sh) | (x >> (64-sh));
  }
}

upcxx::detail::digest upcxx::detail::digest::eat(uint64_t x0, uint64_t x1) const {
  uint64_t w0 = this->w0;
  uint64_t w1 = this->w1;
  uint64_t w2 = x0;
  uint64_t w3 = x1;
  
  // SpookyHash: a 128-bit noncryptographic hash function
  // By Bob Jenkins, public domain
  w3 ^= w2;  w2 = bitrotl(w2,15);  w3 += w2;
  w0 ^= w3;  w3 = bitrotl(w3,52);  w0 += w3;
  w1 ^= w0;  w0 = bitrotl(w0,26);  w1 += w0;
  w2 ^= w1;  w1 = bitrotl(w1,51);  w2 += w1;
  w3 ^= w2;  w2 = bitrotl(w2,28);  w3 += w2;
  w0 ^= w3;  w3 = bitrotl(w3, 9);  w0 += w3;
  w1 ^= w0;  w0 = bitrotl(w0,47);  w1 += w0;
  w2 ^= w1;  w1 = bitrotl(w1,54);  w2 += w1;
  w3 ^= w2;  w2 = bitrotl(w2,32);  w3 += w2;
  w0 ^= w3;  w3 = bitrotl(w3,25);  w0 += w3;
  w1 ^= w0;  w0 = bitrotl(w0,63);  w1 += w0;
  
  return digest{w0, w1};
}
