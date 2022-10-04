#ifndef _4281eee2_6d52_49d0_8126_75b21f8cb178
#define _4281eee2_6d52_49d0_8126_75b21f8cb178

#include <upcxx/future/fwd.hpp>

#include <upcxx/diagnostic.hpp>
#include <upcxx/lpc.hpp>
#include <upcxx/utility.hpp>

#include <cstddef>
#include <new>

/* Place this macro in a class definition to give it overrides of operator
 * new/delete that just call the language provided defaults. This might seem
 * pointless now, but if we ever customize our own allocator we'll need
 * something like this slapped on all of our classes (except it won't just
 * call the std defaults). The reason we're doing this here (and now) is because
 * there is code in "./core.cpp" that wants to delete `void*` storage which it
 * *knows* must have been allocated for a `future_body`. Without these stubs,
 * calling `future_body::operator delete(x)` would be invalid. Adding these
 * stubs seems better than dumbing down offending code to always call the generic
 * deallocate, which if we get custom allocation might be slower or just wrong.
 */
#ifndef UPCXXI_OPNEW_AS_STD // pretty sure not defined anywhere, just thinking ahead.
  #define UPCXXI_OPNEW_AS_STD \
    static void* operator new(std::size_t size) {\
      return ::operator new(size);\
    }\
    static void operator delete(void *p) {\
      ::operator delete(p);\
    }
#endif

#if __PGI // TODO: range of impacted versions and/or C++ standard?
  // Work around a bug leading to nullptr initialization for a function pointer
  // See PR#119 for more details
  #define UPCXXI_PROMISE_VTABLE_HACK 1
#endif

#ifndef UPCXXI_ISSUE_485_SLOW_THE_ALWAYS
// Old intel needs a suboptimal hack, see issue 485 and PR 357
// issue 549: hipcc device compilation implements a subset of C++ that excludes dynamic initialization
#define UPCXXI_ISSUE_485_SLOW_THE_ALWAYS ( \
   (__INTEL_COMPILER && __INTEL_COMPILER < 1900) || \
   __HIP_DEVICE_COMPILE__ \
   )
#endif

namespace upcxx {
  namespace detail {
    //////////////////////////////////////////////////////////////////////
    // future headers...
  
    struct future_header {
      UPCXXI_OPNEW_AS_STD
      
      // Our refcount. A negative value indicates a static lifetime.
      int ref_n_;
      
      // Status codes:
      static constexpr int 
        // Any greater-than-zero status value means we are waiting for
        // that many other futures until we hit "status_active". When
        // one future notifies a dependent future of its completion,
        // it simply decrements the dependent's status.

        // This future's dependencies have all finished, but this one
        // needs its body's `leave_active()` called.
        status_active = 0,
        
        // This future finished. Its `result_` member points to a
        // future_header_result<T...> containing the result value.
        status_ready = -1,
        
        // This future is proxying for another one which is not
        // finished. Has body of type `future_body_proxy<T...>`.
        status_proxying = -2,
        
        // This future is proxying for another future which is ready.
        // We need to be marked as ready too.
        status_proxying_active = -3;
      
      int status_;
      
      struct dependency_link {
        // The dependency future: one being waited on.
        future_header *dep;
        
        // The successor future: one waiting for dependency.
        future_header_dependent *suc; // nullptr if not in a successor list
        
        // List pointer for dependency's successor list.
        dependency_link *sucs_next;
        
        void unlink();
      };
      
      // List of successor future headers waiting on us.
      dependency_link *sucs_head_;
      
      union {
        // Used when: status_ == status_ready.
        // Points to the future_header_result<T...> holding our results,
        // which might actually be "this".
        future_header *result_;
        
        // Used when: status_ != status_ready && this is instance of future_header_dependent.
        // Holds callback to know what to do when we become active.
        future_body *body_;
      };
      
      // Called by enter_ready() if we have successors.
      void entered_ready_with_sucs(future_header *result, dependency_link *sucs_head);
      
      // Tell this header to enter the ready state, will adopt `result`
      // as its result header (assumes its refcount is already incremented).
      void enter_ready(future_header *result) {
        // caller gave us a reference in result->ref_n_
        this->result_ = result;
        this->status_ = future_header::status_ready;
        
        if(this->sucs_head_ != nullptr)
          this->entered_ready_with_sucs(result, this->sucs_head_);
      }

      // Modify the refcount, but do not take action.
      future_header* incref(int n) {
        int ref_n = this->ref_n_;
        if (ref_n >= 0) this->ref_n_ = ref_n + n;
        return this;
      }
      int decref(int n) { // returns new refcount
        int ref_n = this->ref_n_;
        if (ref_n >= 0) {
          ref_n -= n;
          this->ref_n_ = ref_n;
        }
        return ref_n;
      }
      
      // Drop reference to "a" in favor of "a->result_" (returned).
      static future_header* drop_for_result(future_header *a);
      
      // Drop reference to "a" in favor of its proxied header (returned).
      // "a->status_" must be "status_proxying" or "status_proxying_active".
      static future_header* drop_for_proxied(future_header *a);
    };

    template<typename=void>
    struct future_header_nil1 {
      // The "nil" future, not to be used. Only exists so that future_impl_shref's don't
      // have to test for nullptr, instead they'll just see its negative ref_n_.
      static constexpr future_header the_nil = {
        /*ref_n_*/-1,
        /*status_*/future_header::status_active + 666,
        /*sucs_head_*/nullptr,
        {/*result_*/nullptr}
      };
      
      static constexpr future_header* nil() {
        return const_cast<future_header*>(&the_nil);
      }
    };

    template<typename VoidThanks>
    constexpr future_header future_header_nil1<VoidThanks>::the_nil;

    using future_header_nil = future_header_nil1<>;
    
    #if !UPCXXI_ISSUE_485_SLOW_THE_ALWAYS
    // The fast "always" future, not to be used by anything other than
    // future_header_result<>::always(). Optimization for always-ready
    // empty futures.
    struct future_header_always2 final: future_header {
      // This is a separate class so that we can constexpr initialize
      // the_always.result_ to point to the_always itself. This way
      // things like drop_for_result() work correctly.
      constexpr future_header_always2(): future_header{
        /*ref_n_*/-1,
        /*status_*/future_header::status_ready,
        /*sucs_head_*/nullptr,
        {/*result_*/this}
      } {}
    };

    template<typename=void>
    struct future_header_always1 {
      static constexpr future_header_always2 the_always_fast{};
    };

    template<typename VoidThanks>
    constexpr future_header_always2 future_header_always1<VoidThanks>::the_always_fast;
    #endif // !UPCXXI_ISSUE_485_SLOW_THE_ALWAYS

    ////////////////////////////////////////////////////////////////////
    // future_header_dependent: dependent headers are those that...
    // - Wait for other futures to finish and then fire some specific action.
    // - Use their bodies to store their state while in wait.
    // - Don't store their own results, their "result_" points to the
    //   future_header_result<T...> holding the result.
    
    struct future_header_dependent final: future_header {
      // For our potential membership in the singly-linked "active queue".
      future_header_dependent *active_next_;
      
      future_header_dependent() {
        // dependents carry an extra reference until they become ready
        this->ref_n_ = 2;
        this->status_ = future_header::status_active;
        this->sucs_head_ = nullptr;
      }
      
      // Notify engine that this future has just entered an active state.
      // The "status_" must be "status_active" or "status_proxying_active".
      void entered_active();
      
      // Put this future into the "status_proxying" state using a constructed
      // future_body_proxy<T...> instance and the future to be proxied's header.
      void enter_proxying(future_body_proxy_ *body, future_header *proxied);
      
      // Override refcount arithmetic with more efficient form since we
      // know future_header_dependents are never statically allocated.
      future_header_dependent* incref(int n) {
        this->ref_n_ += n;
        return this;
      }
      int decref(int n) {
        return (this->ref_n_ -= n);
      }
    };
    
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_general: Header operations given no special
    // knowledge.
    
    struct future_header_ops_general {
      static constexpr bool is_trivially_ready_result = false;
      static constexpr bool is_possibly_dependent = true;
      
      template<typename ...T>
      static void incref(future_header *hdr);
      
      template<typename ...T, bool maybe_nil=true>
      static void dropref(future_header *hdr, std::integral_constant<bool,maybe_nil> = {});
      
      template<typename ...T>
      static void delete1(future_header *hdr);
    };
    
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_result: Header operations given this header is
    // an instance of `future_header_result<T...>`.
    
    struct future_header_ops_result: future_header_ops_general {
      static constexpr bool is_trivially_ready_result = false;
      static constexpr bool is_possibly_dependent = false;
      
      template<typename ...T>
      static void incref(future_header *hdr);
      
      template<typename ...T, bool maybe_nil=true>
      static void dropref(future_header *hdr, std::integral_constant<bool,maybe_nil> = {});
      
      template<typename ...T>
      static void delete1(future_header *hdr);
    };
    
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_result: Header operations given this header is
    // an instance of `future_header_result<T...>` and is ready.
    
    struct future_header_ops_result_ready: future_header_ops_result {
      static constexpr bool is_trivially_ready_result = true;
      static constexpr bool is_possibly_dependent = false;
      
      template<typename ...T>
      static void incref(future_header *hdr);
      
      template<typename ...T, bool maybe_nil=true>
      static void dropref(future_header *hdr, std::integral_constant<bool,maybe_nil> = {});
      
      template<typename ...T>
      static void delete1(future_header *hdr);
    };
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_promise: Header operations given this header is
    // an instance of `future_header_promise<T...>`.
    
    struct future_header_ops_promise: future_header_ops_general {
      static constexpr bool is_trivially_ready_result = false;
      static constexpr bool is_possibly_dependent = false;
      
      template<typename ...T>
      static void incref(future_header *hdr);
      
      template<typename ...T, bool maybe_nil=true>
      static void dropref(future_header *hdr, std::integral_constant<bool,maybe_nil> = {});
      
      template<typename ...T>
      static void delete1(future_header *hdr);
    };
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_dependent: Header operations given this header is
    // an instance of `future_header_dependent`.
    
    struct future_header_ops_dependent: future_header_ops_general {
      static constexpr bool is_trivially_ready_result = false;
      static constexpr bool is_possibly_dependent = true;
      
      template<typename ...T>
      static void incref(future_header *hdr);
      
      template<typename ...T, bool maybe_nil=true>
      static void dropref(future_header *hdr, std::integral_constant<bool,maybe_nil> = {});
      
      template<typename ...T>
      static void delete1(future_header *hdr);
    };
    
    ////////////////////////////////////////////////////////////////////
    // future_body: Companion objects to headers that hold the
    // polymorphic features of the future.
    
    // Base type for all future bodies.
    struct future_body {
      UPCXXI_OPNEW_AS_STD
      
      // The memory block holding this body. Managed by future_body::operator new/delete().
      void *storage_;
      
      future_body(void *storage): storage_(storage) {}
      
      // Tell this body that its time to leave the "active" state by running
      // its specific action.
      virtual void leave_active(future_header_dependent *owner_hdr) = 0;
    };
    
    // Base type for bodies that proxy other futures.
    struct future_body_proxy_: future_body {
      // Our link in the proxied-for future's successor list.
      future_header::dependency_link link_;
      
      future_body_proxy_(void *storage): future_body(storage) {}
    };
    
    template<typename ...T>
    struct future_body_proxy final: future_body_proxy_ {
      future_body_proxy(void *storage): future_body_proxy_(storage) {}
      
      void leave_active(future_header_dependent *hdr) {
        if(0 != hdr->decref(1)) { // dependent becoming ready loses ref
          future_header *result = this->link_.dep; // link.dep replaced with result in dependency's enter_ready()
          
          // discard the body. no destructor needed since future_body_proxy<T...>
          // is trivially destructible and we dont want to decref the proxied pointer.
          operator delete(this->storage_);
          
          hdr->enter_ready(result);
        }
        else { // died early
          void *storage = this->storage_;
          future_header_ops_general::template dropref<T...>(this->link_.dep);
          this->~future_body_proxy();
          operator delete(storage);
          delete hdr;
        }
      }
    };
    
    ////////////////////////////////////////////////////////////////////
    // future_header_result<T...>: Header containing the result values
    
    template<typename ...T>
    struct future_header_result {
      UPCXXI_OPNEW_AS_STD
      
      future_header base_header;
      
      static constexpr int status_results_yes = future_header::status_active + 1;
      static constexpr int status_results_no = future_header::status_active + 2;
      
      using results_t = std::tuple<T...>;
      using results_raw_t = typename std::aligned_storage<sizeof(results_t), alignof(results_t)>::type;
      results_raw_t results_raw_;
      
    public:
      future_header_result():
        base_header{
          /*ref_n_*/1,
          /*status_*/status_results_no,
          /*sucs_head_*/nullptr,
          {/*result_*/&this->base_header}
        } {
      }
      
      template<typename ...U>
      future_header_result(bool not_ready, std::tuple<U...> &&values):
        base_header{
          /*ref_n_*/1,
          /*status_*/not_ready ? status_results_yes : future_header::status_ready,
          /*sucs_head_*/nullptr,
          {/*result_*/&this->base_header}
        } {
        ::new(&results_raw_) results_t(std::move(values));
      }
    
    public:
      // static_cast `hdr` to future_header_result<T...> and retrieve
      // results tuple.
      static std::tuple<T...>& results_of(future_header *hdr) {
        return *reinterpret_cast<std::tuple<T...>*>(
          reinterpret_cast<std::uintptr_t>(
            &reinterpret_cast<future_header_result<T...>*>(hdr)->results_raw_
          ) + 0 // silences bogus strict-aliasing warning with some GCC's
        );
      }
      
      void readify() {
        UPCXX_ASSERT(this->base_header.status_ == status_results_yes);
        this->base_header.enter_ready(&this->base_header);
      }
      
      static void readify(future_header *hdr) {
        UPCXX_ASSERT(hdr->status_ == status_results_yes);
        hdr->enter_ready(hdr);
      }

      bool results_constructible() const {
        return this->base_header.status_ == status_results_no;
      }
      
      template<typename ...U>
      void construct_results(U &&...values) {
        UPCXX_ASSERT(this->base_header.status_ == status_results_no);
        ::new(&this->results_raw_) std::tuple<T...>(std::forward<U>(values)...);
        this->base_header.status_ = status_results_yes;
      }
      
      template<typename ...U>
      void construct_results(std::tuple<U...> &&values) {
        UPCXX_ASSERT(this->base_header.status_ == status_results_no);
        ::new(&this->results_raw_) std::tuple<T...>(std::move(values));
        this->base_header.status_ = status_results_yes;
      }
      
      template<typename ...U>
      void reconstruct_results(std::tuple<U...> &&values) {
        UPCXX_ASSERT(this->base_header.status_ != status_results_no);
        results_of(&this->base_header).~results_t();
        ::new(&this->results_raw_) std::tuple<T...>(std::move(values));
        this->base_header.status_ = status_results_yes;
      }

      static constexpr bool is_trivially_deletable = detail::trait_forall<std::is_trivially_destructible, T...>::value;

      void delete_me_ready() {
        results_of(&this->base_header).~results_t();
        operator delete(this);
      }
      
      void delete_me() {
        if(this->base_header.status_ == status_results_yes ||
           this->base_header.status_ == future_header::status_ready) {
          results_of(&this->base_header).~results_t();
        }
        operator delete(this);
      }
      
      // Override refcount arithmetic to take advantage of the fact that we
      // don't have any value-carrying futures statically allocated.
      future_header_result* incref(int n) {
        this->base_header.ref_n_ += n;
        return this;
      }
      int decref(int n) {
        return (this->base_header.ref_n_ -= n);
      }
    };
    
    template<>
    struct future_header_result<> {
      UPCXXI_OPNEW_AS_STD

      static const future_header the_always_slow;

      static constexpr future_header* always() {
        #if UPCXXI_ISSUE_485_SLOW_THE_ALWAYS
          return const_cast<future_header*>(&the_always_slow);
        #else
          return const_cast<future_header*>(
            static_cast<const future_header*>(
              &future_header_always1<>::the_always_fast
            )
          );
        #endif
      }
      
      enum {
        status_not_ready = future_header::status_active + 1
      };
      
      future_header base_header;
      
    public:  
      future_header_result():
        base_header{
          /*ref_n_*/1,
          /*status_*/status_not_ready,
          /*sucs_head_*/nullptr,
          {/*result_*/&this->base_header}
        } {
      }
      
      future_header_result(bool not_ready, std::tuple<>):
        base_header{
          /*ref_n_*/1,
          /*status_*/not_ready ? status_not_ready : future_header::status_ready,
          /*sucs_head_*/nullptr,
          {/*result_*/&this->base_header}
        } {
      }
      
      static std::tuple<> results_of(future_header *hdr) {
        return std::tuple<>{};
      }
      
      void readify() {
        UPCXX_ASSERT(this->base_header.status_ == status_not_ready);
        this->base_header.enter_ready(&this->base_header);
      }
      
      bool results_constructible() const {
        return true;
      }

      void construct_results() {}
      void construct_results(std::tuple<>) {}
      void reconstruct_results(std::tuple<>) {}
      
      static constexpr bool is_trivially_deletable = true;
      
      void delete_me_ready() { operator delete(this); }
      void delete_me() { operator delete(this); }
      
      // Inherit generic header refcount ops
      future_header_result* incref(int n) {
        this->base_header.incref(n);
        return this;
      }
      int decref(int n) {
        return this->base_header.decref(n);
      }
    };
    
    
    ////////////////////////////////////////////////////////////////////
    // future_header_promise
    
    // future_header_promise's are viable lpc's, but they need additional info
    // stashed in their vtable, hence this derived vtable type. When executed
    // as an lpc, a promise will fulfill all deferred decrements and drop a
    // reference (since sitting in a lpc queue should hold a reference to keep
    // it alive).
    struct promise_vtable: lpc_vtable {
      // this byte distance between the address of the `promise_meta` and the
      // encompassing `future_header`.
      std::ptrdiff_t meta_offset_from_header;
      
      constexpr promise_vtable(
          std::ptrdiff_t meta_offset_from_header,
          void(*fulfill_deferred_and_drop)(lpc_base*)
        ):
        lpc_vtable{/*execute_and_delete=*/fulfill_deferred_and_drop},
        meta_offset_from_header(meta_offset_from_header) {
      }
      
      // The two possible lpc actions. promises where the T... are all trivially
      // destructible can share the same action since the deallocation via
      // `operator delete` does the (nop) destruction.
      static void fulfill_deferred_and_drop_trivial(lpc_base*);
      
      template<typename ...T>
      static void fulfill_deferred_and_drop_nontrivial(lpc_base*);
    };
    
    // Promise specific state.
    struct promise_meta {
      // We derive from `lpc_base` but have to use "first member of standard
      // layout" instead of proper inheritance because we need this type to
      // be standard layout so that the containing header is also standard layout
      // so we can ultimately use `offsetof` to know the offset of this type
      // in the header.
      lpc_base base;
      
      // The dependency counter. 1 point for the result values, the rest for
      // the anonymous dependencies.
      std::intptr_t countdown = 1;
      
      // Counts fulfilled dependencies which are to be applied when this promise
      // is executed as an lpc. This field is zero if and only if this promise
      // is not already linked in as an lpc somewhere.
      std::intptr_t deferred_decrements = 0;

      #if UPCXXI_ASSERT_ENABLED
        // points to queue holding this as lpc so that we can assert promise
        // isn't being registered in multiple intrusive queues simultaneously
        void *deferred_queue = nullptr;
      #endif
      
      promise_meta(promise_vtable const *vtbl) {
        base.vtbl = vtbl;
      } 
    };
    
    // The future header of a promise.
    template<typename ...T>
    struct future_header_promise {
      UPCXXI_OPNEW_AS_STD
      
      // We "inherit" from future_header_result<T...> use "first member of standard
      // layout" since real inheritance would break standard layout.
      future_header_result<T...> base_header_result;
      
      // Promise specific state data.
      promise_meta pro_meta;
      
      future_header_promise();
      
      static constexpr bool is_trivially_deletable = future_header_result<T...>::is_trivially_deletable;
      
      // Override refcount arithmetic with more efficient form since we
      // know `future_header_promise`'s are never statically allocated.
      future_header_promise* incref(int n) {
        this->base_header_result.base_header.ref_n_ += n;
        return this;
      }
      int decref(int n) {
        return (this->base_header_result.base_header.ref_n_ -= n);
      }

      void dropref() {
        future_header_ops_promise::template dropref<T...>(&this->base_header_result.base_header, std::false_type());
      }
      
      void fulfill(std::intptr_t n) {
        UPCXX_ASSERT(this->pro_meta.countdown > 0, "Attempted to fulfill an already ready promise.");
        UPCXX_ASSERT(this->pro_meta.countdown - n >= 0, "Attempted to over-fulfill a promise to a negative state.");
        
        this->pro_meta.countdown -= n;
        if(0 == this->pro_meta.countdown)
          this->base_header_result.readify();
      }
    };
    
    // This builds the promise_vtable corresponding to future_header_promise<T...>.
  
    template<typename ...T>
    struct the_promise_vtable {
      static constexpr promise_vtable vtbl{
        /*meta_offset_from_header*/
        offsetof(future_header_promise<T...>, pro_meta),
        /*fulfill_deferred_and_drop*/
        future_header_promise<T...>::is_trivially_deletable
          ? promise_vtable::fulfill_deferred_and_drop_trivial
          : promise_vtable::fulfill_deferred_and_drop_nontrivial<T...>
      };
    };
    
    template<typename ...T>
    constexpr promise_vtable the_promise_vtable<T...>::vtbl;
    
    #if UPCXXI_PROMISE_VTABLE_HACK
    // This empty-parameter specialization is redundant, the general <T...> case
    // redues to something functionally equivalent. Unfortunately, the PGI linker
    // is not initializing the `execute_and_delete` fnptr of vtbl correctly.
    // By specializing the empty case, the linker treats it differently (not
    // as weak definition) and compile-time initialization looks good again.
    template<>
    struct the_promise_vtable<> {
      // This can't be constexpr because then we would run into link issues when mixing C++17 apps with pre-17 upcxx builds
      static const promise_vtable vtbl;
    };
    #endif

    template<typename ...T>
    future_header_promise<T...>::future_header_promise():
      base_header_result(),
      pro_meta(&the_promise_vtable<T...>::vtbl) {
    }
    
    inline void promise_vtable::fulfill_deferred_and_drop_trivial(lpc_base *m) {
      promise_meta *meta = reinterpret_cast<promise_meta*>(m);
      promise_vtable const *vtbl = static_cast<promise_vtable const*>(meta->base.vtbl);
      auto *hdr = (future_header*)((char*)meta - vtbl->meta_offset_from_header);
      meta->countdown -= meta->deferred_decrements;
      meta->deferred_decrements = 0;
      
      if(0 == meta->countdown) {
        // just like future_header_result::readify()
        UPCXX_ASSERT(hdr->status_ == future_header::status_active + 1);
        hdr->enter_ready(hdr);
      }
      
      if(0 == hdr->decref(1))
        future_header_promise<>::operator delete(hdr);
    }
    
    template<typename ...T>
    void promise_vtable::fulfill_deferred_and_drop_nontrivial(lpc_base *m) {
      promise_meta *meta = reinterpret_cast<promise_meta*>(m);
      auto *hdr = (future_header_promise<T...>*)((char*)meta - offsetof(future_header_promise<T...>, pro_meta));
      meta->countdown -= meta->deferred_decrements;
      meta->deferred_decrements = 0;
      
      if(0 == meta->countdown)
        hdr->base_header_result.readify();
      
      future_header_ops_promise::template dropref<T...>(&hdr->base_header_result.base_header, std::false_type());
    }
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_general implementation
    
    template<typename ...T>
    void future_header_ops_general::incref(future_header *hdr) {
      hdr->incref(1);
    }
    
    template<typename ...T, bool maybe_nil>
    void future_header_ops_general::dropref(future_header *hdr, std::integral_constant<bool,maybe_nil>) {
      if(0 == hdr->decref(1))
        delete1<T...>(hdr);
    }
    
    template<typename ...T>
    void future_header_ops_general::delete1(future_header *hdr) {
      // Common case is deleting a ready future.
      if(hdr->status_ == future_header::status_ready) {
        future_header *result = hdr->result_;
        
        if(result == hdr)
          reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me_ready();
        else {
          // Drop ref to our result.
          future_header_ops_result_ready::dropref<T...>(result, std::false_type());
          // Since we're ready we have no body, just delete the header.
          delete static_cast<future_header_dependent*>(hdr);
        }
      }
      // Future dying prematurely.
      else {
        UPCXX_ASSERT(hdr->result_ == hdr);
        // Not ready but is its own result, must be a promise.
        // Don't need to cast to `future_header_promise` since `delete_me` covers this.
        reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me();
      }
    }

    ////////////////////////////////////////////////////////////////////
    // future_header_ops_result implementation
    
    template<typename ...T>
    void future_header_ops_result::incref(future_header *hdr) {
      reinterpret_cast<future_header_result<T...>*>(hdr)->incref(1);
    }
    
    template<typename ...T, bool maybe_nil>
    void future_header_ops_result::dropref(future_header *hdr, std::integral_constant<bool,maybe_nil>) {
      if(0 == hdr->decref(1))
        reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me();
    }
    
    template<typename ...T>
    void future_header_ops_result::delete1(future_header *hdr) {
      reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me();
    } 
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_result_ready implementation
    
    template<typename ...T>
    void future_header_ops_result_ready::incref(future_header *hdr) {
      reinterpret_cast<future_header_result<T...>*>(hdr)->incref(1);
    }
    
    template<typename ...T, bool maybe_nil>
    void future_header_ops_result_ready::dropref(future_header *hdr, std::integral_constant<bool,maybe_nil>) {
      if(0 == hdr->decref(1))
        reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me_ready();
    }
    
    template<typename ...T>
    void future_header_ops_result_ready::delete1(future_header *hdr) {
      reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me_ready();
    } 
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_promise implementation
    
    template<typename ...T>
    void future_header_ops_promise::incref(future_header *hdr) {
      reinterpret_cast<future_header_promise<T...>*>(hdr)->incref(1);
    }
    
    template<typename ...T, bool maybe_nil>
    void future_header_ops_promise::dropref(future_header *hdr, std::integral_constant<bool,maybe_nil>) {
      auto *pro = reinterpret_cast<future_header_promise<T...>*>(hdr);
      
      if((maybe_nil ? hdr != &future_header_nil::the_nil : true) && 0 == pro->decref(1))
        pro->base_header_result.delete_me();
    }
    
    template<typename ...T>
    void future_header_ops_promise::delete1(future_header *hdr) {
      reinterpret_cast<future_header_result<T...>*>(hdr)->delete_me();
    } 
    
    ////////////////////////////////////////////////////////////////////
    // future_header_ops_dependent implementation
    
    template<typename ...T>
    void future_header_ops_dependent::incref(future_header *hdr) {
      static_cast<future_header_dependent*>(hdr)->incref(1);
    }
    
    template<typename ...T, bool maybe_nil>
    void future_header_ops_dependent::dropref(future_header *hdr, std::integral_constant<bool,maybe_nil>) {
      future_header_dependent *dep = static_cast<future_header_dependent*>(hdr);
      
      if((maybe_nil ? hdr != &future_header_nil::the_nil : true) && 0 == dep->decref(1))
        delete1<T...>(hdr);
    }
    
    template<typename ...T>
    void future_header_ops_dependent::delete1(future_header *hdr) {
      // Dependent's can't die prematurely
      UPCXX_ASSERT(hdr->status_ == future_header::status_ready);
      
      future_header *result = hdr->result_;
      // Drop ref to our result.
      future_header_ops_result_ready::dropref<T...>(result, /*maybe_nil=*/std::false_type());
      // Since we're ready we have no body, just delete the header.
      delete static_cast<future_header_dependent*>(hdr);
    }
    
    ////////////////////////////////////////////////////////////////////
    
    inline future_header* future_header::drop_for_result(future_header *a) {
      future_header *b = a->result_;
      
      int a_refs = a->ref_n_;
      int a_unit = a_refs < 0 || a == b ? 0 : 1;
      
      int b_refs = b->ref_n_;
      int b_unit = b_refs < 0 || a == b ? 0 : 1;
      
      a_refs -= a_unit;
      b_refs += b_unit;
      
      if(0 == a_refs)
        // deleting a so b loses that reference
        b_refs -= b_unit;
      
      // write back a->ref_n_
      if(a_unit == 1) a->ref_n_ = a_refs;
      // write back b->ref_n_
      if(b_unit == 1) b->ref_n_ = b_refs;
      
      if(0 == a_refs) {
        // must be a dependent since if it were a result it couldn't have zero refs
        delete static_cast<future_header_dependent*>(a);
      }
      
      return b;
    }
    
    inline future_header* future_header::drop_for_proxied(future_header *a) {
      future_body_proxy_ *a_body = static_cast<future_body_proxy_*>(a->body_);
      future_header *b = a_body->link_.dep;
      
      int a_refs = a->ref_n_;
      int b_refs = b->ref_n_;
      int b_unit = b_refs < 0 ? 0 : 1;
      
      a_refs -= 1;
      b_refs += b_unit;
      
      if(0 == a_refs)
        // deleting a so b loses that reference
        b_refs -= b_unit;
      
      // write back a->ref_n_
      a->ref_n_ = a_refs;
      // write back b->ref_n_
      if(b_unit == 1) b->ref_n_ = b_refs;
      
      if(0 == a_refs) {
        if(a->status_ == status_proxying)
          a_body->link_.unlink();
        // proxy bodies are trivially destructible
        future_body::operator delete(a_body->storage_);
        // proxying headers are dependents
        delete static_cast<future_header_dependent*>(a);
      }
      
      return b;
    }
  } // namespace detail
}

#endif
