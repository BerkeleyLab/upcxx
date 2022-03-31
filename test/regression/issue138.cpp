#include <upcxx/upcxx.hpp>

#include <vector>
#include <deque>
#include <list>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

#include "../util.hpp"

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";

#if MINIMAL
  static constexpr int scale = 1;
#else
  static constexpr int scale = 100;
#endif

template<typename T>
struct the_hasher {
  std::size_t operator()(T const &x) const {
    return std::hash<T>()(x);
  }
};
template<typename A, typename B>
struct the_hasher<std::pair<A,B>> {
  std::size_t operator()(std::pair<A,B> const &x) const {
    std::size_t h = std::hash<A>()(x.first);
    h *= 31;
    h += std::hash<B>()(x.second);
    return h;
  }
};

template<typename Seq, typename T, typename Elt>
void do_sequence(Elt elt) {
  for(int j=0; j < 10; j++) {
    Seq seq1, seq2;
    
    for(int i=0; i < 10*scale*j; i++)
      seq1.push_back(elt(i));
    for(int i=0; i < scale*j; i++)
      seq2.push_back(elt(i));
    
    upcxx::rpc(
      (upcxx::rank_me() + j%3) % upcxx::rank_n(),
      [=](Seq const &seq1, Seq const &seq2) {
        UPCXX_ASSERT_ALWAYS((int)seq1.size() == 10*scale*j);
        int i=0;
        for(auto const &x: seq1)
          UPCXX_ASSERT_ALWAYS(x == elt(i++));
        
        UPCXX_ASSERT_ALWAYS((int)seq2.size() == scale*j);
        i = 0;
        for(auto const &x: seq2)
          UPCXX_ASSERT_ALWAYS(x == elt(i++));
      },
      seq1, seq2
    ).wait();
  }
}

struct each_seq {
  template<typename T, typename Elt>
  void operator()(Elt elt) {
    do_sequence<std::vector<T>,T>(elt);
    do_sequence<std::deque<T>,T>(elt);
    do_sequence<std::list<T>,T>(elt);
  }
};

template<typename Set, typename T, typename Elt, typename EltOk>
void do_set(Elt elt, EltOk ok) {
  for(int j=0; j < 10; j++) {
    Set set1, set2;
    
    for(int i=0; i < 10*scale*j; i++)
      set1.insert(elt(i));
    auto set1_n = set1.size();
    
    for(int i=0; i < scale*j; i++)
      set2.insert(elt(i));
    auto set2_n = set2.size();
    
    upcxx::rpc(
      (upcxx::rank_me() + j%3) % upcxx::rank_n(),
      [=](Set const &set1, Set const &set2) {
        UPCXX_ASSERT_ALWAYS(set1_n == set1.size());
        for(auto const &x: set1)
          UPCXX_ASSERT_ALWAYS(ok(x));
        
        UPCXX_ASSERT_ALWAYS(set2_n == set2.size());
        for(auto const &x: set2)
          UPCXX_ASSERT_ALWAYS(ok(x));
      },
      set1, set2
    ).wait();
  }
}

struct each_set {
  template<typename T, typename Elt, typename EltOk>
  void operator()(Elt elt, EltOk ok) {
    do_set<std::set<T>,T>(elt,ok);
    do_set<std::unordered_set<T,the_hasher<T>>,T>(elt,ok);
  }
};

struct each_map {
  template<typename KV, typename Elt, typename EltOk>
  void operator()(Elt elt, EltOk ok) {
    using K = typename KV::first_type;
    using V = typename KV::second_type;
    do_set<std::set<KV>,KV>(elt,ok);
    do_set<std::unordered_set<KV,the_hasher<KV>>,KV>(elt,ok);
    do_set<std::map<K,V>,std::pair<K const,V>>(elt,ok);
    do_set<std::unordered_map<K,V,the_hasher<K>>,std::pair<K const,V>>(elt,ok);
  }
};

template<typename Fn>
void each_elt_for_seq(Fn fn) {
#if !MINIMAL
  fn.template operator()<char>(
    [](int i) { return alphabet[i % 4]; }
  );
  fn.template operator()<std::uint8_t>(
    [](unsigned i) { return std::uint8_t(i*i); }
  );
#endif
  fn.template operator()<std::uint16_t>(
    [](unsigned i) { return std::uint16_t(i*i); }
  );
#if !MINIMAL
  fn.template operator()<std::uint32_t>(
    [](unsigned i) { return std::uint32_t(i*i); }
  );
  fn.template operator()<std::uint64_t>(
    [](std::uint64_t i) { return i*i; }
  );
#endif
  fn.template operator()<float>(
    [](int i) { return float(i) + 3; }
  );
#if !MINIMAL
  fn.template operator()<double>(
    [](int i) { return double(i) - 301; }
  );
#endif
}

template<typename Fn>
void each_elt_for_set(Fn fn) {
#if !MINIMAL
  fn.template operator()<char>(
    [](int i) { return alphabet[i % 4]; },
    [](char x) { return 'a' <= x && x <= 'd'; }
  );
  fn.template operator()<std::uint8_t>(
    [](unsigned i) { return std::uint8_t(i*i)<<1; },
    [](std::uint8_t x) { return 0 == (x & 1); }
  );
  fn.template operator()<std::uint16_t>(
    [](unsigned i) { return std::uint16_t(i*i)<<1; },
    [](std::uint16_t x) { return 0 == (x & 1); }
  );
  fn.template operator()<std::uint32_t>(
    [](unsigned i) { return std::uint32_t(i*i)<<1; },
    [](std::uint32_t x) { return 0 == (x & 1); }
  );
#endif
  fn.template operator()<std::uint64_t>(
    [](std::uint64_t i) { return i*i<<1; },
    [](std::uint64_t x) { return 0 == (x & 1); }
  );
  fn.template operator()<std::string>(
    [](int i)->std::string {
      int n = i % 100;
      char hdr[2] = {char('0' + n%10), char('0' + n/10)};
      return std::string(hdr, 2) + std::string(n, alphabet[n%6]);
    },
    [](std::string const &s)->bool {
      int n = int(s[0]-'0') + 10*int(s[1]-'0');
      bool ok = (int)s.size() == 2 + n;
      for(int i=0; i < n; i++)
        ok &= s[2+i] == alphabet[n%6];
      return ok;
    }
  );
}

template<typename Fn>
void each_elt_for_map(Fn fn) {
#if !MINIMAL
  {
    using keyval = std::pair<unsigned,char>;
    auto key2val = [](unsigned k) { return alphabet[k%4]; };
    fn.template operator()<keyval>(
      [=](int i) { return std::make_pair(unsigned(i), key2val(i)); },
      [=](keyval x) { return x.second == key2val(x.first); }
    );
  }
  {
    using keyval = std::pair<unsigned,std::uint8_t>;
    auto key2val = [](unsigned k) { return std::uint8_t(k*k); };
    fn.template operator()<keyval>(
      [=](int i) { return std::make_pair(unsigned(i), key2val(i)); },
      [=](keyval x) { return x.second == key2val(x.first); }
    );
  }
  {
    using keyval = std::pair<unsigned,std::uint16_t>;
    auto key2val = [](unsigned k) { return std::uint16_t(k*k); };
    fn.template operator()<keyval>(
      [=](int i) { return std::make_pair(unsigned(i), key2val(i)); },
      [=](keyval x) { return x.second == key2val(x.first); }
    );
  }
  {
    using keyval = std::pair<unsigned,std::uint32_t>;
    auto key2val = [](unsigned k) { return std::uint32_t(k*k); };
    fn.template operator()<keyval>(
      [=](int i) { return std::make_pair(unsigned(i), key2val(i)); },
      [=](keyval x) { return x.second == key2val(x.first); }
    );
  }
#endif
  {
    using keyval = std::pair<unsigned,std::uint64_t>;
    auto key2val = [](unsigned k) { return std::uint64_t(k*k); };
    fn.template operator()<keyval>(
      [=](int i) { return std::make_pair(unsigned(i), key2val(i)); },
      [=](keyval x) { return x.second == key2val(x.first); }
    );
  }
  {
    using keyval = std::pair<std::string,std::string>;
    fn.template operator()<keyval>(
      [](int i)->keyval {
        return std::make_pair(std::to_string(i), std::string(i, alphabet[i%6]));
      },
      [](keyval const &xy)->bool {
        int n = 0;
        for(char c: xy.first) {
          n *= 10;
          n += c - '0';
        }
        bool ok = (int)xy.second.size() == n;
        for(char c: xy.second)
          ok &= c == alphabet[n%6];
        return ok;
      }
    );
  }
}

template<typename Fn>
struct each_subseq {
  template<typename SubSeq, typename T, typename Elt>
  void at_subseq(Elt elt) {
    Fn().template operator()<SubSeq>(
      [=](int i)->SubSeq {
        SubSeq seq;
        for(int j=0; j < i%10; j++)
          seq.push_back(elt(j));
        return seq;
      }
    );
  }
  
  template<typename T, typename Elt>
  void operator()(Elt elt) {
    this->template at_subseq<std::vector<T>,T>(elt);
    this->template at_subseq<std::deque<T>,T>(elt);
    this->template at_subseq<std::list<T>,T>(elt);
  }
};

void all() {
  each_elt_for_seq(each_seq());
  each_elt_for_seq(each_subseq<each_seq>());
  each_elt_for_set(each_set());
  each_elt_for_map(each_map());
}

int main() {
  upcxx::init();
  print_test_header();
  all();
  upcxx::barrier();
  print_test_success();
  upcxx::finalize();
}

