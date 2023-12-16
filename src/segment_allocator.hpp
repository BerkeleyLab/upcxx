#ifndef _94b3295f_75f9_4487_96e2_6b2f32033f16
#define _94b3295f_75f9_4487_96e2_6b2f32033f16

#include <map>
#include <unordered_map>
#include <cstdint>

namespace upcxx {
namespace detail {
  /* A crude but scalable allocator for carving up a contiguous address space
   * (aka segment).
   *  - Does not assume segment memory is accessible to CPU, hence all metadata
   *    stored out-of-segment in typical C++ heap.
   *  - Best-fit strategy, with first-fit used to break ties for same-size free blocks.
   *  - Performance: logarithmic in number of live blocks.
   *  - Coalesces adjacent free blocks eagerly.
   *  - Probably around ~8 words of metadata per block: i.e. not competitive with
   *    allocators that excel at small allocations.
   *  - *NOT* thread safe.
   */
  class segment_allocator {
    // block: a contiguous piece of memory that is either allocated to the user
    // (a hunk) or free space (a hole). Invariants:
    //  1. The entire segment is partitioned into an ordered list of adjacent
    //     blocks: ie no unclaimed space between blocks.
    //  2. A hole is never adjacent to another hole. Thus the transition from
    //     a hunk to a hole must immediately coalesce with any adjacent holes.
    struct block {
      std::uintptr_t
        is_hole:1, // 1-bit to encode if we're a hole or not
        // offset of block's low address in segment. The high offset can be
        // obtained by our "next" block's begin offset
        begin:(8*sizeof(std::uintptr_t)-1);

      // adjacent blocks, prev:low, next:high. prev may be null, but next never is.
      block *prev, *next;
    };
    
    std::uintptr_t seg_base_;
    block endpost_; // dummy block representing end of segment (always tail of linked list)

    // lex. sorted index of holes by (size, begin)
    std::map<std::pair<std::uintptr_t,std::uintptr_t>, block*> holes_by_size_;
    // index of hunks by begin
    std::unordered_map<std::uintptr_t, block*> hunks_by_begin_;
    
  public:
    segment_allocator(void *segment_base, std::size_t segment_size);
    segment_allocator(segment_allocator const&) = delete;
    segment_allocator(segment_allocator &&that);
    segment_allocator& operator=(segment_allocator &&that);
    ~segment_allocator();

    std::pair<void*,std::size_t> segment_range() const {
      return {reinterpret_cast<void*>(seg_base_), endpost_.begin};
    }

    bool in_segment(void *p) const {
      return reinterpret_cast<std::uintptr_t>(p) - seg_base_ < endpost_.begin;
    }

    std::int64_t segment_size() const {
      return (std::int64_t)endpost_.begin;
    }
    
    std::int64_t segment_free() const {
      std::int64_t free_space = 0;
      for (auto &p : holes_by_size_) {
        free_space += p.first.first;
      }
      return free_space;
    }
    
    void* allocate(std::size_t size, std::size_t align);
    void deallocate(void *p);
  };
}
}

#endif
