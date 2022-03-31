#ifndef _773d73ab_fce8_4b18_9cd0_d7c76204383c
#define _773d73ab_fce8_4b18_9cd0_d7c76204383c

#include <upcxx/team_fwd.hpp>
#include <upcxx/backend.hpp>

namespace upcxx {
  namespace detail {
    template<typename T>
    future_header_promise<T>* registered_promise(detail::digest id, int initial_anon) {
      UPCXXI_ASSERT_MASTER();
      
      future_header_promise<T> *pro;
      
      // We use dist_master_registry to map from `dist_id<T>` to
      // `promise<dist_object<T>&>*`
      auto it_and_inserted = registry.insert({id, nullptr});
      
      if(it_and_inserted.second) {
        pro = new future_header_promise<T>;
        detail::promise_require_anonymous(pro, initial_anon);
        it_and_inserted.first->second = static_cast<void*>(pro);
      }
      else
        pro = static_cast<future_header_promise<T>*>(it_and_inserted.first->second);
      
      return pro;
    }

    template<typename T, typename ...U>
    T* registered_state(detail::digest id, U &&...ctor_args) {
      UPCXXI_ASSERT_MASTER();
      
      T *thing;
      
      // We use dist_master_registry to map from `dist_id<T>` to
      // `promise<dist_object<T>&>*`
      auto it_and_inserted = registry.insert({id, nullptr});
      
      if(it_and_inserted.second) {
        thing = new T(std::forward<U>(ctor_args)...);
        it_and_inserted.first->second = static_cast<void*>(thing);
      }
      else
        thing = static_cast<T*>(it_and_inserted.first->second);
      
      return thing;
    }
  }
 
  UPCXXI_ATTRIB_CONST
  inline bool local_team_contains(intrank_t rank) {
    UPCXXI_ASSERT_INIT();
    UPCXX_ASSERT(rank >= 0 && rank < upcxx::rank_n(),
      "local_team_contains(rank) requires rank in [0, world().rank_n()-1] == [0, " << upcxx::rank_n()-1 << "], but given: " << rank);
    return backend::rank_is_local(rank);
  }

  UPCXXI_ATTRIB_CONST
  inline std::pair<intrank_t, intrank_t> local_team_position() {
    UPCXXI_ASSERT_INIT();
    return std::make_pair(backend::nbrhd_set_rank,backend::nbrhd_set_size);
  }
  
  inline team& world() {
    // do NOT assert_init here - world() is the implicit default argument for too many calls,
    // so asserting here produces a message masking the true culprit that was explicitly invoked.
    // UPCXXI_ASSERT_INIT();
    return detail::the_world_team.value();
  }
  inline team& local_team() {
    UPCXXI_ASSERT_INIT();
    return detail::the_local_team.value();
  }

  namespace detail {
  // team references are bound using their id's.
  template<>
  struct binding<team&> {
    using on_wire_type = team_id;
    using off_wire_type = team&;
    using off_wire_future_type = future1<detail::future_kind_result, team&>;
    using stripped_type = team&;
    static constexpr bool immediate = true;
    
    static team_id on_wire(team const &o) {
      return o.id();
    }
    
    static team& off_wire(team_id id) {
      return id.here();
    }

    static off_wire_future_type off_wire_future(team_id id) {
      return detail::make_fast_future<team&>(id.here());
    }
  };
  
  template<>
  struct binding<team const&>:
    binding<team&> {
    
    using stripped_type = team const&;
  };
  
  template<>
  struct binding<team&&> {
    #if 0
      // TODO: figure out how to make this static_assert trip only when this
      // instantiation is USED.
      static_assert(false,
        "Moving a team&& into a binding must surely be an error!"
      );
    #endif
  };
  }
}
#endif
  
  
