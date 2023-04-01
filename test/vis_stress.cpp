#include <upcxx/upcxx.hpp>
#include "util.hpp"

#include <new>

using upcxx::dist_object;
using upcxx::global_ptr;

struct ops_as_regular {
  template<typename T, int len>
  void put(T const *src, global_ptr<T> dest) {
    auto dest_ptrs = new global_ptr<T>[len];
    auto src_ptrs = new T const*[len];

    for(int i=0; i < len; i++) {
      dest_ptrs[i] = dest + i;
      src_ptrs[i] = src + i;
    }
    
    auto done = upcxx::rput_regular(src_ptrs, src_ptrs + len, 1, dest_ptrs, dest_ptrs + len, 1);
    done.wait();

    delete[] dest_ptrs;
    delete[] src_ptrs;
  }

  template<typename T, int len>
  void get(global_ptr<T> src, T *dest) {
    auto src_ptrs = new global_ptr<T>[len];
    auto dest_ptrs = new T*[len];
    
    for(int i=0; i < len; i++) {
      dest_ptrs[i] = dest + i;
      src_ptrs[i] = src + i;
    }
    
    auto done = upcxx::rget_regular(src_ptrs, src_ptrs + len, 1, dest_ptrs, dest_ptrs + len, 1);
    done.wait();

    delete[] dest_ptrs;
    delete[] src_ptrs;
  }
};

struct ops_as_irregular {
  template<typename T, int len>
  void put(T const *src, global_ptr<T> dest) {
    auto src_runs = new std::pair<T const* const, int>[len];
    auto dest_runs = new std::tuple<global_ptr<T>, const unsigned>[len];
    
    for(int i=0; i < len; i++) {
      new((void*)&src_runs[i]) std::pair<T const* const, int>(src + i, 1);
      new((void*)&dest_runs[i]) std::tuple<global_ptr<T>, const unsigned>(dest + i, 1);
    }
    
    auto done = upcxx::rput_irregular(src_runs, src_runs + len, dest_runs, dest_runs + len);
    done.wait();

    delete[] src_runs;
    delete[] dest_runs;
  }

  template<typename T, int len>
  void get(global_ptr<T> src, T *dest) {
    auto src_runs = new std::tuple<const global_ptr<T>, unsigned>[len];
    auto dest_runs = new std::pair<T*, const int>[len];
    
    for(int i=0; i < len; i++) {
      new((void*)&src_runs[i]) std::tuple<const global_ptr<T>, unsigned>(src + i, 1);
      new((void*)&dest_runs[i]) std::pair<T*, const int>(dest + i, 1);
    }
    
    auto done = upcxx::rget_irregular(src_runs, src_runs + len, dest_runs, dest_runs + len);
    done.wait();

    delete[] src_runs;
    delete[] dest_runs;
  }
};

struct ops_as_strided {
  template<typename T, int len>
  void put(T const *src, global_ptr<T> dest) {
    constexpr std::ptrdiff_t strides[1] = {sizeof(T)};
    constexpr std::size_t extents[1] = {len};
    
    auto done = upcxx::rput_strided<1>(src, strides, dest, strides, extents);
    done.wait();
  }

  template<typename T, int len>
  void get(global_ptr<T> src, T *dest) {
    constexpr std::ptrdiff_t strides[1] = {sizeof(T)};
    constexpr std::size_t extents[1] = {len};
    
    auto done = upcxx::rget_strided<1>(src, strides, dest, strides, extents);
    done.wait();
  }
};

template<typename Uint, int len, typename Ops>
void test_case(Ops ops) {
  dist_object<global_ptr<Uint>> dptr{upcxx::new_array<Uint>(len)};

  int n = upcxx::rank_n();
  int me = upcxx::rank_me();
  int nebr = (me+1) % n;
  
  global_ptr<Uint> nebr_ptr = dptr.fetch(nebr).wait();

  // allocate array of len+1 in case len==0, extra element never used
  static Uint src_data[len+1];
  for(int i=0; i < len; i++)
    src_data[i] = nebr*nebr + i;
  
  ops.template put<Uint,len>((Uint const*)src_data, nebr_ptr);
  
  upcxx::barrier();

  for(int i=0; i < len; i++)
    UPCXX_ASSERT_ALWAYS(dptr->local()[i] == Uint(me*me + i));

  #if 1 // enable gets
    // allocate array of len+1 in case len==0, extra element never used
    static Uint got_data[len+1];
    ops.template get<Uint,len>(nebr_ptr, got_data);

    for(int i=0; i < len; i++)
      UPCXX_ASSERT_ALWAYS(got_data[i] == src_data[i]);

    upcxx::barrier();
  #endif
  
  upcxx::delete_array(*dptr);
}

template<typename Uint, int len>
void sweep_given_len() {
  // loop body 2
  test_case<Uint,len>(ops_as_strided());
  test_case<Uint,len>(ops_as_regular());
  test_case<Uint,len>(ops_as_irregular());  
}

template<typename Uint>
void sweep_given_type() {
  // loop body 1
  sweep_given_len<Uint,0>();
  sweep_given_len<Uint,1>();
  sweep_given_len<Uint,2>();
  if (os_env<bool>("UPCXX_OVERSUBSCRIBED",false)) {
    sweep_given_len<Uint,100>();
  } else { 
    sweep_given_len<Uint,123456>();
  }
}

int main() {
  upcxx::init();
  print_test_header();

  // loop body 0
  sweep_given_type<uint8_t>();
  sweep_given_type<uint16_t>();
  sweep_given_type<uint32_t>();
  sweep_given_type<uint64_t>();

  print_test_success();
  upcxx::finalize();
}
