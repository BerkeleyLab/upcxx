#ifndef _3dad9bb9_dc96_4a7a_ae33_13d58d0d974f
#define _3dad9bb9_dc96_4a7a_ae33_13d58d0d974f

#include <upcxx/upcxx_config.hpp>
#include <upcxx/backend_fwd.hpp>

#include <atomic>
#include <mutex>

// issue 495: UPC++ does not currently support GASNet-level AM progress threads
// To minimize impact on users we assume it's off at compile time and validate 
// the dynamic value at startup to ensure we haven't broken the rule.
#define UPCXXI_HIDDEN_AM_CONCURRENCY_LEVEL 0

namespace upcxx {
  namespace detail {
  #if UPCXXI_BACKEND_GASNET_PAR || UPCXXI_HIDDEN_AM_CONCURRENCY_LEVEL
    // AM handlers may run concurrently wrt the primordial thread

    using par_mutex = std::mutex;

    template<typename T>
    using par_atomic = std::atomic<T>;
  #else
    // AM handlers never run concurrently wrt the primordial thread

    struct par_mutex {
      void lock() {}
      void unlock() {}
    };

    template<typename T>
    class par_atomic {
      T val_;
    public:
      par_atomic() noexcept = default;
      par_atomic(T desired) noexcept: val_(desired) {}
      par_atomic(par_atomic const&) = delete;

      T operator=(T desired) {
        return (val_ = desired);
      }
      par_atomic& operator=(par_atomic const&) = delete;

      bool is_lock_free() const noexcept { return true; }

      T load(std::memory_order = std::memory_order_seq_cst) const noexcept {
        return val_;
      }
      void store(T desired, std::memory_order = std::memory_order_seq_cst) noexcept {
        val_ = desired;
      }

      operator T() const noexcept { return val_; }

      T exchange(T desired, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ = desired;
        return old;
      }

      bool compare_exchange_weak(T &expected, T desired, std::memory_order = std::memory_order_seq_cst) noexcept {
        if(val_ == expected) {
          val_ = desired;
          return true;
        }
        else {
          expected = val_;
          return false;
        }
      }

      bool compare_exchange_weak(
          T &expected, T desired,
          std::memory_order = std::memory_order_seq_cst,
          std::memory_order = std::memory_order_seq_cst
        ) noexcept {
        return this->compare_exchange_weak(expected, desired);
      }

      bool compare_exchange_strong(T &expected, T desired, std::memory_order = std::memory_order_seq_cst) noexcept {
        return this->compare_exchange_weak(expected, desired);
      }

      bool compare_exchange_strong(
          T &expected, T desired,
          std::memory_order = std::memory_order_seq_cst,
          std::memory_order = std::memory_order_seq_cst
        ) noexcept {
        return this->compare_exchange_weak(expected, desired);
      }

      template<typename U>
      T fetch_add(U x, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ += x;
        return old;
      }
      template<typename U>
      T fetch_sub(U x, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ -= x;
        return old;
      }
      template<typename U>
      T fetch_or(U x, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ |= x;
        return old;
      }
      template<typename U>
      T fetch_and(U x, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ &= x;
        return old;
      }
      template<typename U>
      T fetch_xor(U x, std::memory_order = std::memory_order_seq_cst) noexcept {
        T old = val_;
        val_ ^= x;
        return old;
      }
    };
  #endif
  }
}
#endif
