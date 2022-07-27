#ifndef _fbcda049_eb37_438b_9a9c_a2ce81b8b8f5
#define _fbcda049_eb37_438b_9a9c_a2ce81b8b8f5

/**
 * allocate.hpp
 */

#include <upcxx/backend.hpp>
#include <upcxx/diagnostic.hpp>
#include <upcxx/global_ptr.hpp>
#include <upcxx/exceptions.hpp>

#include <algorithm> // max
#include <cstdint>
#include <sstream>
#include <cstddef> // max_align_t
#include <limits> // numeric_limits
#include <type_traits> // aligned_storage, is_default_constructible,
                       // is_destructible, is_trivially_destructible

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  /* Declared in: upcxx/backend_fwd.hpp
  
  void* allocate(std::size_t size,
                 std::size_t alignment = alignof(std::max_align_t));
  
  void deallocate(void* ptr);
  */
  
  //////////////////////////////////////////////////////////////////////
  
  template<typename T>
  UPCXXI_NODISCARD
  global_ptr<T> allocate(std::size_t n = 1, std::size_t alignment = alignof(T)) {
    UPCXXI_ASSERT_INIT();
    void *p = upcxx::allocate(n * sizeof(T), alignment);
    return p == nullptr
      ? global_ptr<T>(nullptr)
      : global_ptr<T>(
          detail::internal_only{},
          upcxx::rank_me(),
          reinterpret_cast<T*>(p)
        );
  }

  template<typename T>
  void deallocate(global_ptr<T> gptr) {
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(gptr);
    if (gptr != nullptr) {
      UPCXX_ASSERT(
        gptr.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me(),
        "upcxx::deallocate must be called by owner of global pointer"
      );
      
      upcxx::deallocate(gptr.UPCXXI_INTERNAL_ONLY(raw_ptr_));
    }
  }

  namespace detail {
    template<bool throws, typename T, typename ...Args>
    global_ptr<T> new_(Args &&...args) {
      static_assert(!std::is_array<T>::value,
                    "The element type to upcxx::new_ currently may not "
                    "itself be an array type -- use upcxx::new_array "
                    "instead. Please contact us if you have a need for "
                    "this functionality in upcxx::new_.");

      void *ptr = allocate(sizeof(T), alignof(T));
      
      if (ptr == nullptr) {
        if (throws) {
          throw bad_shared_alloc(__func__, std::max(sizeof(T), alignof(T)));
        }
        return nullptr;
      }

      T *result;
      try {
        result = ::new(ptr) T(std::forward<Args>(args)...); // placement new
      } catch (...) {
        // reclaim memory and rethrow the exception
        deallocate(ptr);
        throw;
      }

      return global_ptr<T>(
        detail::internal_only{},
        upcxx::rank_me(),
        result
      );
    }
  }

  template<typename T, typename ...Args>
  UPCXXI_NODISCARD
  global_ptr<T> new_(Args &&...args) {
    UPCXXI_ASSERT_INIT();
    return detail::new_</*throws=*/true, T>(std::forward<Args>(args)...);
  }

  template<typename T, typename ...Args>
  UPCXXI_NODISCARD
  global_ptr<T> new_(const std::nothrow_t &tag, Args &&...args) {
    UPCXXI_ASSERT_INIT();
    return detail::new_</*throws=*/false, T>(std::forward<Args>(args)...);
  }

  namespace detail {
    template<bool throws, typename T>
    global_ptr<T> new_array(std::size_t n) {
      static_assert(std::is_default_constructible<T>::value,
                    "T must be default constructible");
      static_assert(!std::is_array<T>::value,
                    "The element type to upcxx::new_array currently "
                    "may not itself be an array type. Please contact us "
                    "if you have a need for this functionality.");
      
      std::size_t size = sizeof(std::size_t);
      size = (size + alignof(T)-1) & -alignof(T);

      if(n > (std::numeric_limits<std::size_t>::max() - size) / sizeof(T)) {
        // more bytes required than can be represented by size_t
        if(throws)
          throw bad_shared_alloc(std::string(__func__) + "(" + std::to_string(n) 
                              + ") requested more bytes than can be represented by size_t!");
        return nullptr;
      }

      std::size_t offset = size;
      size += n * sizeof(T);
      
      void *ptr = upcxx::allocate(size, std::max(alignof(std::size_t), alignof(T)));
      
      if(ptr == nullptr) {
        if(throws) throw bad_shared_alloc(__func__, size);
        return nullptr;
      }
      
      *reinterpret_cast<std::size_t*>(ptr) = n;
      T *elts = reinterpret_cast<T*>((char*)ptr + offset);
      
      if(!std::is_trivially_constructible<T>::value) {
        T *p = elts;
        try {
          for(T *p1=elts+n; p != p1; p++)
            ::new(p) T;
        } catch (...) {
          // destruct constructed elements, reclaim memory, and
          // rethrow the exception
          if(!std::is_trivially_destructible<T>::value) {
            for(T *p2=p-1; p2 >= elts; p2--)
              p2->~T();
          }
          deallocate(ptr);
          throw;
        }
      }
      
      return global_ptr<T>(detail::internal_only{}, upcxx::rank_me(), elts);
    }
  }

  template<typename T>
  UPCXXI_NODISCARD
  global_ptr<T> new_array(std::size_t n) {
    UPCXXI_ASSERT_INIT();
    return detail::new_array</*throws=*/true, T>(n);
  }

  template<typename T>
  UPCXXI_NODISCARD
  global_ptr<T> new_array(std::size_t n, const std::nothrow_t &tag) {
    UPCXXI_ASSERT_INIT();
    return detail::new_array</*throws=*/false, T>(n);
  }

  template<typename T>
  void delete_(global_ptr<T> gptr) {
    static_assert(std::is_destructible<T>::value,
                  "T must be destructible");
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(gptr);
    
    if (gptr != nullptr) {
      UPCXX_ASSERT(
        gptr.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me(),
        "upcxx::delete_ must be called by owner of shared memory."
      );
      
      T *ptr = gptr.UPCXXI_INTERNAL_ONLY(raw_ptr_);
      ptr->~T();
      upcxx::deallocate(ptr);
    }
  }

  template<typename T>
  void delete_array(global_ptr<T> gptr) {
    static_assert(std::is_destructible<T>::value,
                  "T must be destructible");
    UPCXXI_ASSERT_INIT();
    UPCXXI_GPTR_CHK(gptr);
    
    if (gptr != nullptr) {
      UPCXX_ASSERT(
        gptr.UPCXXI_INTERNAL_ONLY(rank_) == upcxx::rank_me(),
        "upcxx::delete_array must be called by owner of shared memory."
      );
      
      T *tptr = gptr.local();
      
      // padding to keep track of number of elements
      std::size_t padding = sizeof(std::size_t);
      padding = (padding + alignof(T)-1) & -alignof(T);
      
      void *ptr = reinterpret_cast<char*>(tptr) - padding;
      
      if (!std::is_trivially_destructible<T>::value) {
        std::size_t size = *reinterpret_cast<std::size_t*>(ptr);
        for(T *p=tptr, *p1=tptr+size; p != p1; p++)
          p->~T();
      }
      
      upcxx::deallocate(ptr);
    }
  }
} // namespace upcxx

#endif
