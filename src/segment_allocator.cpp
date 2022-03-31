#include <upcxx/diagnostic.hpp>
#include <upcxx/segment_allocator.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

#include <algorithm>

using upcxx::detail::segment_allocator;

using std::size_t;
using std::uintptr_t;
using std::pair;

namespace {
  pair<uintptr_t, uintptr_t> make_pair_uu(uintptr_t a, uintptr_t b) {
    return {a, b};
  }
  
  template<typename Block>
  inline void insert_hole_by_size(std::map<pair<uintptr_t, uintptr_t>, Block*> &m, Block *b, uintptr_t size) {
    m.insert(std::make_pair(make_pair_uu(size, b->begin), b));
  }
}

GASNETT_COLD
segment_allocator::segment_allocator(void *segment_base, size_t segment_size) :
  seg_base_(reinterpret_cast<uintptr_t>(segment_base)),
  endpost_{0,segment_size,nullptr,nullptr} {

  if (!segment_size) return; // empty segment

  block *big_hole = new block{
    /*is_hole*/1,
    /*begin*/0,
    /*next, prev*/nullptr, &endpost_
  };
  this->endpost_.prev = big_hole;
  
  insert_hole_by_size(holes_by_size_, big_hole, segment_size);
}

GASNETT_COLD
segment_allocator::segment_allocator(segment_allocator &&that):
  segment_allocator(nullptr, 0) {
  *this = std::move(that);
}

GASNETT_COLD
segment_allocator& segment_allocator::operator=(segment_allocator &&that) {
  UPCXX_ASSERT(!this->seg_base_ && !this->endpost_.begin,
               "Move assignment is only allowed an an inactive segment_allocator");
  seg_base_ = that.seg_base_;
  holes_by_size_ = std::move(that.holes_by_size_);
  hunks_by_begin_ = std::move(that.hunks_by_begin_);

  that.seg_base_ = 0;

  this->endpost_ = that.endpost_;
  that.endpost_.begin = 0;
  that.endpost_.prev = nullptr;

  if(this->endpost_.prev != nullptr)
    this->endpost_.prev->next = &this->endpost_;

  return *this;
}

GASNETT_COLD
segment_allocator::~segment_allocator() {
  block *b = endpost_.prev;
  while(b != nullptr) {
    block *b1 = b->prev;
    delete b;
    b = b1;
  }
}

void* segment_allocator::allocate(size_t m_size, size_t m_align) {
  if(m_size == 0)
    return nullptr;
  
  if(m_size >= 2*4096) {
    m_size = (m_size + 4096-1) & (size_t)-4096;
    m_align = std::max<size_t>(4096, m_align);
  }
  else if(m_size >= 2*64) {
    m_size = (m_size + 64-1) & (size_t)-64;
    m_align = std::max<size_t>(64, m_align);
  }
  
  uintptr_t m_size_padded = m_size;
  
top:
  auto h_it = holes_by_size_.lower_bound(make_pair_uu(m_size_padded, 0));
  
  if(h_it == holes_by_size_.end())
    return nullptr;
  
  // h points to hole possibly big enough
  block *h = h_it->second;
  uintptr_t h_begin = h->begin;
  uintptr_t h_end = h->next->begin;
  
  uintptr_t m_begin = ((seg_base_ + h_begin + m_align-1) & -m_align) - seg_base_;
  uintptr_t m_end = m_begin + m_size;
  
  if(m_end <= h_end) { // we fit
    block *m; // will hold hunk allocated for user

    holes_by_size_.erase(h_it); // h will no longer a hole of the same size
    
    if(h_begin < m_begin) { // need hole on left
      if(h->prev && (m_begin - h_begin) < (h_end - h->prev->begin)/16) {
        // The hole we are about to create is "small" compared to its neighbors,
        // so just grow the neighbor to absorb it (ie induce internal fragmentation)
        // to avoid letting that hole be allocated (which could induce fragmentation).
        m = h;
        m->is_hole = 0;
        m->begin = m_begin;
      }
      else {
        m = new block{/*is_hole=*/0, m_begin, h, h->next};
        m->next->prev = m;
        h->next = m;

        insert_hole_by_size(holes_by_size_, h, m_begin - h_begin);
      }
    }
    else { // no hole on left, repurpose current hole
      m = h;
      m->is_hole = 0;
    }
    
    hunks_by_begin_[m_begin] = m;
    
    // right hole
    if(m_end < h_end) {
      block *hr = new block{/*is_hole=*/1, m_end, m, m->next};
      hr->next->prev = hr;
      m->next = hr;
      
      insert_hole_by_size(holes_by_size_, hr, h_end - m_end);
    }

    return reinterpret_cast<void*>(seg_base_ + m_begin);
  }
  else if(m_size_padded == m_size) {
    // uh-oh: alignment caused us to not fit
    m_size_padded = m_size + m_align-1;

    if(m_size_padded == m_size)
      return nullptr;
    else
      goto top;
  }
  else
    return nullptr;
}

void segment_allocator::deallocate(void *ptr) {
  if(ptr == nullptr)
    return;
  
  uintptr_t m_begin = reinterpret_cast<uintptr_t>(ptr) - this->seg_base_;

  block *m; {
    auto it = hunks_by_begin_.find(m_begin);
    UPCXX_ASSERT_ALWAYS(it != hunks_by_begin_.end(), "Invalid address to deallcoate.");
    
    m = it->second;
    hunks_by_begin_.erase(it);
  }
  
  uintptr_t m_end = m->next->begin;

  if(m->prev && m->prev->is_hole) { // fuse with left hole
    block *lh = m->prev; // left hole
    lh->next = m->next; // unlink m
    lh->next->prev = lh;

    // increase size of left hole
    holes_by_size_.erase(make_pair_uu(m_begin - lh->begin, lh->begin));
    insert_hole_by_size(holes_by_size_, lh, m_end - lh->begin);
    
    delete m;
    m = lh;
    m_begin = lh->begin;
  }
  else {
    m->is_hole = 1;
    insert_hole_by_size(holes_by_size_, m, m_end - m_begin);
  }

  // m is now a hole
  
  if(m->next->is_hole) { // fuse with right hole
    block *rh = m->next; // right hole
    uintptr_t rh_end = rh->next->begin;
    m->next = rh->next;
    m->next->prev = m;

    // right hole dies
    holes_by_size_.erase(make_pair_uu(rh_end - rh->begin, rh->begin));
    
    // m size changes to absorb right hole
    holes_by_size_.erase(make_pair_uu(rh->begin - m_begin, m_begin));
    insert_hole_by_size(holes_by_size_, m, rh_end - m_begin);
    
    delete rh;
  }
}
