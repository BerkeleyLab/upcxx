#ifndef _60c9396d_79c1_45f4_a5d2_aa6194a75958
#define _60c9396d_79c1_45f4_a5d2_aa6194a75958

#include <upcxx/bind.hpp>
#include <upcxx/digest.hpp>
#include <upcxx/future.hpp>
#include <upcxx/optional.hpp>
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
    dist_id() : dig_(detail::tombstone) {}

    UPCXXI_ATTRIB_PURE
    dist_object<T>& here() const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_NOT_TOMB(dig_);
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
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_NOT_TOMB(dig_);
      return detail::promise_get_future(detail::registered_promise<dist_object<T>&>(dig_));
    }
    
    #define UPCXXI_COMPARATOR(op) \
      UPCXXI_ATTRIB_CONST \
      friend bool operator op(dist_id a, dist_id b) {\
        return a.dig_ op b.dig_; \
      }
    UPCXXI_COMPARATOR(==)
    UPCXXI_COMPARATOR(!=)
    UPCXXI_COMPARATOR(<)
    UPCXXI_COMPARATOR(<=)
    UPCXXI_COMPARATOR(>)
    UPCXXI_COMPARATOR(>=)
    #undef UPCXXI_COMPARATOR

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
  struct inactive_t{
    explicit inactive_t() = default;
  };
  constexpr inactive_t inactive{};

  template<typename T>
  class dist_object {
    const upcxx::team *tm_;
    detail::digest id_;
    upcxx::optional<T> value_;

    struct init_list_param {};
    
  public:
    dist_object():
      tm_(nullptr),
      id_(detail::tombstone) {
    }

    template<typename ...U>
    dist_object(upcxx::inactive_t, U &&...arg):
      tm_(nullptr),
      id_(detail::tombstone),
      value_(upcxx::in_place, std::forward<U>(arg)...) {
    }

    template<typename ...U>
    dist_object(const upcxx::team &tm, U &&...arg):
      tm_(&tm),
      value_(upcxx::in_place, std::forward<U>(arg)...) {
      create_();
    }
    
    dist_object(T value, const upcxx::team &tm):
      tm_(&tm),
      value_(upcxx::in_place, std::move(value)) {
      create_();
    }
    
    dist_object(T value):
      dist_object(upcxx::world(), std::move(value)) {
    }

    // This overload is to avoid ambiguity in cases like:
    //   dist_object<std::unordered_map<int,int>>({})
    // Without this overload, the call could resolve to either the
    // dist_object(T) ctor, with a default constructed unordered_map
    // as the argument, or to dist_object(dist_object&&), with a
    // default constructed dist_object as the argument. See
    // example/prog-guide/dmap.hpp for an actual use case.
    dist_object(std::initializer_list<init_list_param>):
      dist_object(T{}) {
    }
    
    dist_object(dist_object const&) = delete;

    dist_object(dist_object &&that) noexcept: dist_object() {
      *this = std::move(that);
    }

    dist_object& operator=(dist_object &&that) noexcept {
      if (&that == this) return *this; // see issue 547

      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_MASTER();
      // only allow assignment moves onto "dead" object
      UPCXX_ASSERT(
        !this->is_active(),
        "Move assignment is only allowed on an inactive dist_object"
      );

      this->tm_ = that.tm_;
      this->id_ = that.id_;
      this->value_ = std::move(that.value_);
      // revert `that` to non-constructed state
      that.tm_ = nullptr;
      that.id_ = detail::tombstone;

      if (this->is_active()) {
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

      return *this;
    }
    
    ~dist_object() {
      if (is_active() && backend::init_count > 0) UPCXXI_ASSERT_MASTER();

      if(id_ != detail::tombstone) {
        auto it = detail::registry.find(id_);
        static_cast<detail::future_header_promise<dist_object<T>&>*>(it->second)->dropref();
        detail::registry.erase(it);
      }
    }

    void activate(const upcxx::team &tm) {
      UPCXX_ASSERT(
        !is_active(),
        "dist_object<T>::activate() called on an already active object"
      );
      UPCXX_ASSERT(
        has_value(),
        "dist_object<T>::activate() called on an object that has no value"
      );
      tm_ = &tm;
      create_();
    }

    bool is_active() const { return id_ != detail::tombstone; }
    bool has_value() const { return value_.has_value(); }

    template<typename ...Args>
    T& emplace(Args&&... args) {
      return value_.emplace(std::forward<Args>(args)...);
    }
    
    T* operator->() const {
      UPCXX_ASSERT(has_value());
      return const_cast<T*>(&*value_);
    }
    T& operator*() const {
      UPCXX_ASSERT(has_value());
      return const_cast<T&>(*value_);
    }
    
    upcxx::team& team() { return *const_cast<upcxx::team*>(tm_); }
    const upcxx::team& team() const { return *tm_; }
    dist_id<T> id() const { 
      UPCXXI_ASSERT_NOT_TOMB(id_);
      return dist_id<T>{id_};
    }
    
    UPCXXI_NODISCARD
    future<deserialized_type_t<T>> fetch(intrank_t rank) const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
      UPCXXI_ASSERT_NOT_TOMB(id_);
      static_assert(
        is_serializable<T>::value,
        "T must be Serializable for dist_object<T>::fetch."
      );
      return upcxx::rpc(*tm_, rank,
                        [](dist_object<T> const &o) -> const T& {
                          return *o;
                        }, *this);
    }

  private:
    void create_() {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_MASTER();
      UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::none);
      UPCXX_ASSERT(tm_);

      id_ = const_cast<upcxx::team*>(tm_)->next_collective_id(detail::internal_only());
      UPCXX_ASSERT(id_ != detail::tombstone);

      backend::fulfill_during<progress_level::user>(
          detail::registered_promise<dist_object<T>&>(id_)->incref(1),
          std::tuple<dist_object<T>&>(*this),
          backend::master
        );
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
      UPCXX_ASSERT(o.is_active());
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
