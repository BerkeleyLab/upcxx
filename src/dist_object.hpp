#ifndef _60c9396d_79c1_45f4_a5d2_aa6194a75958
#define _60c9396d_79c1_45f4_a5d2_aa6194a75958

#include <upcxx/bind.hpp>
#include <upcxx/digest.hpp>
#include <upcxx/future.hpp>
#include <upcxx/rpc.hpp>
#include <upcxx/utility.hpp>
#include <upcxx/team.hpp>

#include <cstdint>
#include <functional>

namespace upcxx {
  template<typename T>
  struct dist_id;
  
  template<typename T>
  class dist_object;
}

////////////////////////////////////////////////////////////////////////
  
namespace upcxx {
  template<typename T>
  struct dist_id {
  private:
    detail::digest dig_;
    explicit dist_id(detail::digest id) : dig_(id) {}

    friend class dist_object<T>;
    friend struct std::hash<upcxx::dist_id<T>>;
    
  public:
    dist_id() : dig_(detail::digest::zero()) {}

    dist_object<T>& here() const {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT(detail::registry[dig_],
        "dist_id::here() called for an invalid id or dist_object (possibly outside its lifetime)");
      return std::get<0>(
        // 3. retrieve results tuple
        detail::future_header_result<dist_object<T>&>::results_of(
          // 1. get future_header_promise<...>* for this digest
          &detail::registered_promise<dist_object<T>&>(dig_)
            // 2. cast to future_header* (not using inheritnace, must use embedded first member)
            ->base_header_result.base_header
        )
      );
    }
    
    future<dist_object<T>&> when_here() const {
      UPCXX_ASSERT_INIT();
      return detail::promise_get_future(detail::registered_promise<dist_object<T>&>(dig_));
    }
    
    #define UPCXX_COMPARATOR(op) \
      friend bool operator op(dist_id a, dist_id b) {\
        return a.dig_ op b.dig_; \
      }
    UPCXX_COMPARATOR(==)
    UPCXX_COMPARATOR(!=)
    UPCXX_COMPARATOR(<)
    UPCXX_COMPARATOR(<=)
    UPCXX_COMPARATOR(>)
    UPCXX_COMPARATOR(>=)
    #undef UPCXX_COMPARATOR

    friend std::ostream& operator<<(std::ostream &o, dist_id<T> x) {
      return o << x.dig_;
    }
  };
}

namespace std {
  template<typename T>
  struct hash<upcxx::dist_id<T>> {
    size_t operator()(upcxx::dist_id<T> id) const {
      return hash<upcxx::detail::digest>()(id.dig_);
    }
  };
}

////////////////////////////////////////////////////////////////////////

namespace upcxx {
  template<typename T>
  class dist_object {
    const upcxx::team *tm_;
    detail::digest id_;
    T value_;
    
  public:
    template<typename ...U>
    dist_object(const upcxx::team &tm, U &&...arg):
      tm_(&tm),
      value_(std::forward<U>(arg)...) {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT_MASTER();
      UPCXX_ASSERT_COLLECTIVE_SAFE(entry_barrier::none);
      
      id_ = const_cast<upcxx::team*>(&tm)->next_collective_id(detail::internal_only());
      
      backend::fulfill_during<progress_level::user>(
          detail::registered_promise<dist_object<T>&>(id_)->incref(1),
          std::tuple<dist_object<T>&>(*this),
          backend::master
        );
    }
    
    dist_object(T value, const upcxx::team &tm):
      tm_(&tm),
      value_(std::move(value)) {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT_MASTER();
      UPCXX_ASSERT_COLLECTIVE_SAFE(entry_barrier::none);

      id_ = const_cast<upcxx::team*>(&tm)->next_collective_id(detail::internal_only());
      
      backend::fulfill_during<progress_level::user>(
          detail::registered_promise<dist_object<T>&>(id_)->incref(1),
          std::tuple<dist_object<T>&>(*this),
          backend::master
        );
    }
    
    dist_object(T value):
      dist_object(upcxx::world(), std::move(value)) {
    }
    
    dist_object(dist_object const&) = delete;

    dist_object(dist_object &&that) noexcept:
      tm_(that.tm_),
      id_(that.id_),
      value_(std::move(that.value_)) {
      
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT_MASTER();
      UPCXX_ASSERT((that.id_ != detail::digest{~0ull, ~0ull}));

      that.id_ = detail::digest{~0ull, ~0ull}; // the tombstone id value

      // Moving is painful for us because the original constructor (of that)
      // created a promise, set its result to point to that, and then
      // deferred its fulfillment until user progress. We hackishly overwrite
      // the promise/future's result with our new address. Whether or not the
      // deferred fulfillment has happened doesn't matter, but will determine
      // whether the app observes the same future taking different values at
      // different times (definitely not usual for futures).
      static_cast<detail::future_header_promise<dist_object<T>&>*>(detail::registry[id_])
        ->base_header_result.reconstruct_results(std::tuple<dist_object<T>&>(*this));
    }
    
    ~dist_object() {
      if (backend::init_count > 0) UPCXX_ASSERT_MASTER();

      if(id_ != detail::digest{~0ull, ~0ull}) {
        auto it = detail::registry.find(id_);
        static_cast<detail::future_header_promise<dist_object<T>&>*>(it->second)->dropref();
        detail::registry.erase(it);
      }
    }
    
    T* operator->() const { return const_cast<T*>(&value_); }
    T& operator*() const { return const_cast<T&>(value_); }
    
    upcxx::team& team() { return *const_cast<upcxx::team*>(tm_); }
    const upcxx::team& team() const { return *tm_; }
    dist_id<T> id() const { return dist_id<T>{id_}; }
    
    UPCXX_NODISCARD
    future<deserialized_type_t<T>> fetch(intrank_t rank) const {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT_MASTER_CURRENT_IFSEQ();
      static_assert(
        is_serializable<T>::value,
        "T must be Serializable for dist_object<T>::fetch."
      );
      return upcxx::rpc(*tm_, rank,
                        [](dist_object<T> const &o) -> const T& {
                          return *o;
                        }, *this);
    }
  };
}

////////////////////////////////////////////////////////////////////////

namespace upcxx {
  namespace detail {
  // dist_object<T> references are bound using their id's.
  template<typename T>
  struct binding<dist_object<T>&> {
    using on_wire_type = dist_id<T>;
    using off_wire_type = dist_object<T>&;
    using off_wire_future_type = future<dist_object<T>&>;
    using stripped_type = dist_object<T>&;
    static constexpr bool immediate = false;
    
    static dist_id<T> on_wire(dist_object<T> const &o) {
      return o.id();
    }
    
    static future<dist_object<T>&> off_wire(dist_id<T> id) {
      return id.when_here();
    }
    static future<dist_object<T>&> off_wire_future(dist_id<T> id) {
      return id.when_here();
    }
  };
  
  template<typename T>
  struct binding<dist_object<T> const&>:
    binding<dist_object<T>&> {
    
    using stripped_type = dist_object<T> const&;
  };
  
  template<typename T>
  struct binding<dist_object<T>&&> {
    static_assert(sizeof(T) != sizeof(T),
      "Moving a dist_object into a binding must surely be an error!"
    );
  };
  }
}
#endif
