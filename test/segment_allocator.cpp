#include <upcxx/upcxx.hpp>

#include "util.hpp"

#include <algorithm>
#include <iostream>

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

using upcxx::detail::segment_allocator;
using std::size_t;

int main() {
  upcxx::init();
  print_test_header();

  constexpr unsigned seg_size = 1<<20;
  void *seg = operator new(seg_size*sizeof(unsigned));
  segment_allocator seg_alloc(seg, seg_size*sizeof(unsigned));

  constexpr int max_blobs = 64<<10;
  unsigned **blobs = new unsigned*[max_blobs](/*null...*/);

  size_t total = 0;
  size_t min_total = (size_t)-1;
  size_t sum_total = 0;
  size_t fail_n = 0;
  
  for(int i=0; i < 1<<20; i++) {
    unsigned j = i;
    j *= 0x123456789;
    j ^= j >>13;
    j %= max_blobs;
    
    if(blobs[j]) {
      unsigned sz = blobs[j][0];
      for(unsigned k=1; k < sz; k++)
        UPCXX_ASSERT_ALWAYS(blobs[j][k] == k);

      seg_alloc.deallocate(blobs[j]);
      total -= sz;
      blobs[j] = nullptr;
    }
    else {
      unsigned sz = i*0xdeadbeef;
      sz ^= sz >> 11;
      sz *= 0xabcdef123;
      sz ^= sz >> 17;
      sz %= seg_size/10;
      sz += 1;
      sz = std::min<unsigned>(sz, seg_size-total);
      
      blobs[j] = (unsigned*)seg_alloc.allocate(sz*sizeof(unsigned), sizeof(unsigned));

      if(blobs[j] != nullptr) {
        total += sz;
        blobs[j][0] = sz;
        for(unsigned k=1; k < sz; k++)
          blobs[j][k] = k;
      }
      else { // alloc failed
        fail_n += 1;
        min_total = std::min(min_total, total);
        sum_total += total;
      }
    }
  }

  std::cout<<"Max wasted space: "<<100*(1 - double(min_total)/seg_size)<<"%"<<std::endl
           <<"Avg wasted space: "<<100*(1 - double(sum_total)/fail_n/seg_size)<<"%"<<std::endl;
  
  for(int j=0; j < max_blobs; j++)
    if(blobs[j])
      seg_alloc.deallocate(blobs[j]);
  
  delete[] blobs;
  operator delete(seg);

  upcxx::barrier();
  print_test_success();
  upcxx::finalize();
}
