#include <upcxx/future/core.hpp>

using namespace std;

namespace detail = upcxx::detail;

using upcxx::detail::future_header;
using upcxx::detail::future_header_nil;
using upcxx::detail::future_header_dependent;
using upcxx::detail::future_body;
using upcxx::detail::future_body_proxy_;

template<typename ...T>
using future_header_result = upcxx::detail::future_header_result<T...>;

// IMPORTANT NOTE: We rely on this to be constant initialized so that
// functions like make_future() work correctly during dynamic
// initialization. The initializers here must all be constant
// expressions.
const future_header future_header_result<>::the_always_slow = {
  /*ref_n_*/-1,
  /*status_*/upcxx::detail::future_header::status_ready,
  /*sucs_head_*/nullptr,
  {/*result_*/const_cast<future_header*>(
                &upcxx::detail::future_header_result<>::the_always_slow
              )}
};

#if UPCXXI_PROMISE_VTABLE_HACK
const detail::promise_vtable detail::the_promise_vtable<>::vtbl{
  /*meta_offset_from_header*/
  offsetof(future_header_promise<>, pro_meta),
  /*fulfill_deferred_and_drop*/
  promise_vtable::fulfill_deferred_and_drop_trivial
};
#endif

namespace {
  __thread future_header_dependent **active_tail_ = nullptr;
}

void future_header::dependency_link::unlink() {
  if(suc != nullptr) {
    future_header::dependency_link **pp = &dep->sucs_head_;
    while(*pp != this)
      pp = &(*pp)->sucs_next;
    *pp = (*pp)->sucs_next;
  }
}

void future_header_dependent::entered_active() {
  if(active_tail_ != nullptr) {
    // add to existing active queue
    *active_tail_ = this;
    this->active_next_ = nullptr;
    active_tail_ = &this->active_next_;
  }
  else { // no active queue exists
    future_header_dependent *active_head = nullptr;
    active_tail_ = &active_head;
    
    // we are only inhabitant of active queue
    this->body_->leave_active(this);
    
    // active queue may have been just appended to, deplete it
    future_header_dependent *cur = active_head;
    while(cur != nullptr) {
      future_header_dependent *next = cur->active_next_;
      if(next == nullptr) {
        active_head = nullptr;
        active_tail_ = &active_head;
      }
      cur->body_->leave_active(cur);
      cur = next ? next : active_head;
    }
    
    // dislodge our queue
    active_tail_ = nullptr;
  }
}

void future_header::entered_ready_with_sucs(future_header *result, dependency_link *sucs_head) {
  /* Outlined:
  // caller gave us a reference in result->ref_n_
  this->result_ = result;
  this->status_ = future_header::status_ready;
  */
  
  // keep track of references we lose as we give our result to our successors
  int this_refs = this->ref_n_;
  UPCXX_ASSERT(this_refs > 0);
  int this_refs_unit = result == this ? 0 : 1;
  
  int result_refs = result->ref_n_;
  int result_refs_unit = result == this || result_refs < 0 ? 0 : 1;
  
  // begin an active queue if one isnt already in place
  future_header_dependent *active_head = nullptr;
  bool our_active_queue = active_tail_ == nullptr;
  if(our_active_queue)
    active_tail_ = &active_head;
  
  { // nobody will enter our sucs list while we traverse, because:
    // 1. we defer draining til after traversal, no functions called here.
    // 2. we're ready, so nobody should be entering our sucs_head_ anyway
    dependency_link *link = sucs_head;
    this->sucs_head_ = nullptr;
    
    while(link != nullptr) {
      future_header_dependent *suc = link->suc;
      
      link->dep = result;
      link->suc = nullptr; // mark as unlinked
      
      this_refs -= this_refs_unit;
      result_refs += result_refs_unit;
      
      if(--suc->status_ <= future_header::status_active) {
        // add to active queue
        *active_tail_ = suc;
        suc->active_next_ = nullptr;
        active_tail_ = &suc->active_next_;
      }
      
      link = link->sucs_next;
    }
  }
  
  if(0 == this_refs)
    // we're dying so take one back from result.
    // only possible if we've lost references, hence result_refs will not cross zero.
    result_refs -= result_refs_unit;
  
  // write back this->ref_n_
  if(this_refs_unit == 1) this->ref_n_ = this_refs;
  // write back result->ref_n_
  if(result_refs_unit == 1) result->ref_n_ = result_refs;
  
  if(0 == this_refs) {
    // we know result != this, hence we're a future_header_dependent
    delete static_cast<future_header_dependent*>(this);  
  }
  
  // if we made an active queue then we have to deplete it
  if(our_active_queue) {
    future_header_dependent *cur = active_head;
    while(cur != nullptr) {
      future_header_dependent *next = cur->active_next_;
      if(next == nullptr) {
        active_head = nullptr;
        active_tail_ = &active_head;
      }
      cur->body_->leave_active(cur);
      cur = next ? next : active_head;
    }
    active_tail_ = nullptr;
  }

  // Avoid dangling-pointer warning from GCC 12+
  UPCXXI_ASSUME(active_tail_ != &active_head);
}

void future_header_dependent::enter_proxying(
    future_body_proxy_ *body,
    future_header *proxied
  ) {
  
  // we defer deleting memory until the end to help compiler keep things in registers
  void *deferred_delete_1 = nullptr;
  future_header_dependent *deferred_delete_2 = nullptr;
  void *deferred_delete_3 = nullptr;
  
  if(proxied->status_ == future_header::status_proxying ||
     proxied->status_ == future_header::status_proxying_active
    ) {
    future_body_proxy_ *proxied_body = static_cast<future_body_proxy_*>(proxied->body_);
    future_header *proxied1 = proxied_body->link_.dep;
    
    proxied1->incref(1);
    
    if(0 == static_cast<future_header_dependent*>(proxied)->decref(1)) {
      // steal body from proxied, dont use given body
      deferred_delete_1 = body->storage_;
      body = proxied_body;
      
      // put ourself in proxied1 successor list
      body->link_.suc = this;
      
      deferred_delete_2 = static_cast<future_header_dependent*>(proxied);
    }
    
    proxied = proxied1;
  }
  
  if(proxied->status_ == future_header::status_ready) {
    deferred_delete_3 = body->storage_; // dont need body
    this->decref(1); // dependent entering ready loses ref
    UPCXX_ASSERT(this->ref_n_ != 0); // caller should have ensured we have an external observer
    this->enter_ready(future_header::drop_for_result(proxied));
  }
  else {
    this->status_ = future_header::status_proxying;
    
    // will write back these reference counts later
    int this_refs = this->ref_n_;
    int proxied_refs = proxied->ref_n_;
    bool proxied_refs_wb = proxied_refs >= 0;
    
    // look for successors that proxy us
    for(dependency_link **plink = &this->sucs_head_; *plink != nullptr;) {
      dependency_link *link = *plink;
      future_header_dependent *suc = link->suc;
      
      if(suc->status_ == future_header::status_proxying) {
        // our successor is proxying on us, make it point past us
        static_cast<future_body_proxy_*>(suc->body_)->link_.dep = proxied;
        
        // remove link from our sucs list
        *plink = link->sucs_next;
        
        // add link to proxied sucs
        link->sucs_next = proxied->sucs_head_;
        proxied->sucs_head_ = link;
        
        // our loss is proxied's gain
        this_refs -= 1;
        proxied_refs += 1;
      }
      else
        plink = &link->sucs_next;
    }
    
    if(this_refs == 0)
      proxied_refs -= 1;
    
    // write back proxied->ref_n_
    // we know it wont be zero because either we're referencing it or our successors are
    if(proxied_refs_wb) proxied->ref_n_ = proxied_refs;
    
    if(this_refs == 0) {
      // nobody points to us, so we die...
      future_body::operator delete(body->storage_);
      delete this;
    }
    else {
      // point to proxied
      body->link_.dep = proxied; // give them our reference
      
      // put ourself in proxied->sucs_head_
      body->link_.suc = this;
      body->link_.sucs_next = proxied->sucs_head_;
      proxied->sucs_head_ = &body->link_;
      
      this->ref_n_ = this_refs;
      this->body_ = body;
    }
  }
  
  future_body::operator delete(deferred_delete_1);
  delete deferred_delete_2;
  future_body::operator delete(deferred_delete_3);
}
