#ifndef _dbba940a_54c7_48a3_a31f_66676be3cca4
#define _dbba940a_54c7_48a3_a31f_66676be3cca4

#include <upcxx/upcxx_config.hpp>

#include <atomic>
#include <cstdint>
#include <limits>

#if UPCXX_MPSC_QUEUE_BIGLOCK
  #include <mutex>
#endif

#define UINTPTR_OF(p) reinterpret_cast<std::uintptr_t>(p)

namespace upcxx {
  namespace detail {
    ////////////////////////////////////////////////////////////////////////////
    // `detail::intru_queue`: intrusive queue of heterogeneous elements derived
    // from some base type T. The queue is parametric in its thread-safety
    // level. The type T must have a field of type `intru_queue_intruder<T>`.
    
    enum class intru_queue_safety {
      // This queue has no thread-safety properties = very fast
      none,
      // Queue can be `enqueue`'d to by multiple threads concurrently while
      // at most one thread can be `dequeue`'ing or `burst`'ing.
      mpsc
    };
    
    template<typename T>
    struct intru_queue_intruder {
       std::atomic<T*> p;
       
       intru_queue_intruder(): p(reinterpret_cast<T*>(0x1)) {}
       ~intru_queue_intruder() {}

       bool is_enqueued() const {
         return p.load(std::memory_order_relaxed) != reinterpret_cast<T*>(0x1);
       }
    };
    
    template<typename T,
             intru_queue_safety safety,
             intru_queue_intruder<T> T::*intruder>
    class intru_queue;
    
    ////////////////////////////////////////////////////////////////////////////
    // intru_queue<..., safety=none> specialization:
    
    template<typename T, intru_queue_intruder<T> T::*next>
    class intru_queue<T, intru_queue_safety::none, next> {
      T *head_;
      std::uintptr_t tailp_xor_head_;
      
    public:
      constexpr intru_queue():
        head_(),
        tailp_xor_head_() {
      }
      
      intru_queue(intru_queue const&) = delete;
      intru_queue(intru_queue &&that);
      
      constexpr bool empty() const {
        return this->head_ == nullptr;
      }

      T* peek() const { return this->head_; }
      
      void enqueue(T *x);
      T* dequeue();
      
      template<typename Fn>
      int burst(Fn &&fn);
      template<typename Fn>
      int burst(int max_n, Fn &&fn);
    
    private:
      template<typename Fn>
      int burst_something(Fn &&fn, T *head);
      template<typename Fn>
      int burst_something(int max_n, Fn &&fn, T *head);
    };
    
    ////////////////////////////////////////////////////////////////////////////
    
    template<typename T, intru_queue_intruder<T> T::*next>
    intru_queue<T, intru_queue_safety::none, next>::intru_queue(intru_queue &&that) {
      this->head_ = that.head_;
      if(that.tailp_xor_head_ == 0)
        this->tailp_xor_head_ = 0;
      else
        this->tailp_xor_head_ = that.tailp_xor_head_ ^ UINTPTR_OF(&that.head_) ^ UINTPTR_OF(&this->head_);
      
      that.head_ = nullptr;
      that.tailp_xor_head_ = 0;
    }
    
    template<typename T, intru_queue_intruder<T> T::*next>
    inline void intru_queue<T,intru_queue_safety::none, next>::enqueue(T *x) {
      T **tailp = reinterpret_cast<T**>(UINTPTR_OF(&this->head_) ^ this->tailp_xor_head_);
      *tailp = x;
      this->tailp_xor_head_ = UINTPTR_OF(&(x->*next)) ^ UINTPTR_OF(&this->head_);
      (x->*next).p.store(nullptr, std::memory_order_relaxed);
    }

    template<typename T, intru_queue_intruder<T> T::*next>
    inline T* intru_queue<T, intru_queue_safety::none, next>::dequeue() {
      T *ans = this->head_;
      this->head_ = (ans->*next).p.load(std::memory_order_relaxed);
      if(this->head_ == nullptr)
        this->tailp_xor_head_ = 0;
      return ans;
    }
    
    template<typename T, intru_queue_intruder<T> T::*next>
    template<typename Fn>
    inline int intru_queue<T, intru_queue_safety::none, next>::burst(Fn &&fn) {
      if(this->head_ == nullptr)
        return 0;
      else
        return this->burst_something(static_cast<Fn&&>(fn), this->head_);
    }
  
    template<typename T, intru_queue_intruder<T> T::*next>
    template<typename Fn>
    int intru_queue<T, intru_queue_safety::none, next>::burst_something(Fn &&fn, T *head1) {
      this->head_ = nullptr;
      this->tailp_xor_head_ = 0;
      
      int exec_n = 0;
      
      do {
        T *head1_next = (head1->*next).p.load(std::memory_order_relaxed);
        fn(head1);
        head1 = head1_next;
        exec_n += 1;
      } while(head1 != nullptr);
      
      return exec_n;
    }
    
    template<typename T, intru_queue_intruder<T> T::*next>
    template<typename Fn>
    inline int intru_queue<T, intru_queue_safety::none, next>::burst(int max_n, Fn &&fn) {
      T *head = this->head_;
      if(max_n == 0 || head == nullptr)
        return 0;
      else
        return this->burst_something(max_n, static_cast<Fn&&>(fn), head);
    }
  
    template<typename T, intru_queue_intruder<T> T::*next>
    template<typename Fn>
    int intru_queue<T, intru_queue_safety::none, next>::burst_something(int max_n, Fn &&fn, T *head1) {
      T **tailp1 = reinterpret_cast<T**>(UINTPTR_OF(&this->head_) ^ this->tailp_xor_head_);
      
      this->head_ = nullptr;
      this->tailp_xor_head_ = 0;
      
      int n = max_n;
      
      do {
        T *head1_next = (head1->*next).p.load(std::memory_order_relaxed);
        fn(head1);
        head1 = head1_next;
        n -= 1;
      } while(head1 != nullptr && n != 0);
      
      if(head1 != nullptr) {
        *tailp1 = this->head_;
        if(this->head_ == nullptr)
          this->tailp_xor_head_ = UINTPTR_OF(tailp1) ^ UINTPTR_OF(&this->head_);
        this->head_ = head1;
      }
      
      return max_n - n;
    }
    
    ////////////////////////////////////////////////////////////////////////////
    // intru_queue<..., safety=mpsc> specialization:
    
    #if UPCXX_MPSC_QUEUE_ATOMIC
      template<typename T, intru_queue_intruder<T> T::*next>
      class intru_queue<T, intru_queue_safety::mpsc, next> {
        std::atomic<T*> head_;
        std::atomic<std::uintptr_t> tailp_xor_head_;
        
      private:
        constexpr std::atomic<T*>* decode_tailp(std::uintptr_t u) const {
          return reinterpret_cast<std::atomic<T*>*>(u ^ UINTPTR_OF(&head_));
        }
        constexpr std::uintptr_t encode_tailp(std::atomic<T*> *val) const {
          return UINTPTR_OF(val) ^ UINTPTR_OF(&head_);
        }

      public:
        constexpr intru_queue():
          head_(),
          tailp_xor_head_() {
        }
        
        intru_queue(intru_queue const&) = delete;
        intru_queue(intru_queue &&that) = delete;
        
        constexpr bool empty() const {
          return this->head_.load(std::memory_order_relaxed) == nullptr;
        }
        
        T* peek() const {
          return this->head_.load(std::memory_order_relaxed);
        }

        void enqueue(T *x);
        T* dequeue();
        
        template<typename Fn>
        int burst(Fn &&fn);
        template<typename Fn>
        int burst(int max_n, Fn &&fn);
      
      private:
        template<typename Fn>
        int burst_something(int max_n, Fn &&fn, T *head);
      };
      
      ////////////////////////////////////////////////////////////////////////////
      
      template<typename T, intru_queue_intruder<T> T::*next>
      inline void intru_queue<T, intru_queue_safety::mpsc, next>::enqueue(T *x) {
        (x->*next).p.store(nullptr, std::memory_order_relaxed);
        
        std::atomic<T*> *got = this->decode_tailp(
                                 this->tailp_xor_head_.exchange(
                                   this->encode_tailp(&(x->*next).p)
                                 )
                               );
        got->store(x, std::memory_order_relaxed);
      }
      
      template<typename T, intru_queue_intruder<T> T::*next>
      template<typename Fn>
      inline int intru_queue<T, intru_queue_safety::mpsc, next>::burst(Fn &&fn) {
        return this->burst(std::numeric_limits<int>::min(), static_cast<Fn&&>(fn));
      }
      
      template<typename T, intru_queue_intruder<T> T::*next>
      template<typename Fn>
      inline int intru_queue<T, intru_queue_safety::mpsc, next>::burst(int max_n, Fn &&fn) {
        T *head = this->head_.load(std::memory_order_relaxed);
        
        if(head == nullptr)
          return 0;
        
        return this->burst_something(max_n, static_cast<Fn&&>(fn), head);
      }
      
      template<typename T, intru_queue_intruder<T> T::*next>
      T* intru_queue<T, intru_queue_safety::mpsc, next>::dequeue() {
        T *head = this->head_.load(std::memory_order_relaxed);
        T *head_next = (head->*next).p.load(std::memory_order_relaxed);

        this->head_.store(head_next, std::memory_order_relaxed);

        if(head_next == nullptr) {
          std::uintptr_t expected = this->encode_tailp(&(head->*next).p);
          std::uintptr_t desired = this->encode_tailp(&this->head_);
          if(!this->tailp_xor_head_.compare_exchange_weak(expected, desired)) {
            do {
              // TODO: pause instruction here
              head_next = (head->*next).p.load(std::memory_order_relaxed);
            } while(head_next == nullptr);

            this->head_.store(head_next, std::memory_order_relaxed);
          }
        }
        
        return head;
      }

      template<typename T, intru_queue_intruder<T> T::*next>
      template<typename Fn>
      int __attribute__((noinline))
      intru_queue<T, intru_queue_safety::mpsc, next>::burst_something(int max_n, Fn &&fn, T *head) {
        int exec_n = 0;
        T *p = head;
        
        // Execute as many elements as we can until we reach one that looks
        // like it may be the last in the list.
        while(true) {
          T *p_next = (p->*next).p.load(std::memory_order_relaxed);
          if(p_next == nullptr)
            break; // Element has no `next`, so it looks like the last.
          
          fn(p);
          p = p_next;
          
          if(max_n == ++exec_n) {
            this->head_.store(p, std::memory_order_relaxed);
            return exec_n;
          }
        }
        
        // We executed as many as we could without doing an `exchange`.
        // If that was at least one then we quit and hope next time we burst
        // there will be even more so we can kick the can of doing the heavy
        // atomic once again.
        if(exec_n != 0) {
          this->head_.store(p, std::memory_order_relaxed);
          return exec_n;
        }
        
        // So it *looks* like there is exactly one element in the list (though
        // more may be on the way as we speak). Since it isn't safe to execute
        // an element without knowing its successor first (thanks to
        // execute_and_*DELETE*), we reset the list to start at our `head_`
        // pointer. The reset is done with an `exchange`, with the effects:
        //  1) The last element present in the list before reset is returned
        //     to us (actually the address of its `next` field is).
        //  2) All elements added after reset will start at `head_` and so
        //     won't be successors of the last element from 1.
        this->head_.store(nullptr, std::memory_order_relaxed);
        
        std::atomic<T*> *last_next = this->decode_tailp(
                                       this->tailp_xor_head_.exchange(
                                         this->encode_tailp(&this->head_)
                                       )
                                     );
        
        // Process all elements before the last.
        while(&(p->*next).p != last_next) {
          // Get next pointer, and must spin for it. Spin should be of
          // extremely short duration since we know that it's on the way by
          // virtue of this not being the tail element.
          T *p_next = (p->*next).p.load(std::memory_order_relaxed);
          while(p_next == nullptr) {
            // TODO: add pause instruction here
            // asm volatile("pause\n": : :"memory");
            p_next = (p->*next).p.load(std::memory_order_relaxed);
          }
          
          fn(p);
          p = p_next;

          // We have no choice but to ignore the `max_n` budget since we
          // are the only ones who know these elements exist (unless we kept
          // a pointer in our datastructure to stash these elements for
          // consumption in a later burst). Also, it is unlikely that we
          // would blow our budget by much since this list remnant is
          // probably length 1.
          exec_n += 1;
        }

        // And now the last.
        fn(p);
        exec_n += 1;

        return exec_n;
      }
    
    #elif UPCXX_MPSC_QUEUE_BIGLOCK
    
      /* This is the poorly performing but most likely bug-free implementation of
       * a mpsc intru_queue. There is a single global lock, yuck.
       *
       * NOTE: This implementation actually uses one lock PER instantiation of this class.
       * This means we get slightly improved concurrency for queues of different types, 
       * but also means it is NOT safe to type-pun this intru_queue and use it, 
       * even if the T's share an inheritance hierarchy or are structurally equivalent unequal types.
       */
      template<typename T, intru_queue_intruder<T> T::*next>
      class intru_queue<T, intru_queue_safety::mpsc, next> {
        static std::mutex the_lock_;
        
        using unsafe_queue = intru_queue<T, intru_queue_safety::none, next>;
        unsafe_queue q_;
        
      public:
        constexpr intru_queue():
          q_() {
        }
        
        intru_queue(intru_queue const&) = delete;
        intru_queue(intru_queue &&that) = delete;
        
        constexpr bool empty() const {
          return q_.empty();
        }
        
        void enqueue(T *x) {
          std::lock_guard<std::mutex> locked(the_lock_);
          q_.enqueue(x);
        }
        
        template<typename Fn>
        int burst(Fn &&fn) {
          using blob = typename std::aligned_storage<sizeof(unsafe_queue), alignof(unsafe_queue)>::type;
          
          // Use the move constructor to steal from the main into a local temporary
          blob tmpq_blob;
          unsafe_queue *tmpq;
          {
            std::lock_guard<std::mutex> locked(the_lock_);
            tmpq = ::new(&tmpq_blob) unsafe_queue(std::move(q_));
          }
          
          int ans = tmpq->burst(std::forward<Fn>(fn));
          tmpq->~unsafe_queue();
          return ans;
        }
        
        template<typename Fn>
        int burst(int max_n, Fn &&fn) {
          // issue 245: Cannot safely support max_n here with the current strategy, 
          // without grabbing the lock again and re-enqueuing any entries beyond max_n
          // So instead, just ignore max_n and process a snapshot of the entire queue
          return burst(std::forward<Fn>(fn));
        }
      };
      
      template<typename T, intru_queue_intruder<T> T::*next>
      std::mutex intru_queue<T, intru_queue_safety::mpsc, next>::the_lock_;
    
    #else
      #error "Invalid UPCXX_MPSC_QUEUE_xxx."
    #endif
  }
}
#undef UINTPTR_OF
#endif
