#ifndef _ba798e6d_cac8_4c55_839e_7b4ba217212c
#define _ba798e6d_cac8_4c55_839e_7b4ba217212c

#include <upcxx/bind.hpp>
#include <upcxx/backend_fwd.hpp>
#include <upcxx/digest.hpp>
#include <upcxx/utility.hpp>

#include <gasnet_fwd.h> // gex_EP_Location_t

#include <unordered_map>

/* This is the forward declaration(s) of upcxx::team and friends. It does not
 * define the function bodies nor does it pull in the full backend header.
 */

////////////////////////////////////////////////////////////////////////////////

namespace upcxx {
  namespace detail {
    extern std::unordered_map<digest, void*> registry;
    
    // Get the promise pointer from the master map.
    template<typename T>
    future_header_promise<T>* registered_promise(detail::digest id, int initial_anon=0);

    template<typename T, typename ...U>
    T* registered_state(detail::digest id, U &&...ctor_args);
  }
  
  class team;
  
  struct team_id {
  private:
    detail::digest dig_;
    explicit team_id(detail::digest id) : dig_(id) {}

    friend class team;
    friend void finalize();
    friend struct std::hash<upcxx::team_id>;

  public:
    team_id() : dig_(detail::tombstone) {} // issue 343: disable trivial default construction

    UPCXXI_ATTRIB_PURE
    team& here() const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_NOT_TOMB(dig_);
      team *presult = static_cast<team*>(detail::registry[dig_]);
      UPCXX_ASSERT(presult, "team_id::here() called for an invalid id or team (possibly outside its lifetime)");
      return *presult;
    }

    future<team&> when_here() const {
      UPCXXI_ASSERT_INIT();
      UPCXXI_ASSERT_NOT_TOMB(dig_);
      team *pteam = static_cast<team*>(detail::registry[dig_]);
      // issue170: Currently the only form of team construction has barrier semantics,
      // such that a newly created team_id cannot arrive at user-level progress anywhere
      // until after the local representative has been constructed.
      UPCXX_ASSERT(pteam != nullptr);
      return make_future<team&>(*pteam);
    }
    
    #define UPCXXI_COMPARATOR(op) \
      friend bool operator op(team_id a, team_id b) {\
        return a.dig_ op b.dig_; \
      }
    UPCXXI_COMPARATOR(==)
    UPCXXI_COMPARATOR(!=)
    UPCXXI_COMPARATOR(<)
    UPCXXI_COMPARATOR(<=)
    UPCXXI_COMPARATOR(>)
    UPCXXI_COMPARATOR(>=)
    #undef UPCXXI_COMPARATOR
  
    friend inline std::ostream& operator<<(std::ostream &o, team_id x) {
      return o << x.dig_;
    }
  };
}

namespace std {
  template<>
  struct hash<upcxx::team_id> {
    size_t operator()(upcxx::team_id id) const {
      return hash<upcxx::detail::digest>()(id.dig_);
    }
  };
}

namespace upcxx {
  class team final :
      backend::team_base /* defined by <backend>/runtime_fwd.hpp */ {
    detail::digest id_;
    std::uint64_t coll_counter_;
    intrank_t n_, me_;
    
  public:
    team();
    team(detail::internal_only, backend::team_base &&base, detail::digest id,
         intrank_t n, intrank_t me);
    team(team const&) = delete;
    team(team &&that);
    team& operator=(team &&that);
    ~team();

    UPCXXI_ATTRIB_PURE
    bool is_active() const {
      return id_ != detail::tombstone;
    }
   
    UPCXXI_ATTRIB_PURE
    intrank_t rank_n() const { 
      UPCXXI_ASSERT_INIT(); 
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      return n_; 
    }
    UPCXXI_ATTRIB_PURE
    intrank_t rank_me() const { 
      UPCXXI_ASSERT_INIT(); 
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      return me_; 
    }
    
    UPCXXI_ATTRIB_PURE
    intrank_t from_world(intrank_t rank) const {
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      UPCXX_ASSERT(rank >= 0 && rank < upcxx::rank_n(), 
                   "team::from_world(rank) requires rank in [0, world().rank_n()-1] == [0, " << upcxx::rank_n()-1 << "], but given: " << rank);
      return backend::team_rank_from_world(*this, rank);
    }
    UPCXXI_ATTRIB_PURE
    intrank_t from_world(intrank_t rank, intrank_t otherwise) const {
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      UPCXX_ASSERT(rank >= 0 && rank < upcxx::rank_n(), 
                   "team::from_world(rank, otherwise) requires rank in [0, world().rank_n()-1] == [0, " << upcxx::rank_n()-1 << "], but given: " << rank);
      return backend::team_rank_from_world(*this, rank, otherwise);
    }
    
    UPCXXI_ATTRIB_PURE
    intrank_t operator[](intrank_t peer) const {
      UPCXXI_ASSERT_INIT();
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      UPCXX_ASSERT(peer >= 0 && peer < this->rank_n(), 
                   "team[peer_index] requires peer_index in [0, rank_n()-1] == [0, " << this->rank_n()-1 << "], but given: " << peer);
      return backend::team_rank_to_world(*this, peer);
    }
    
    UPCXXI_ATTRIB_PURE
    team_id id() const {
      UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
      return team_id{id_};
    }
    
    static constexpr intrank_t color_none = -0xbad;
    
    team split(intrank_t color, intrank_t key) const;

    ////////////////////////////////////////////////////////////////////////////
    // team::create
    
    team create(detail::internal_only, const gex_EP_Location_t *locs, size_t count) const;

    template<typename Iter>
    team create(Iter cbegin, Iter cend, size_t count) const {
      std::vector<gex_EP_Location_t> locs;

      // optimization: reserve space if we know the requirement in constant time
      if (count) locs.reserve(count);

      // linear pass over the ranks to convert them to GASNet's format
      gex_EP_Location_t loc0;
      loc0.gex_ep_index = 0;
      for ( ; cbegin != cend; cbegin++) {
        loc0.gex_rank = (gex_Rank_t)*cbegin;
        locs.push_back(loc0);
      }

      return this->create(detail::internal_only(), 
                          locs.data(), locs.size());
    }
    template<typename Iter>
    team create(Iter cbegin, Iter cend) const {
      size_t count = 0;
      if (std::is_base_of<std::random_access_iterator_tag, 
                       typename std::iterator_traits<Iter>::iterator_category>::value) 
          count = std::distance(cbegin, cend);

      return this->create(static_cast<Iter&&>(cbegin), static_cast<Iter&&>(cend), count);
    }
    template<typename Container>
    team create(const Container &ranks) const {
      return this->create(ranks.cbegin(), ranks.cend(), ranks.size());
    }
    
    void destroy(entry_barrier eb = entry_barrier::user);
    
    ////////////////////////////////////////////////////////////////////////////
    // internal only
    
    const team_base& base(detail::internal_only) const {
      return *this;
    }
    
    detail::digest next_collective_id(detail::internal_only) {
      return id_.eat(coll_counter_++);
    }

    void destroy(detail::internal_only, entry_barrier eb = entry_barrier::user);

    void invalidate(detail::internal_only);
  };
  
  team& world();
  team& local_team();
  
  namespace detail {
    extern detail::raw_storage<team> the_world_team;
    extern detail::raw_storage<team> the_local_team;    
  }
}
#endif
