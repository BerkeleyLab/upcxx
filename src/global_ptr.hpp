#ifndef _bcef2443_cf3b_4148_be6d_be2d24f46848
#define _bcef2443_cf3b_4148_be6d_be2d24f46848

/**
 * global_ptr.hpp
 */
#define UPCXXI_IN_GLOBAL_PTR_HPP

#include <upcxx/backend.hpp>
#include <upcxx/diagnostic.hpp>
#include <upcxx/memory_kind.hpp>

#include <cstddef> // ptrdiff_t
#include <cstdint> // uintptr_t
#include <cstring> // memcpy
#include <iostream> // ostream
#include <type_traits> // is_const, is_volatile

#ifndef UPCXXI_GPTR_CHECK_ENABLED
// -DUPCXXI_GPTR_CHECK_ENABLED=0/1 independently controls gptr checking (default enabled with assertions)
#define UPCXXI_GPTR_CHECK_ENABLED UPCXXI_ASSERT_ENABLED
#endif
#ifndef UPCXXI_GPTR_CHECK_ALIGNMENT
#define UPCXXI_GPTR_CHECK_ALIGNMENT 1 // -DUPCXXI_GPTR_CHECK_ALIGNMENT=0 disables alignment checking
#endif

#if UPCXXI_GPTR_CHECK_ENABLED
#define UPCXXI_GPTR_CHK(p)         ((p).check(true,  __func__, UPCXXI_FUNC))
#define UPCXXI_GPTR_CHK_NONNULL(p) ((p).check(false, __func__, UPCXXI_FUNC))
#else
#define UPCXXI_GPTR_CHK(p)         ((void)0)
#define UPCXXI_GPTR_CHK_NONNULL(p) ((void)0)
#endif

namespace upcxx {
  //////////////////////////////////////////////////////////////////////////////
  // global_ptr
  
  template<typename T, memory_kind Kind = memory_kind::host>
  class global_ptr : public global_ptr<const T, Kind> {
  public:
    using element_type = T;
    using pointer_type = T*;
    #include <upcxx/global_ptr_impl.hpp>

    using base_type = global_ptr<const T, Kind>;

    // allow construction from a pointer-to-non-const
    explicit global_ptr(detail::internal_only, intrank_t rank, T *raw,
                        unsigned int heap_idx = 0, memory_kind dynamic_kind = Kind):
      base_type(detail::internal_only(), rank, raw, heap_idx, dynamic_kind) {
    }

    template <typename U>
    explicit global_ptr(detail::internal_only,
                        const global_ptr<U, Kind> &other, std::ptrdiff_t offset):
      base_type(detail::internal_only(), other, offset) {
    }

    // trivial copy construct/assign (TriviallyCopyable)
    global_ptr(global_ptr const &) = default;
    global_ptr& operator=(global_ptr const &) = default;

    // kind conversion constructor
    template<memory_kind FromKind,
             typename = typename std::enable_if<(Kind != FromKind && Kind == memory_kind::any)>::type>
    global_ptr(global_ptr<const T,FromKind> const &that):
      base_type(that) {
    }

    // null pointer represented with rank 0
    global_ptr(std::nullptr_t nil = nullptr):
      base_type(nil) {
    }

    T* local() const {
      return const_cast<T*>(base_type::local());
    }
  };

  template<typename T, memory_kind Kind>
  class global_ptr<const T, Kind> {
  public:
    using element_type = const T;
    using pointer_type = const T*;
    #include <upcxx/global_ptr_impl.hpp>

    static constexpr memory_kind kind = Kind;

    void check(bool allow_null=true, const char *short_context=nullptr, const char *context=nullptr) const {
        void *this_sanity_check = (void*)this;
        UPCXX_ASSERT_ALWAYS(this_sanity_check, "global_ptr::check() invoked on a null pointer to global_ptr");
        #if UPCXXI_GPTR_CHECK_ALIGNMENT
          constexpr size_t align = detail::align_of<T>();
        #else
          constexpr size_t align = 0;
        #endif
          backend::validate_global_ptr(allow_null, UPCXXI_INTERNAL_ONLY(rank_),
                                       reinterpret_cast<void*>(UPCXXI_INTERNAL_ONLY(raw_ptr_)),
                                       UPCXXI_INTERNAL_ONLY(heap_idx_), UPCXXI_INTERNAL_ONLY(dynamic_kind_),
                                       Kind, align,
                                       detail::typename_of<T>(), 
                                       short_context, context);
    }
    
    explicit global_ptr(detail::internal_only, intrank_t rank, const T *raw,
                        unsigned int heap_idx = 0, memory_kind dynamic_kind = Kind):
      #if UPCXXI_MANY_KINDS
        UPCXXI_INTERNAL_ONLY(heap_idx_)(heap_idx),
        UPCXXI_INTERNAL_ONLY(dynamic_kind_)(dynamic_kind),
      #endif
      UPCXXI_INTERNAL_ONLY(rank_)(rank),
      UPCXXI_INTERNAL_ONLY(raw_ptr_)(const_cast<T*>(raw)) {
      static_assert(std::is_trivially_copyable<global_ptr<T,Kind>>::value, "Internal error.");
      static_assert(std::is_trivially_copyable<global_ptr<const T,Kind>>::value, "Internal error.");
      static_assert(std::is_trivially_destructible<global_ptr<T,Kind>>::value, "Internal error.");
      static_assert(std::is_trivially_destructible<global_ptr<const T,Kind>>::value, "Internal error.");
      static_assert(std::is_default_constructible<global_ptr<T,Kind>>::value, "Internal error.");
      static_assert(std::is_default_constructible<global_ptr<const T,Kind>>::value, "Internal error.");
      static_assert(sizeof(global_ptr) <= 16, "global_ptr should be 128-bits or less");
      UPCXXI_GPTR_CHK(*this);
    }

    // global_ptr offset and reinterpret in a single operation
    template <typename U>
    explicit global_ptr(detail::internal_only, 
                        const global_ptr<U, Kind> &other, std::ptrdiff_t offset):
      #if UPCXXI_MANY_KINDS
        UPCXXI_INTERNAL_ONLY(heap_idx_)(other.UPCXXI_INTERNAL_ONLY(heap_idx_)),
        UPCXXI_INTERNAL_ONLY(dynamic_kind_)(other.UPCXXI_INTERNAL_ONLY(dynamic_kind_)),
      #endif
      UPCXXI_INTERNAL_ONLY(rank_)(other.UPCXXI_INTERNAL_ONLY(rank_)),
      UPCXXI_INTERNAL_ONLY(raw_ptr_)(reinterpret_cast<T*>(
          reinterpret_cast<::std::uintptr_t>(
            other.UPCXXI_INTERNAL_ONLY(raw_ptr_)) + offset)) {
        UPCXXI_GPTR_CHK(other);
        UPCXX_ASSERT(other, "Global pointer expression may not be null");
        UPCXXI_GPTR_CHK_NONNULL(*this);
      }

    // trivial copy construct/assign (TriviallyCopyable)
    global_ptr(global_ptr const &) = default;
    global_ptr& operator=(global_ptr const &) = default;

    // kind conversion constructor
    template<memory_kind FromKind,
             typename = typename std::enable_if<(Kind != FromKind && Kind == memory_kind::any)>::type>
    global_ptr(global_ptr<const T,FromKind> const &that):
      global_ptr(detail::internal_only(),
                 that.UPCXXI_INTERNAL_ONLY(rank_),
                 that.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                 that.UPCXXI_INTERNAL_ONLY(heap_idx_),
                 that.UPCXXI_INTERNAL_ONLY(dynamic_kind_)) {
      UPCXXI_GPTR_CHK(*this);
    }
    
    // null pointer represented with rank 0
    global_ptr(std::nullptr_t nil = nullptr):
      global_ptr(detail::internal_only(), 0, nullptr) {
      UPCXXI_GPTR_CHK(*this);
    }
    
    UPCXXI_ATTRIB_PURE
    bool is_local() const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(*this);
      UPCXXI_ASSERT_VALID_DEFINITELY_LOCAL();
      return 
        // is static host kind or dynamic host kind or null:
        (Kind == memory_kind::host || UPCXXI_INTERNAL_ONLY(heap_idx_) == 0) 
        &&
        // statically one local_team or is null or rank in my local_team:
        (/*constexpr*/backend::all_ranks_definitely_local || 
         UPCXXI_INTERNAL_ONLY(raw_ptr_) == nullptr ||
         backend::rank_is_local(UPCXXI_INTERNAL_ONLY(rank_)));
    }

    UPCXXI_ATTRIB_PURE
    bool is_null() const {
      UPCXXI_GPTR_CHK(*this);
      return UPCXXI_INTERNAL_ONLY(heap_idx_) == 0 &&
        UPCXXI_INTERNAL_ONLY(raw_ptr_) == nullptr;
    }
    
    // This creates ambiguity with gp/int arithmetic like `my_gp + 1` since 
    // the compiler can't decide if it wants to upconvert the 1 to ptrdiff_t
    // or downconvert (to bool) the gp and use operator+(int,int). This is why
    // our operator+/- have overloads for all the integral types (those smaller
    // than `int` aren't necessary due to promotion).
    UPCXXI_ATTRIB_PURE
    explicit operator bool() const {
      return !is_null();
    }
    
    UPCXXI_ATTRIB_PURE
    const T* local() const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_GPTR_CHK(*this);
      UPCXX_ASSERT_ALWAYS(Kind == memory_kind::host || UPCXXI_INTERNAL_ONLY(heap_idx_) == 0,
                   "global_ptr<T>::local() does not create device pointers. Use device_allocator<Device>::local(gptr) instead.");
      // locality checks for host pointers handled in backend::localize_memory()
      return static_cast<T*>(
          backend::localize_memory(
            UPCXXI_INTERNAL_ONLY(rank_),
            reinterpret_cast<std::uintptr_t>(UPCXXI_INTERNAL_ONLY(raw_ptr_))
          )
      );
    }

    UPCXXI_ATTRIB_PURE
    intrank_t where() const {
      UPCXXI_GPTR_CHK(*this);
      return UPCXXI_INTERNAL_ONLY(rank_);
    }

    UPCXXI_ATTRIB_PURE
    memory_kind dynamic_kind() const {
      UPCXXI_GPTR_CHK(*this);
      if (Kind == memory_kind::any)
        return UPCXXI_INTERNAL_ONLY(dynamic_kind_);
      else
        return Kind;
    }
    
    UPCXXI_ATTRIB_PURE
    std::ptrdiff_t operator-(global_ptr rhs) const {
      if (UPCXXI_INTERNAL_ONLY(raw_ptr_) == rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_)) {
        UPCXXI_GPTR_CHK(*this); UPCXXI_GPTR_CHK(rhs);
      } else {
        UPCXXI_GPTR_CHK_NONNULL(*this); UPCXXI_GPTR_CHK_NONNULL(rhs);
      }
      UPCXX_ASSERT(
        UPCXXI_INTERNAL_ONLY(heap_idx_) == rhs.UPCXXI_INTERNAL_ONLY(heap_idx_),
        "operator-(global_ptr,global_ptr): requires pointers of the same kind & device."
      );
      UPCXX_ASSERT(
        UPCXXI_INTERNAL_ONLY(rank_) == rhs.UPCXXI_INTERNAL_ONLY(rank_),
        "operator-(global_ptr,global_ptr): requires pointers to the same rank."
      );
      return UPCXXI_INTERNAL_ONLY(raw_ptr_) - rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_);
    }

    UPCXXI_ATTRIB_CONST
    friend bool operator==(global_ptr a, global_ptr b) {
      UPCXXI_GPTR_CHK(a); UPCXXI_GPTR_CHK(b); 
      return a.UPCXXI_INTERNAL_ONLY(heap_idx_) == b.UPCXXI_INTERNAL_ONLY(heap_idx_) &&
        a.UPCXXI_INTERNAL_ONLY(rank_) == b.UPCXXI_INTERNAL_ONLY(rank_) &&
        a.UPCXXI_INTERNAL_ONLY(raw_ptr_) == b.UPCXXI_INTERNAL_ONLY(raw_ptr_);
    }
    UPCXXI_ATTRIB_CONST
    friend bool operator==(global_ptr a, std::nullptr_t) {
      return a == global_ptr(nullptr);
    }
    UPCXXI_ATTRIB_CONST
    friend bool operator==(std::nullptr_t, global_ptr b) {
      return global_ptr(nullptr) == b;
    }
    
    UPCXXI_ATTRIB_CONST
    friend bool operator!=(global_ptr a, global_ptr b) {
      UPCXXI_GPTR_CHK(a); UPCXXI_GPTR_CHK(b); 
      return a.UPCXXI_INTERNAL_ONLY(heap_idx_) != b.UPCXXI_INTERNAL_ONLY(heap_idx_) ||
        a.UPCXXI_INTERNAL_ONLY(rank_) != b.UPCXXI_INTERNAL_ONLY(rank_) ||
        a.UPCXXI_INTERNAL_ONLY(raw_ptr_) != b.UPCXXI_INTERNAL_ONLY(raw_ptr_);
    }
    UPCXXI_ATTRIB_CONST
    friend bool operator!=(global_ptr a, std::nullptr_t) {
      return a != global_ptr(nullptr);
    }
    UPCXXI_ATTRIB_CONST
    friend bool operator!=(std::nullptr_t, global_ptr b) {
      return global_ptr(nullptr) != b;
    }
    
    // Comparison operators specify partial order
    #define UPCXXI_COMPARE_OP(op) \
      UPCXXI_ATTRIB_CONST \
      friend bool operator op(global_ptr a, global_ptr b) {\
        UPCXXI_GPTR_CHK(a); UPCXXI_GPTR_CHK(b); \
        return a.UPCXXI_INTERNAL_ONLY(raw_ptr_) op b.UPCXXI_INTERNAL_ONLY(raw_ptr_);\
      }\
      UPCXXI_ATTRIB_CONST \
      friend bool operator op(global_ptr a, std::nullptr_t b) {\
        UPCXXI_GPTR_CHK(a); \
        return a.UPCXXI_INTERNAL_ONLY(raw_ptr_) op b;\
      }\
      UPCXXI_ATTRIB_CONST \
      friend bool operator op(std::nullptr_t a, global_ptr b) {\
        UPCXXI_GPTR_CHK(b); \
        return a op b.UPCXXI_INTERNAL_ONLY(raw_ptr_);\
      }
    UPCXXI_COMPARE_OP(<)
    UPCXXI_COMPARE_OP(<=)
    UPCXXI_COMPARE_OP(>)
    UPCXXI_COMPARE_OP(>=)
    #undef UPCXXI_COMPARE_OP
  
  public: //private!
    #if UPCXXI_MANY_KINDS
      std::uint32_t UPCXXI_INTERNAL_ONLY(heap_idx_) : 24;
      memory_kind   UPCXXI_INTERNAL_ONLY(dynamic_kind_) : 8;
    #else
      static constexpr std::uint32_t UPCXXI_INTERNAL_ONLY(heap_idx_) = 0;
      static constexpr memory_kind   UPCXXI_INTERNAL_ONLY(dynamic_kind_) = memory_kind::host;
    #endif
    intrank_t UPCXXI_INTERNAL_ONLY(rank_);
    T* UPCXXI_INTERNAL_ONLY(raw_ptr_);

    T* raw_internal(detail::internal_only) {
      return UPCXXI_INTERNAL_ONLY(raw_ptr_);
    }
  };

  template<typename T, typename U, memory_kind K>
  UPCXXI_ATTRIB_CONST
  global_ptr<T,K> static_pointer_cast(global_ptr<U,K> ptr) {
    UPCXXI_GPTR_CHK(ptr);
    return global_ptr<T,K>(detail::internal_only(),
                           ptr.UPCXXI_INTERNAL_ONLY(rank_),
                           static_cast<T*>(ptr.UPCXXI_INTERNAL_ONLY(raw_ptr_)),
                           ptr.UPCXXI_INTERNAL_ONLY(heap_idx_),
                           ptr.UPCXXI_INTERNAL_ONLY(dynamic_kind_));
  }

  template<typename T, typename U, memory_kind K>
  UPCXXI_ATTRIB_CONST
  global_ptr<T,K> reinterpret_pointer_cast(global_ptr<U,K> ptr) {
    UPCXXI_GPTR_CHK(ptr);
    return global_ptr<T,K>(detail::internal_only(),
                           ptr.UPCXXI_INTERNAL_ONLY(rank_),
                           reinterpret_cast<T*>(ptr.UPCXXI_INTERNAL_ONLY(raw_ptr_)),
                           ptr.UPCXXI_INTERNAL_ONLY(heap_idx_),
                           ptr.UPCXXI_INTERNAL_ONLY(dynamic_kind_));
  }

  template<typename T, typename U, memory_kind K>
  UPCXXI_ATTRIB_CONST
  global_ptr<T,K> const_pointer_cast(global_ptr<U,K> ptr) {
    UPCXXI_GPTR_CHK(ptr);
    return global_ptr<T,K>(detail::internal_only(),
                           ptr.UPCXXI_INTERNAL_ONLY(rank_),
                           const_cast<T*>(ptr.UPCXXI_INTERNAL_ONLY(raw_ptr_)),
                           ptr.UPCXXI_INTERNAL_ONLY(heap_idx_),
                           ptr.UPCXXI_INTERNAL_ONLY(dynamic_kind_));
  }

  template<memory_kind ToK, typename T, memory_kind FromK>
  UPCXXI_ATTRIB_CONST
  // sfinae out if the kinds are statically incompatible
  typename std::enable_if<ToK == FromK || ToK == memory_kind::any || FromK == memory_kind::any,
                          global_ptr<T,ToK>>::type
  static_kind_cast(global_ptr<T,FromK> p) {
    UPCXXI_GPTR_CHK(p);
    return global_ptr<T,ToK>(detail::internal_only(),
                           p.UPCXXI_INTERNAL_ONLY(rank_),
                           p.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                           p.UPCXXI_INTERNAL_ONLY(heap_idx_),
                           p.UPCXXI_INTERNAL_ONLY(dynamic_kind_));
  }
  
  template<memory_kind ToK, typename T, memory_kind FromK>
  UPCXXI_ATTRIB_CONST
  // sfinae out if the kinds are statically incompatible
  typename std::enable_if<ToK == FromK || ToK == memory_kind::any || FromK == memory_kind::any,
                          global_ptr<T,ToK>>::type
  dynamic_kind_cast(global_ptr<T,FromK> p) {
    UPCXXI_GPTR_CHK(p);
    return (ToK == memory_kind::any || ToK == p.dynamic_kind())
        ? global_ptr<T,ToK>(detail::internal_only(), 
                           p.UPCXXI_INTERNAL_ONLY(rank_),
                           p.UPCXXI_INTERNAL_ONLY(raw_ptr_),
                           p.UPCXXI_INTERNAL_ONLY(heap_idx_),
                           p.UPCXXI_INTERNAL_ONLY(dynamic_kind_)
        )
        : global_ptr<T,ToK>(nullptr);
  }

  template<typename T, memory_kind K>
  std::ostream& operator<<(std::ostream &os, global_ptr<T,K> ptr) {
    // UPCXXI_GPTR_CHK(ptr) // allow output of bad pointers for diagnostic purposes
    return os << "(gp: " << ptr.UPCXXI_INTERNAL_ONLY(rank_) << ", " 
              << reinterpret_cast<void*>(ptr.UPCXXI_INTERNAL_ONLY(raw_ptr_)) // issue #223
	      << ", heap=" << ptr.UPCXXI_INTERNAL_ONLY(heap_idx_) << ")";
  }

  template<typename T>
  UPCXXI_ATTRIB_PURE
  global_ptr<T> to_global_ptr(T *p) {
    UPCXXI_ASSERT_INIT();
    if(p == nullptr)
      return global_ptr<T>(nullptr);
    else {
      intrank_t rank;
      std::uintptr_t raw;
    
      std::tie(rank, raw) = backend::globalize_memory((void*)p);
    
      return global_ptr<T>(detail::internal_only(), rank, reinterpret_cast<T*>(raw));
    }
  }
  
  template<typename T>
  UPCXXI_ATTRIB_PURE
  global_ptr<T> try_global_ptr(T *p) {
    UPCXXI_ASSERT_INIT();
    intrank_t rank;
    std::uintptr_t raw;
    
    std::tie(rank, raw) =
      p == nullptr
        ? std::tuple<intrank_t, std::uintptr_t>(0, 0x0)
        : backend::globalize_memory((void*)p, std::make_tuple(0, 0x0));
    
    return global_ptr<T>(detail::internal_only(), rank, reinterpret_cast<T*>(raw));
  }
}

////////////////////////////////////////////////////////////////////////////////
// Specializations of standard function objects

namespace std {
  // Comparators specify total order
  template<typename T, upcxx::memory_kind K>
  struct less<upcxx::global_ptr<T,K>> {
    UPCXXI_ATTRIB_CONST
    bool operator()(upcxx::global_ptr<T,K> lhs,
                              upcxx::global_ptr<T,K> rhs) const {
      UPCXXI_GPTR_CHK(lhs); UPCXXI_GPTR_CHK(rhs); 
      bool ans = lhs.UPCXXI_INTERNAL_ONLY(raw_ptr_) < rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(rank_) == rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(rank_) < rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) == rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) < rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      return ans;
    }
  };
  
  template<typename T, upcxx::memory_kind K>
  struct less_equal<upcxx::global_ptr<T,K>> {
    UPCXXI_ATTRIB_CONST
    bool operator()(upcxx::global_ptr<T,K> lhs,
                              upcxx::global_ptr<T,K> rhs) const {
      UPCXXI_GPTR_CHK(lhs); UPCXXI_GPTR_CHK(rhs); 
      bool ans = lhs.UPCXXI_INTERNAL_ONLY(raw_ptr_) <= rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(rank_) == rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(rank_) < rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) == rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) < rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      return ans;
    }
  };
  
  template<typename T, upcxx::memory_kind K>
  struct greater<upcxx::global_ptr<T,K>> {
    UPCXXI_ATTRIB_CONST
    bool operator()(upcxx::global_ptr<T,K> lhs,
                              upcxx::global_ptr<T,K> rhs) const {
      UPCXXI_GPTR_CHK(lhs); UPCXXI_GPTR_CHK(rhs); 
      bool ans = lhs.UPCXXI_INTERNAL_ONLY(raw_ptr_) > rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(rank_) == rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(rank_) > rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) == rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) > rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      return ans;
    }
  };
  
  template<typename T, upcxx::memory_kind K>
  struct greater_equal<upcxx::global_ptr<T,K>> {
    UPCXXI_ATTRIB_CONST
    bool operator()(upcxx::global_ptr<T,K> lhs,
                              upcxx::global_ptr<T,K> rhs) const {
      UPCXXI_GPTR_CHK(lhs); UPCXXI_GPTR_CHK(rhs); 
      bool ans = lhs.UPCXXI_INTERNAL_ONLY(raw_ptr_) >= rhs.UPCXXI_INTERNAL_ONLY(raw_ptr_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(rank_) == rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(rank_) > rhs.UPCXXI_INTERNAL_ONLY(rank_);
      ans &= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) == rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      ans |= lhs.UPCXXI_INTERNAL_ONLY(heap_idx_) > rhs.UPCXXI_INTERNAL_ONLY(heap_idx_);
      return ans;
    }
  };

  template<typename T, upcxx::memory_kind K>
  struct hash<upcxx::global_ptr<T,K>> {
    UPCXXI_ATTRIB_CONST
    std::size_t operator()(upcxx::global_ptr<T,K> gptr) const {
      UPCXXI_GPTR_CHK(gptr); 
      /** Utilities derived from Boost, subject to the following license:

      Boost Software License - Version 1.0 - August 17th, 2003

      Permission is hereby granted, free of charge, to any person or organization
      obtaining a copy of the software and accompanying documentation covered by
      this license (the "Software") to use, reproduce, display, distribute,
      execute, and transmit the Software, and to prepare derivative works of the
      Software, and to permit third-parties to whom the Software is furnished to
      do so, all subject to the following:

      The copyright notices in the Software and this entire statement, including
      the above license grant, this restriction and the following disclaimer,
      must be included in all copies of the Software, in whole or in part, and
      all derivative works of the Software, unless such copies or derivative
      works are solely in the form of machine-executable object code generated by
      a source language processor.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
      SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
      FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
      ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
      DEALINGS IN THE SOFTWARE.
      */

      std::uint64_t b = std::uint64_t(gptr.UPCXXI_INTERNAL_ONLY(heap_idx_))<<32 |
        std::uint32_t(gptr.UPCXXI_INTERNAL_ONLY(rank_));
      std::uint64_t a =
        reinterpret_cast<std::uint64_t>(gptr.UPCXXI_INTERNAL_ONLY(raw_ptr_));
      a ^= b + 0x9e3779b9 + (a<<6) + (a>>2);
      return std::size_t(a);
    }
  };
} // namespace std

#undef UPCXXI_IN_GLOBAL_PTR_HPP
#endif
