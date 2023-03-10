#include <upcxx/team.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>

using namespace std;

namespace detail = upcxx::detail;
namespace backend = upcxx::backend;
namespace gasnet = upcxx::backend::gasnet;

using upcxx::team;
using detail::raw_storage;
using detail::tombstone;

raw_storage<team> detail::the_world_team;
raw_storage<team> detail::the_local_team;

std::unordered_map<upcxx::detail::digest, void*> upcxx::detail::registry;

GASNETT_COLD
team::team():
  team(detail::internal_only{}, backend::team_base{}, tombstone, 0, -1) {
}

GASNETT_COLD
team::team(detail::internal_only, backend::team_base &&base, detail::digest id,
           intrank_t n, intrank_t me):
  backend::team_base(std::move(base)),
  id_(id),
  coll_counter_(0),
  n_(n),
  me_(me) {
  
  if (id_ == tombstone) { // constructing an invalid team
    UPCXX_ASSERT(n_ == 0 && me_ == -1);
    UPCXX_ASSERT(this->handle == reinterpret_cast<uintptr_t>(GEX_TM_INVALID));
  } else {
    detail::registry[id_] = this;
  }
}

GASNETT_COLD
team::team(team &&that): team() {
  *this = std::move(that);
}

GASNETT_COLD
team& team::operator=(team &&that) {
  if (&that == this) return *this; // see issue 547

  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_MASTER();
  UPCXX_ASSERT(&that != &world(),
               "team world() cannot be passed to move constructor or assignment");
  UPCXX_ASSERT(&that != &local_team(),
               "team local_team() cannot be passed to move constructor or assignment");
  UPCXX_ASSERT(
    !this->is_active(),
    "team move assignment operator requires receiver to be inactive"
  );

  backend::team_base::operator=(std::move(that));
  id_ = that.id_;
  coll_counter_ = that.coll_counter_;
  n_ = that.n_;
  me_ = that.me_;

  that.invalidate(detail::internal_only{});

  if (id_ != tombstone) {
    detail::registry[id_] = this;
  }
  return *this;
}

GASNETT_COLD
team::~team() {
  if(backend::init_count > 0) { // we don't assert on leaks after finalization
    if(this->handle != reinterpret_cast<uintptr_t>(GEX_TM_INVALID)) {
      UPCXX_ASSERT_ALWAYS(
        0 == detail::registry.count(id_),
        "ERROR: team::destroy() must be called collectively before destructor."
      );
    }
  }
}

GASNETT_COLD
team team::split(intrank_t color, intrank_t key) const {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);
  UPCXX_ASSERT(color >= 0 || color == color_none);
  UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");
  
  gex_TM_t sub_tm = GEX_TM_INVALID;
  gex_TM_t *p_sub_tm = color == color_none ? nullptr : &sub_tm;
  
  // query the required scratch size
  size_t scratch_sz = gex_TM_Split(
    p_sub_tm, gasnet::handle_of(*this),
    color, key,
    nullptr, 0,
    GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED
  );
  
  void *scratch_buf = p_sub_tm
    ? gasnet::allocate(scratch_sz, GASNET_PAGESIZE, &gasnet::sheap_footprint_misc)
    : nullptr;
  
  // construct the new GASNet team
  gex_TM_Split(
    p_sub_tm, gasnet::handle_of(*this),
    color, key,
    scratch_buf, scratch_sz,
    /*flags*/0
  );
 
  intrank_t ranks, me;
  detail::digest id = // next_collective_id MUST be called unconditionally
    const_cast<team*>(this)->next_collective_id(detail::internal_only()).eat(color);

  if(p_sub_tm) {
    gex_TM_SetCData(sub_tm, scratch_buf);
    me =    (intrank_t)gex_TM_QueryRank(sub_tm);
    ranks = (intrank_t)gex_TM_QuerySize(sub_tm);
    UPCXX_ASSERT(id_ != tombstone);
  } else { // this process gets an invalid team
    id =    tombstone; 
    me =    -1;
    ranks = 0;
  }
  
  return team( detail::internal_only(),
               backend::team_base{reinterpret_cast<uintptr_t>(sub_tm)},
               id, ranks, me );
}

GASNETT_COLD
team team::create(detail::internal_only, const gex_EP_Location_t *locs, size_t count) const {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);
  UPCXX_ASSERT(is_active(), "function call prohibited on an inactive team");

  #if UPCXXI_ASSERT_ENABLED
    std::stringstream ss;
    std::unordered_set<intrank_t> check_ids;
    intrank_t limit = this->rank_n();
    const char *err = nullptr;
    ss << count << " entries : [ ";
    for (size_t i = 0; i < count; i++) {
      if (i) ss << ", ";
      intrank_t rank = (intrank_t)locs[i].gex_rank;
      UPCXX_ASSERT(locs[i].gex_ep_index == 0);
      ss << rank;
      if (rank < 0 || rank >= limit) err = "rank index out-of-range for parent team";
      else if (check_ids.count(rank)) err = "duplicate rank index";
      else check_ids.insert(rank);
    }
    ss << " ]";
    if (count && !check_ids.count(this->rank_me())) err = "missing self";
    //experimental::say() << "team::create(" << ss.str() << ")";
    if (err) 
      UPCXXI_FATAL_ERROR("Invalid rank list passed to team::create(): "
                         << err << "\n  " << ss.str());
  #endif
 
  gex_TM_t parent_tm = gasnet::handle_of(*this);
  gex_TM_t sub_tm = GEX_TM_INVALID;
  gex_TM_t *p_sub_tm = count > 0 ? &sub_tm : nullptr;

  // query the required scratch size
  size_t scratch_sz = gex_TM_Create(
    nullptr, !!p_sub_tm,
    parent_tm,
    const_cast<gex_EP_Location_t *>(locs), count,
    nullptr, 0,
    GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED | GEX_FLAG_TM_LOCAL_SCRATCH
  );

  void *scratch_buf = p_sub_tm
    ? gasnet::allocate(scratch_sz, GASNET_PAGESIZE, &gasnet::sheap_footprint_misc)
    : nullptr;
 
  // construct the new GASNet team
  gex_TM_Create(
    p_sub_tm, !!p_sub_tm,
    parent_tm,
    const_cast<gex_EP_Location_t *>(locs), count,
    &scratch_buf, scratch_sz,
    GEX_FLAG_TM_LOCAL_SCRATCH
  );

  // world rank of first team member preserves global uniqueness:
  intrank_t r0 = (p_sub_tm ? (*this)[locs->gex_rank]: 0);
  detail::digest id = // next_collective_id MUST be called unconditionally
    const_cast<team*>(this)->next_collective_id(detail::internal_only()).eat(r0);
  
  intrank_t ranks, me;
  if(p_sub_tm) {
    gex_TM_SetCData(sub_tm, scratch_buf);
    me =    (intrank_t)gex_TM_QueryRank(sub_tm);
    UPCXX_ASSERT(gex_TM_QuerySize(sub_tm) == count);
    ranks = count;
    UPCXX_ASSERT(id_ != tombstone);
  } else { // this process gets an invalid team
    id =    tombstone; 
    me =    -1;
    ranks = 0;
  }

  return team( detail::internal_only(),
               backend::team_base{reinterpret_cast<uintptr_t>(sub_tm)},
               id, ranks, me );
}

GASNETT_COLD
void team::destroy(entry_barrier eb) {
  UPCXXI_ASSERT_INIT();
  if (!is_active()) return; // issue 500: ignore destroy of invalid teams
  UPCXXI_ASSERT_MASTER();
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
  UPCXX_ASSERT(this != &world(),      "team::destroy() is prohibited on team world()");
  UPCXX_ASSERT(this != &local_team(), "team::destroy() is prohibited on the local_team()");

  team::destroy(detail::internal_only(), eb);
}

GASNETT_COLD
void team::destroy(detail::internal_only, entry_barrier eb) {
  UPCXXI_ASSERT_MASTER();
  
  gex_TM_t tm = gasnet::handle_of(*this);

  if(tm != GEX_TM_INVALID) {
    backend::quiesce(*this, eb);

    void *scratch = gex_TM_QueryCData(tm);

    if (tm != gasnet::handle_of(detail::the_world_team.value())) {
        gex_Memvec_t scratch_area;
        gex_TM_Destroy(tm, &scratch_area, GEX_FLAG_GLOBALLY_QUIESCED);

        if (scratch) UPCXX_ASSERT(scratch == scratch_area.gex_addr);
    }
    
    gasnet::deallocate(scratch, &gasnet::sheap_footprint_misc);
  }
  
  UPCXX_ASSERT(id_ != tombstone);
  detail::registry.erase(id_);

  invalidate(detail::internal_only{});
}

GASNETT_COLD
void team::invalidate(detail::internal_only) {
  id_ = tombstone;
  handle = reinterpret_cast<uintptr_t>(GEX_TM_INVALID);
  n_ = 0;
  me_ = -1;
}
