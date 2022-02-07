#ifndef _0a792abf_0420_42b8_91a0_67a4b337f136
#define _0a792abf_0420_42b8_91a0_67a4b337f136

#include <upcxx/backend_fwd.hpp>
#include <upcxx/concurrency.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/segment_allocator.hpp>

namespace upcxx {
  namespace detail {
    struct device_allocator_base {
      int heap_idx_; // -1 = inactive
      detail::segment_allocator seg_;

    public:
      device_allocator_base(int heap_idx, detail::segment_allocator seg):
        heap_idx_(heap_idx),
        seg_(std::move(seg)) {
        if (heap_idx_ >= 0) {
          backend::heap_state *hs = backend::heap_state::get(heap_idx_);
          UPCXX_ASSERT(!hs->alloc_base, 
                       "A given device object may only be used to create one device_allocator");
          hs->alloc_base = this; // register
        }
      }
      device_allocator_base(device_allocator_base const&) = delete;
      device_allocator_base(device_allocator_base&& other) : 
        heap_idx_(other.heap_idx_), seg_(std::move(other.seg_)) {
        if (heap_idx_ >= 0) {
          backend::heap_state *hs = backend::heap_state::get(heap_idx_);
          UPCXX_ASSERT(hs->alloc_base == &other);
          hs->alloc_base = this; // update registration
          other.heap_idx_ = -1; // deactivate
        }
      }

      bool is_active() const { return heap_idx_ >= 0; }
    };

    // specialized per device type
    template<typename Device>
    struct device_allocator_core; /*: device_allocator_base {

      device_allocator_core(); // non-collective default constructor

      // collective constructor
      // dev must be provided but might be inactive
      // base==null means not provided
      device_allocator_core(Device &dev, typename Device::pointer<void> base, std::size_t size);

      // move constructor must be provided (need not be default)
      device_allocator_core(device_allocator_core&&) = default;

      void destroy();
    };*/
  }
  
  template<typename Device>
  class device_allocator: protected detail::device_allocator_core<Device> {
    detail::par_mutex lock_;
    
  public:
    using device_type = Device;
    using detail::device_allocator_base::is_active;

    device_allocator():
      detail::device_allocator_core<Device>() { }

    device_allocator(Device &dev, typename Device::template pointer<void> base, std::size_t size):
      detail::device_allocator_core<Device>(
        (UPCXXI_ASSERT_INIT(),
         UPCXXI_ASSERT_ALWAYS_MASTER(),
         UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ(),
         dev), base, size) { }

    device_allocator(Device &dev, std::size_t size):
      detail::device_allocator_core<Device>(
        (UPCXXI_ASSERT_INIT(),
         UPCXXI_ASSERT_ALWAYS_MASTER(),
         UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ(),
         dev), Device::template null_pointer<void>(), size) { }
    
    device_allocator(device_allocator &&that):
      // base class move ctor
      detail::device_allocator_core<Device>::device_allocator_core(
        static_cast<detail::device_allocator_core<Device>&&>(
          ( UPCXXI_ASSERT_MASTER_HELD_IFSEQ(), // required to ensure thread-safety wrt allocate
            // use comma operator to create a temporary lock_guard surrounding
            // the invocation of our base class's move ctor
            std::lock_guard<detail::par_mutex>(that.lock_), 
            that)
        )
      ) {
    }

    template<typename T>
    UPCXXI_NODISCARD
    global_ptr<T,Device::kind> allocate(std::size_t n=1,
                                        std::size_t align = Device::template default_alignment<T>()) {
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT(this->is_active(), "device_allocator::allocate() invoked on an inactive device.");
      UPCXXI_ASSERT_MASTER_HELD_IFSEQ();
      lock_.lock();
      void *ptr = this->seg_.allocate(
          n*sizeof(T),
          std::max<std::size_t>(align, Device::min_alignment())
        );
      lock_.unlock();
      
      if(ptr == nullptr)
        return global_ptr<T,Device::kind>(nullptr);
      else
        return global_ptr<T,Device::kind>(
          detail::internal_only(),
          upcxx::rank_me(),
          (T*)ptr,
          this->heap_idx_
        );
    }

    template<typename T>
    void deallocate(global_ptr<T,Device::kind> p) {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(p);
      if(p) {
        UPCXX_ASSERT(this->is_active(), "device_allocator::daallocate() invoked on an inactive device.");
        UPCXX_ASSERT(p.UPCXXI_INTERNAL_ONLY(heap_idx_) == this->heap_idx_ &&
                     p.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me());
        UPCXXI_ASSERT_MASTER_HELD_IFSEQ();
        lock_.lock();
        this->seg_.deallocate(p.UPCXXI_INTERNAL_ONLY(raw_ptr_));
        lock_.unlock();
      }
    }

    template<typename T>
    UPCXXI_ATTRIB_PURE
    global_ptr<T,Device::kind> to_global_ptr(typename Device::template pointer<T> p) const {
      UPCXXI_ASSERT_INIT();
      if (p == Device::template null_pointer<T>()) return global_ptr<T,Device::kind>();

      UPCXX_ASSERT(this->is_active(), "device_allocator::to_global_ptr() invoked on an inactive device.");
      return global_ptr<T,Device::kind>(
        detail::internal_only(),
        upcxx::rank_me(),
        p,
        this->heap_idx_
      );
    }

    #if 0 // removed from spec
    template<typename T>
    UPCXXI_ATTRIB_PURE
    global_ptr<T,Device::kind> try_global_ptr(typename Device::template pointer<T> p) const {
      UPCXXI_ASSERT_INIT();
      if (p == Device::template null_pointer<T>()) return global_ptr<T,Device::kind>();

      UPCXX_ASSERT(this->is_active(), "device_allocator::try_global_ptr() invoked on an inactive device.");
      return this->seg_.in_segment((void*)p)
        ? global_ptr<T,Device::kind>(
          detail::internal_only(),
          upcxx::rank_me(),
          p,
          this->heap_idx_
        )
        : global_ptr<T,Device::kind>(nullptr);
    }
    #endif
    
    template<typename T>
    UPCXXI_ATTRIB_PURE
    static typename Device::id_type device_id(global_ptr<T,Device::kind> gp) {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(gp);
      UPCXX_ASSERT(gp.is_null() || gp.where() == upcxx::rank_me());
      if (!gp) return Device::invalid_device_id;
      else {
        #if UPCXXI_ASSERT_ENABLED // issue 468: avoid unused-variable warning
          backend::heap_state *hs = backend::heap_state::get(gp.UPCXXI_INTERNAL_ONLY(heap_idx_));
        #endif
        UPCXX_ASSERT(hs->alloc_base && hs->alloc_base->is_active(), 
          "device_allocator::device_id() invoked with a pointer from an inactive device.");
        return Device::template heap_idx_to_device_id<Device>(gp.UPCXXI_INTERNAL_ONLY(heap_idx_));
      }
    }
    
    template<typename T>
    UPCXXI_ATTRIB_PURE
    static typename Device::template pointer<T> local(global_ptr<T,Device::kind> gp) {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(gp);
      if (!gp) return Device::template null_pointer<T>();
      UPCXX_ASSERT(gp.where() == upcxx::rank_me());
      #if UPCXXI_ASSERT_ENABLED // issue 468: avoid unused-variable warning
        backend::heap_state *hs = backend::heap_state::get(gp.UPCXXI_INTERNAL_ONLY(heap_idx_));
      #endif
      UPCXX_ASSERT(hs->alloc_base && hs->alloc_base->is_active(), 
        "device_allocator::device_id() invoked with a pointer from an inactive device.");
      return gp.UPCXXI_INTERNAL_ONLY(raw_ptr_);
    }
  };
}

#endif
