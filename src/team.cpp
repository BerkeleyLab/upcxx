#include <upcxx/team.hpp>

#include <upcxx/backend/gasnet/runtime_internal.hpp>

using namespace std;

namespace detail = upcxx::detail;
namespace backend = upcxx::backend;
namespace gasnet = upcxx::backend::gasnet;

using upcxx::team;
using detail::raw_storage;

raw_storage<team> detail::the_world_team;
raw_storage<team> detail::the_local_team;

std::unordered_map<upcxx::detail::digest, void*> upcxx::detail::registry;

team::team(detail::internal_only, backend::team_base &&base, detail::digest id,
           intrank_t n, intrank_t me):
  backend::team_base(std::move(base)),
  id_(id),
  coll_counter_(0),
  n_(n),
  me_(me) {
  
  detail::registry[id_] = this;
}

team::team(team &&that):
  backend::team_base(std::move(that)),
  id_(that.id_),
  coll_counter_(that.coll_counter_),
  n_(that.n_),
  me_(that.me_) {

  UPCXX_ASSERT_INIT();
  UPCXX_ASSERT_MASTER();
  UPCXX_ASSERT((that.id_ != detail::digest{~0ull, ~0ull}));
  
  that.id_ = detail::digest{~0ull, ~0ull}; // the tombstone id value
  
  detail::registry[id_] = this;
}

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

team team::split(intrank_t color, intrank_t key) const {
  UPCXX_ASSERT_INIT();
  UPCXX_ASSERT_MASTER();
  UPCXX_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);
  UPCXX_ASSERT(color >= 0 || color == color_none);
  
  gex_TM_t sub_tm = GEX_TM_INVALID;
  gex_TM_t *p_sub_tm = color == color_none ? nullptr : &sub_tm;
  
  size_t scratch_sz = gex_TM_Split(
    p_sub_tm, gasnet::handle_of(*this),
    color, key,
    nullptr, 0,
    GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED
  );
  
  void *scratch_buf = p_sub_tm
    ? upcxx::allocate(scratch_sz, GASNET_PAGESIZE)
    : nullptr;
  
  gex_TM_Split(
    p_sub_tm, gasnet::handle_of(*this),
    color, key,
    scratch_buf, scratch_sz,
    /*flags*/0
  );
  
  if(p_sub_tm)
    gex_TM_SetCData(sub_tm, scratch_buf);
  
  return team(
      detail::internal_only(),
      backend::team_base{reinterpret_cast<uintptr_t>(sub_tm)},
      const_cast<team*>(this)->next_collective_id(detail::internal_only()).eat(color),
      p_sub_tm ? (intrank_t)gex_TM_QuerySize(sub_tm) : 0,
      p_sub_tm ? (intrank_t)gex_TM_QueryRank(sub_tm) : -1
    );
}

void team::destroy(entry_barrier eb) {
  UPCXX_ASSERT_INIT();
  UPCXX_ASSERT_MASTER();
  UPCXX_ASSERT_COLLECTIVE_SAFE(eb);
  UPCXX_ASSERT(this != &world(),      "team::destroy() is prohibited on team world()");
  UPCXX_ASSERT(this != &local_team(), "team::destroy() is prohibited on the local_team()");

  team::destroy(detail::internal_only(), eb);
}

void team::destroy(detail::internal_only, entry_barrier eb) {
  UPCXX_ASSERT_MASTER();
  
  gex_TM_t tm = gasnet::handle_of(*this);

  if(tm != GEX_TM_INVALID) {
    backend::quiesce(*this, eb);

    void *scratch = gex_TM_QueryCData(tm);

    if (tm != gasnet::handle_of(detail::the_world_team.value())) {
        gex_Memvec_t scratch_area;
        gex_TM_Destroy(tm, &scratch_area, GEX_FLAG_GLOBALLY_QUIESCED);

        if (scratch) UPCXX_ASSERT(scratch == scratch_area.gex_addr);
    }
    
    upcxx::deallocate(scratch);
  }
  
  if(id_ != detail::digest{~0ull, ~0ull})
    detail::registry.erase(id_);
}
