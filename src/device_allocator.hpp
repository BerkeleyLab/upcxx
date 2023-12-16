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

      device_allocator_base(int heap_idx=-1, 
                            segment_allocator seg=segment_allocator(nullptr, 0)):
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
        device_allocator_base() {
        *this = std::move(other);
      }
      device_allocator_base& operator=(device_allocator_base&& other) {
        if (&other == this) return *this; // see issue 547
        UPCXX_ASSERT(
          !is_active(),
          "Move assignment is only allowed an an inactive device allocator"
        );
        heap_idx_ = other.heap_idx_;
        seg_ = std::move(other.seg_);
        if (heap_idx_ >= 0) {
          backend::heap_state *hs = backend::heap_state::get(heap_idx_);
          UPCXX_ASSERT(hs->alloc_base == &other);
          hs->alloc_base = this; // update registration
          other.heap_idx_ = -1; // deactivate
        }
        return *this;
      }

      inline bool is_active() const { return heap_idx_ >= 0; }
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

      void release();
    };*/
  }

  // device-independent public abstract base class:
  class heap_allocator {
   private:
    memory_kind kind_;

   protected:
    heap_allocator(detail::internal_only, memory_kind kind) : kind_(kind) {}

    virtual global_ptr<char,memory_kind::any> allocate_raw(std::size_t n, std::size_t align,
                                                           std::size_t sizeof_T, std::size_t alignof_T) = 0;
    virtual void deallocate_raw(global_ptr<char,memory_kind::any>) = 0;

   public:
    memory_kind kind() const { return kind_; }
    virtual ~heap_allocator() {}

    virtual void destroy(upcxx::entry_barrier eb = entry_barrier::user) = 0;

    virtual bool is_active() const = 0;
    virtual std::int64_t segment_size() const = 0;
    virtual std::int64_t segment_used() const = 0;
    
    template<typename T>
    UPCXXI_NODISCARD
    global_ptr<T,memory_kind::any> allocate(std::size_t n=1, std::size_t align = 0) {
      UPCXXI_ASSERT_INIT();
      return reinterpret_pointer_cast<T>(allocate_raw(n, align, sizeof(T), alignof(T)));
    }

    template<typename T, memory_kind K>
    void deallocate(global_ptr<T,K> p) {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(p);
      deallocate_raw(reinterpret_pointer_cast<char>(p));
    }

  };
 
  // device-dependent public concrete class:
  template<typename Device>
  class device_allocator final : 
    public heap_allocator, protected detail::device_allocator_core<Device> {
    detail::par_mutex lock_;
    Device *implicit_device = nullptr;
    
    device_allocator(Device *dev, std::size_t size, typename Device::template pointer<void> base):
      device_allocator(*dev, size, base) { implicit_device = dev; }

  public:
    using device_type = Device;

    static constexpr memory_kind kind = Device::kind;

    // non-collective default constructor for inactive objects
    device_allocator(): heap_allocator(detail::internal_only(), Device::kind),
      detail::device_allocator_core<Device>() { }

    // public collective constructor
    device_allocator(Device &dev, std::size_t size,
                     typename Device::template pointer<void> base = 
                         Device::template null_pointer<void>()):
      heap_allocator(detail::internal_only(), Device::kind),
      detail::device_allocator_core<Device>(
        (UPCXXI_ASSERT_INIT(),
         UPCXXI_ASSERT_ALWAYS_MASTER(),
         UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ(),
         dev), base, size) { }

    UPCXXI_DEPRECATED("Legacy constructor argument ordering, DEPRECATED since 2022.3.0. "
                      "Use device_allocator(device,size,opt_base) instead, or better yet upcxx::make_gpu_allocator()")
    device_allocator(Device &dev, typename Device::template pointer<void> base, std::size_t size):
      device_allocator(dev, size, base) {}

    // internal constructor used by allocator factory function
    device_allocator(detail::internal_only,
                     typename Device::id_type device_id, std::size_t size,
                     typename Device::template pointer<void> base):
      device_allocator(new Device(device_id), size, base) {}

    ~device_allocator() override {
      if(backend::init_count > 0) { // we don't assert on leaks after finalization
        UPCXX_ASSERT_ALWAYS(!is_active(), "An active upcxx::device_allocator<" 
                           << detail::to_string(kind)
                           << "> must have destroy() called before destructor.");
      }
      delete implicit_device;
    }

    device_allocator(device_allocator &&that): device_allocator() {
      *this = std::move(that);
    }

    device_allocator& operator=(device_allocator &&that) {
      if (&that == this) return *this; // see issue 547
      // base class move assign
      heap_allocator::operator=(std::move(that));
      detail::device_allocator_core<Device>::operator=(
        static_cast<detail::device_allocator_core<Device>&&>(
          ( UPCXXI_ASSERT_MASTER_HELD_IFSEQ(), // required to ensure thread-safety wrt allocate
            // use comma operator to create a temporary lock_guard surrounding
            // the invocation of our base class's move ctor
            std::lock_guard<detail::par_mutex>(that.lock_),
            that)
        )
      );
      implicit_device = that.implicit_device;
      that.implicit_device = nullptr;
      return *this;
    }

    void destroy(upcxx::entry_barrier eb = entry_barrier::user) override {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_ALWAYS_MASTER();
      UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
      UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
      if (!is_active()) {
        backend::quiesce(upcxx::world(), eb);
        return;
      }
      backend::heap_state *hs = backend::heap_state::get(this->heap_idx_);
      UPCXX_ASSERT(hs->alloc_base == this);
      UPCXX_ASSERT(hs->device_base);
      UPCXX_ASSERT(hs->device_base->is_active());    
      hs->device_base->destroy(eb);
    }

    bool is_active() const override { return detail::device_allocator_base::is_active(); }

    std::int64_t segment_size() const override { 
      if (!is_active()) return 0;
      std::lock_guard<detail::par_mutex> g(const_cast<device_allocator*>(this)->lock_);
      return this->seg_.segment_size();
    }

    std::int64_t segment_used() const override { 
      if (!is_active()) return 0;
      std::lock_guard<detail::par_mutex> g(const_cast<device_allocator*>(this)->lock_);
      return this->seg_.segment_size() - this->seg_.segment_free();
    }

    template<typename T>
    UPCXXI_NODISCARD
    global_ptr<T,Device::kind> allocate(std::size_t n=1,
                                        std::size_t align = Device::template default_alignment<T>()) {
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT(this->is_active(), "device_allocator::allocate() invoked on an inactive device.");
      UPCXXI_ASSERT_MASTER_HELD_IFSEQ();
      void *ptr = nullptr;
      { std::lock_guard<detail::par_mutex> g(lock_);
        ptr = this->seg_.allocate(
          n*sizeof(T),
          std::max<std::size_t>(align, Device::min_alignment)
        );
      }
      
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
        UPCXX_ASSERT(this->is_active(), "device_allocator::deallocate() invoked on an inactive device.");
        UPCXX_ASSERT(p.UPCXXI_INTERNAL_ONLY(heap_idx_) == this->heap_idx_ &&
                     p.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me());
        UPCXXI_ASSERT_MASTER_HELD_IFSEQ();
        std::lock_guard<detail::par_mutex> g(lock_);
        this->seg_.deallocate(p.UPCXXI_INTERNAL_ONLY(raw_ptr_));
      }
    }
    template<typename T>
    void deallocate(global_ptr<T,memory_kind::any> p) {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(p);
      if (p) {
        UPCXX_ASSERT(p.dynamic_kind() == Device::kind, 
                    "device_allocator::deallocate() invoked with a pointer of the wrong memory kind");
        deallocate(static_kind_cast<Device::kind>(p));
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

    typename Device::id_type device_id() const {
      UPCXXI_ASSERT_INIT();
      if (!is_active()) return Device::invalid_device_id;
      return Device::template heap_idx_to_device_id<Device>(this->heap_idx_);
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

   protected:

    global_ptr<char,memory_kind::any> 
    allocate_raw(std::size_t n, std::size_t align,
                 std::size_t sizeof_T, std::size_t alignof_T) override {
      if (align == 0) { // 0 == default_alignment<T>()
        align = Device::default_alignment_erased(sizeof_T, alignof_T, Device::normal_alignment);
      }
      return allocate<char>(n * sizeof_T, align);
    }

    void deallocate_raw(global_ptr<char,memory_kind::any> p) override {
      deallocate(p);
    }
  }; // device_allocator

} // namespace

#endif
