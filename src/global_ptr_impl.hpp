#ifndef UPCXXI_IN_GLOBAL_PTR_HPP
#error This header is not meant to be included directly. Please use #include <upcxx/upcxx.hpp>
#endif

    static_assert(!std::is_volatile<T>::value,
                  "global_ptr<T> does not support volatile qualification on T");

    static_assert(!std::is_reference<T>::value, // spec issue #158
                  "global_ptr<T> does not support reference types as T");

    global_ptr operator+=(std::ptrdiff_t diff) {
      if (diff) UPCXXI_GPTR_CHK_NONNULL(*this);
      else      UPCXXI_GPTR_CHK(*this);
      this->UPCXXI_INTERNAL_ONLY(raw_ptr_) += diff;
      UPCXXI_GPTR_CHK(*this);
      return *this;
    }
    friend global_ptr operator+(global_ptr a, int b) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(global_ptr a, long b) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(global_ptr a, long long b) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(global_ptr a, unsigned int b) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(global_ptr a, unsigned long b) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(global_ptr a, unsigned long long b) { return a += (ptrdiff_t)b; }

    friend global_ptr operator+(int b, global_ptr a) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(long b, global_ptr a) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(long long b, global_ptr a) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(unsigned int b, global_ptr a) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(unsigned long b, global_ptr a) { return a += (ptrdiff_t)b; }
    friend global_ptr operator+(unsigned long long b, global_ptr a) { return a += (ptrdiff_t)b; }

    global_ptr operator-=(std::ptrdiff_t diff) {
      if (diff) UPCXXI_GPTR_CHK_NONNULL(*this);
      else      UPCXXI_GPTR_CHK(*this);
      this->UPCXXI_INTERNAL_ONLY(raw_ptr_) -= diff;
      UPCXXI_GPTR_CHK(*this);
      return *this;
    }
    friend global_ptr operator-(global_ptr a, int b) { return a -= (ptrdiff_t)b; }
    friend global_ptr operator-(global_ptr a, long b) { return a -= (ptrdiff_t)b; }
    friend global_ptr operator-(global_ptr a, long long b) { return a -= (ptrdiff_t)b; }
    friend global_ptr operator-(global_ptr a, unsigned int b) { return a -= (ptrdiff_t)b; }
    friend global_ptr operator-(global_ptr a, unsigned long b) { return a -= (ptrdiff_t)b; }
    friend global_ptr operator-(global_ptr a, unsigned long long b) { return a -= (ptrdiff_t)b; }

    global_ptr& operator++() {
      return *this = *this + 1;
    }

    global_ptr operator++(int) {
      global_ptr old = *this;
      *this = *this + 1;
      return old;
    }

    global_ptr& operator--() {
      return *this = *this - 1;
    }

    global_ptr operator--(int) {
      global_ptr old = *this;
      *this = *this - 1;
      return old;
    }

    friend struct std::less<global_ptr<element_type,Kind>>;
    friend struct std::less_equal<global_ptr<element_type,Kind>>;
    friend struct std::greater<global_ptr<element_type,Kind>>;
    friend struct std::greater_equal<global_ptr<element_type,Kind>>;
    friend struct std::hash<global_ptr<element_type,Kind>>;

    template<typename U, typename V, memory_kind K>
    friend global_ptr<U,K> reinterpret_pointer_cast(global_ptr<V,K> ptr);

    template<typename U, memory_kind K>
    friend std::ostream& operator<<(std::ostream &os, global_ptr<U,K> ptr);
