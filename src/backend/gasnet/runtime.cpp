#include <upcxx/backend/gasnet/runtime.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#include <upcxx/backend/gasnet/upc_link.h>
#include <upcxx/backend/gasnet/noise_log.hpp>

#include <upcxx/concurrency.hpp>
#include <upcxx/cuda_internal.hpp>
#include <upcxx/hip_internal.hpp>
#include <upcxx/ze_internal.hpp>
#include <upcxx/os_env.hpp>
#include <upcxx/reduce.hpp>
#include <upcxx/team.hpp>
#include <upcxx/copy.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <iomanip>

#include <unistd.h>

namespace backend = upcxx::backend;
namespace detail  = upcxx::detail;
namespace gasnet  = upcxx::backend::gasnet;

using upcxx::intrank_t;
using upcxx::persona;
using upcxx::persona_scope;
using upcxx::progress_level;
using upcxx::team;
using upcxx::team_id;
using upcxx::experimental::say;
using upcxx::experimental::os_env;

using detail::command;
using detail::par_atomic;
using detail::par_mutex;

using backend::persona_state;

using gasnet::handle_cb_queue;
using gasnet::rpc_as_lpc;
using gasnet::bcast_as_lpc;
using gasnet::bcast_payload_header;
using gasnet::noise_log;
using gasnet::sheap_footprint_t;

using namespace std;

////////////////////////////////////////////////////////////////////////

#if UPCXXI_BACKEND_GASNET_SEQ && !GASNET_SEQ
    #error "This backend is gasnet-seq only!"
#endif

#if UPCXXI_BACKEND_GASNET_PAR && !GASNET_PAR
    #error "This backend is gasnet-par only!"
#endif

#if GASNET_SEGMENT_EVERYTHING
    #error "Segment-everything not supported."
#endif

static_assert(
  sizeof(gex_Event_t) == sizeof(uintptr_t),
  "Failed: sizeof(gex_Event_t) == sizeof(uintptr_t)"
);

static_assert(
  sizeof(gex_AM_SrcDesc_t) <= sizeof(uintptr_t),
  "Failed: sizeof(gex_AM_SrcDesc_t) <= sizeof(uintptr_t)"
);

static_assert(
  sizeof(gex_TM_t) == sizeof(uintptr_t),
  "Failed: sizeof(gex_TM_t) == sizeof(uintptr_t)"
);

////////////////////////////////////////////////////////////////////////
// from: upcxx/backend.hpp

int backend::init_count = 0;

intrank_t backend::rank_n = -1;
intrank_t backend::rank_me; // leave undefined so valgrind can catch it.

intrank_t backend::nbrhd_set_size = -1;
intrank_t backend::nbrhd_set_rank = -1;

bool backend::verbose_noise = false;

backend::heap_state *backend::heap_state::heaps[backend::heap_state::max_heaps] = {/*nullptr...*/};
int backend::heap_state::heap_count[2] = { 1, 0 }; // host segment is implicitly idx 0
constexpr int backend::heap_state::max_heaps_cat[2]; // because C++ constexpr rules are stupid

persona backend::master;
persona_scope *backend::initial_master_scope = nullptr;

intrank_t backend::pshm_peer_lb_; // USE NON-UNDERSCORE VERSION: pshm_peer_lb == local_team()[0]
intrank_t backend::pshm_peer_ub;  // 1 + local_team()[local_team()::rank_n()-1]
intrank_t backend::pshm_peer_n;   // local_team::rank_n()

unique_ptr<uintptr_t[/*local_team.size()*/]> backend::pshm_local_minus_remote;
unique_ptr<uintptr_t[/*local_team.size()*/]> backend::pshm_vbase;
unique_ptr<uintptr_t[/*local_team.size()*/]> backend::pshm_size;

////////////////////////////////////////////////////////////////////////
// from: upcxx/backend/gasnet/runtime.hpp

size_t gasnet::am_size_rdzv_cutover;
size_t gasnet::am_size_rdzv_cutover_local;

sheap_footprint_t gasnet::sheap_footprint_rdzv;
sheap_footprint_t gasnet::sheap_footprint_misc;
sheap_footprint_t gasnet::sheap_footprint_user;
  
#if UPCXXI_BACKEND_GASNET_SEQ
  handle_cb_queue gasnet::master_hcbs;
#endif

////////////////////////////////////////////////////////////////////////

namespace {
  // List of {vbase, peer} pairs (in seperate arrays) sorted by `vbase`, where
  // `vbase` is the local virt-address base for peer segments and `peer` is the
  // local peer index owning that segment.
  unique_ptr<uintptr_t[/*local_team.size()*/]> pshm_owner_vbase;
  unique_ptr<intrank_t[/*local_team.size()*/]> pshm_owner_peer;

  #if UPCXXI_BACKEND_GASNET_SEQ
    // Set by the thread which initiates gasnet since in SEQ only that thread
    // may invoke gasnet.
    void *gasnet_seq_thread_id = nullptr;
  #else
    // unused
    constexpr void *gasnet_seq_thread_id = nullptr;
  #endif

  bool oversubscribed;
  
  auto operation_cx_as_internal_future =
    upcxx::detail::operation_cx_as_internal_future_t{{}};

  void quiesce_rdzv(bool in_finalize, noise_log&);
}

////////////////////////////////////////////////////////////////////////

namespace {
  // we statically allocate the top of the AM handler space, 
  // to improve interoperability with UPCR that uses the bottom
  #define UPCXXI_NUM_AM_HANDLERS 8
  #define UPCXXI_AM_INDEX_BASE   (256 - UPCXXI_NUM_AM_HANDLERS)
  enum {
    id_am_eager_restricted = UPCXXI_AM_INDEX_BASE,
    id_am_eager_master,
    id_am_eager_persona,
    id_am_bcast_master_eager,
    id_am_long_master_packed_cmd,
    id_am_long_master_payload_part,
    id_am_long_master_cmd_part,
    id_am_reply_cb,
    _id_am_endpost
  };
  static_assert(UPCXXI_AM_INDEX_BASE >= GEX_AM_INDEX_BASE, "Incorrect UPCXXI_AM_INDEX_BASE");
  static_assert((int)_id_am_endpost - UPCXXI_AM_INDEX_BASE == UPCXXI_NUM_AM_HANDLERS, "Incorrect UPCXXI_NUM_AM_HANDLERS");
    
  void am_eager_restricted(gex_Token_t, void *buf, size_t buf_size, gex_AM_Arg_t buf_align);
  void am_eager_master(gex_Token_t, void *buf, size_t buf_size, gex_AM_Arg_t buf_align_and_level);
  void am_eager_persona(gex_Token_t, void *buf, size_t buf_size, gex_AM_Arg_t buf_align_and_level,
                        gex_AM_Arg_t persona_ptr_lo, gex_AM_Arg_t persona_ptr_hi);

  void am_bcast_master_eager(gex_Token_t, void *buf, size_t buf_size, gex_AM_Arg_t buf_align_and_level);

  void am_long_master_packed_cmd(gex_Token_t,
    void *payload, size_t payload_size,
    gex_AM_Arg_t reply_cb_lo, gex_AM_Arg_t reply_cb_hi,
    gex_AM_Arg_t cmd_size_align15_level1,
    gex_AM_Arg_t cmd0, gex_AM_Arg_t cmd1, gex_AM_Arg_t cmd2, gex_AM_Arg_t cmd3,
    gex_AM_Arg_t cmd4, gex_AM_Arg_t cmd5, gex_AM_Arg_t cmd6, gex_AM_Arg_t cmd7,
    gex_AM_Arg_t cmd8, gex_AM_Arg_t cmd9, gex_AM_Arg_t cmd10, gex_AM_Arg_t cmd11,
    gex_AM_Arg_t cmd12);

  void am_long_master_payload_part(gex_Token_t,
    void *payload_part, size_t payload_part_size,
    gex_AM_Arg_t nonce,
    gex_AM_Arg_t cmd_size,
    gex_AM_Arg_t cmd_align_level1,
    gex_AM_Arg_t reply_cb_lo, gex_AM_Arg_t reply_cb_hi);

  void am_long_master_cmd_part(gex_Token_t,
    void *cmd_part, size_t cmd_part_size,
    gex_AM_Arg_t nonce,
    gex_AM_Arg_t cmd_size,
    gex_AM_Arg_t cmd_align_level1,
    gex_AM_Arg_t cmd_part_offset);
  
  void am_reply_cb(gex_Token_t, gex_AM_Arg_t cb_lo, gex_AM_Arg_t cb_hi);
  
  #define AM_ENTRY(name, arg_n) \
    {id_##name, (void(*)())name, GEX_FLAG_AM_MEDIUM | GEX_FLAG_AM_REQUEST, arg_n, nullptr, #name}

  #define AM_LONG_ENTRY(name, arg_n) \
    {id_##name, (void(*)())name, GEX_FLAG_AM_LONG | GEX_FLAG_AM_REQUEST, arg_n, nullptr, #name}
  
  gex_AM_Entry_t am_table[] = {
    AM_ENTRY(am_eager_restricted, 1),
    AM_ENTRY(am_eager_master, 1),
    AM_ENTRY(am_eager_persona, 3),
    AM_ENTRY(am_bcast_master_eager, 1),
    AM_LONG_ENTRY(am_long_master_packed_cmd, 16),
    AM_LONG_ENTRY(am_long_master_payload_part, 5),
    AM_ENTRY(am_long_master_cmd_part, 4),
    {id_am_reply_cb, (void(*)())am_reply_cb, GEX_FLAG_AM_SHORT | GEX_FLAG_AM_REPLY, 2, nullptr, "id_am_reply_cb"}
  };
}

////////////////////////////////////////////////////////////////////////

namespace {
  template<typename T>
  gex_AM_Arg_t am_arg_encode_ptr_lo(T *p) {
    intptr_t i = reinterpret_cast<intptr_t>(p);
    return gex_AM_Arg_t(i & 0xffffffffu);
  }
  template<typename T>
  gex_AM_Arg_t am_arg_encode_ptr_hi(T *p) {
    intptr_t i = reinterpret_cast<intptr_t>(p);
    return gex_AM_Arg_t(i >> 31 >> 1);
  }
  template<typename T>
  T* am_arg_decode_ptr(gex_AM_Arg_t lo, gex_AM_Arg_t hi) {
    // Reconstruct a pointer from two gex_AM_Arg_t. The high
    // bits (per_hi) can be safely upshifted into place, on a 32-bit
    // system the result will just be zero. The low bits (per_lo) must
    // not be permitted to sign-extend. Masking against 0xf's achieves
    // this because all literals are non-negative. So the result of the
    // AND could either be signed or unsigned depending on if the mask
    // (a positive value) can be expressed in the desitination signed
    // type (intptr_t).
    intptr_t i = (intptr_t(hi)<<31<<1) | (intptr_t(lo) & 0xffffffffu);
    return reinterpret_cast<T*>(i);
  }

  GASNETT_USED // avoid an unused warning from (at least) PGI
  gex_AM_Arg_t am_arg_encode_i64_lo(int64_t i) {
    return gex_AM_Arg_t(i & 0xffffffffu);
  }
  GASNETT_USED // avoid an unused warning from (at least) PGI
  gex_AM_Arg_t am_arg_encode_i64_hi(int64_t i) {
    return gex_AM_Arg_t(i >> 31 >> 1);
  }
  GASNETT_USED // avoid an unused warning from (at least) PGI
  int64_t am_arg_decode_i64(gex_AM_Arg_t lo, gex_AM_Arg_t hi) {
    return (int64_t(hi)<<31<<1) | (int64_t(lo) & 0xffffffffu);
  }
}

////////////////////////////////////////////////////////////////////////
// shared heap management

#include <upcxx/dl_malloc.h>

void upcxx::backend::heap_state::init() {

}


namespace {
  gex_TM_t world_tm;
  gex_TM_t local_tm;
  gex_EP_t endpoint0;

  detail::par_mutex segment_lock_;
  mspace segment_mspace_;

  // scratch space for the local_team, if required
  size_t local_scratch_sz = 0;
  void  *local_scratch_ptr = nullptr;

  bool use_upc_alloc = true;
  bool upc_heap_coll = false;

  bool   shared_heap_isinit = false;
  void  *shared_heap_base = nullptr;
  size_t shared_heap_sz = 0;

  GASNETT_COLD
  void heap_init_internal(size_t &size, noise_log &noise) {
    UPCXX_ASSERT_ALWAYS(!shared_heap_isinit);

    void *segment_base = 0;
    size_t segment_size = 0;
    gex_Event_Wait(
      gex_EP_QueryBoundSegmentNB(
        world_tm, backend::rank_me, &segment_base, nullptr, &segment_size, 0
      )
    );

    if (upcxxi_upc_is_linked()) {
      static bool firstcall = true;
      if (firstcall) {
        firstcall = false;
        // UPCXX_USE_UPC_ALLOC enables the use of the UPC allocator to replace our allocator
        use_upc_alloc = os_env<bool>("UPCXX_USE_UPC_ALLOC" , (upcxxi_upc_is_pthreads() || use_upc_alloc));
        if (upcxxi_upc_is_pthreads() && !use_upc_alloc) {
          noise.warn() << "UPCXX_USE_UPC_ALLOC=no is not supported in UPC -pthreads mode. Forcing UPCXX_USE_UPC_ALLOC=yes";
          use_upc_alloc = 1;
        }
        if (!use_upc_alloc) {
          // UPCXX_UPC_HEAP_COLL: selects the use of the collective or non-collective UPC shared heap to host the UPC++ allocator
          upc_heap_coll = os_env<bool>("UPCXX_UPC_HEAP_COLL" , upc_heap_coll);
        }
      }
      if (local_scratch_sz && !local_scratch_ptr) { 
        // allocate local scratch separately from the heap to ensure it persists
        // we do this before segment allocation to prevent fragmentation issues
        if (upcxxi_upc_is_pthreads()) // cannot use all_alloc with -pthreads
             local_scratch_ptr = upcxxi_upc_alloc(local_scratch_sz);
        else local_scratch_ptr = upcxxi_upc_all_alloc(local_scratch_sz);
      }
      if (use_upc_alloc) {
        shared_heap_base = segment_base;
        size = segment_size;
      } else {
        if (upc_heap_coll) shared_heap_base = upcxxi_upc_all_alloc(size);
        else shared_heap_base = upcxxi_upc_alloc(size);
      }
    } else { // stand-alone UPC++
      use_upc_alloc = false;
      size = segment_size;
      shared_heap_base = segment_base;
    }
    shared_heap_sz = size;

    if (!use_upc_alloc) {
      // init dlmalloc to run over our piece of the segment
      segment_mspace_ = create_mspace_with_base(shared_heap_base, shared_heap_sz, 0);
      // ensure dlmalloc never tries to mmap anything from the system
      mspace_set_footprint_limit(segment_mspace_, shared_heap_sz);
    }

    // zero shared heap footprint counters
    gasnet::sheap_footprint_rdzv = {0,0};
    gasnet::sheap_footprint_misc = {0,0};
    gasnet::sheap_footprint_user = {0,0};    
    
    if(backend::verbose_noise) {
      uint64_t maxsz = shared_heap_sz;
      uint64_t minsz = shared_heap_sz;
      gex_Event_Wait(gex_Coll_ReduceToOneNB(world_tm, 0, &maxsz, &maxsz, GEX_DT_U64, sizeof(maxsz), 1, GEX_OP_MAX, 0,0,0));
      gex_Event_Wait(gex_Coll_ReduceToOneNB(world_tm, 0, &minsz, &minsz, GEX_DT_U64, sizeof(minsz), 1, GEX_OP_MIN, 0,0,0));
      noise.line()
        <<"Shared heap statistics:\n"
        << "  max size: 0x" << std::hex << maxsz << std::dec << " (" << noise_log::size(maxsz) << ")\n"
        << "  min size: 0x" << std::hex << minsz << std::dec << " (" << noise_log::size(minsz) << ")\n"
        << "  P0 base:  " << shared_heap_base;
    }
    shared_heap_isinit = true;

    if (!upcxxi_upc_is_linked()) {
      if (local_scratch_sz && !local_scratch_ptr) { 
        local_scratch_ptr = gasnet::allocate(local_scratch_sz, GASNET_PAGESIZE, &gasnet::sheap_footprint_misc);
        UPCXX_ASSERT_ALWAYS(local_scratch_ptr);
      }
    }

    backend::heap_state::init();
  }
  void init_localheap_tables(void);
}

// WARNING: This is not a documented or supported entry point, and may soon be removed!!
// void upcxx::experimental::destroy_heap(void):
//
// Precondition: The shared heap is a live state, either by virtue
// of library initialization, or a prior call to upcxx::restore_heap.
// Calling thread must have the master persona.
//
// This collective call over all processes enforces an user-level entry barrier,
// and then destroys the entire shared heap. Behavior is undefined if any
// live objects remain in the shared heap at the time of destruction -
// this includes shared objects allocated directly by the application 
// (ie via upcxx::new* and upcxx::allocate* calls), and those allocated 
// indirectly on its behalf by the runtime. The list of library operations 
// that may indirectly allocate shared objects and their ensuing lifetime
// is implementation-defined.
//
// After this call, the shared heap of all processes are in a dead state.
// While dead, any calls to library functions that trigger shared object
// creation have undefined behavior. The list of such functions is
// implementation-defined.

GASNETT_COLD
void upcxx::experimental::destroy_heap() {
  noise_log noise("upcxx::destroy_heap()");
  
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXX_ASSERT_ALWAYS(shared_heap_isinit);
  backend::quiesce(upcxx::world(), entry_barrier::user);

  quiesce_rdzv(/*in_finalize=*/false, noise);
  
  gex_Event_Wait(gex_Coll_BarrierNB( gasnet::handle_of(upcxx::world()), 0));
  
  if(gasnet::sheap_footprint_user.count != 0)
    noise.warn()<<"destroy_heap() called with "<<gasnet::sheap_footprint_user.count<<" live shared objects.";

  if (use_upc_alloc) { 
    noise.warn()<<"destroy_heap() is not supported for UPCXX_USE_UPC_ALLOC=yes" << endl;
  } else {
    destroy_mspace(segment_mspace_);
    segment_mspace_ = 0;

    if (upcxxi_upc_is_linked()) {
      if (upc_heap_coll) upcxxi_upc_all_free(shared_heap_base);
      else upcxxi_upc_free(shared_heap_base);
      shared_heap_base = nullptr;
    }
  }

  shared_heap_isinit = false;

  noise.show();
}

// void upcxx::experimental::restore_heap(void):
//
// Precondition: The shared heap is a dead state, due to a prior call to upcxx::destroy_heap.
// Calling thread must have the master persona.
//
// This collective call over all processes re-initializes the shared heap of 
// all processes, returning them to a live state.

GASNETT_COLD
void upcxx::experimental::restore_heap(void) {
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXX_ASSERT_ALWAYS(!shared_heap_isinit);
  UPCXX_ASSERT_ALWAYS(shared_heap_sz > 0);

  if (use_upc_alloc) {
    // unsupported/ignored
  } else {
    noise_log mute = noise_log::muted();
    heap_init_internal(shared_heap_sz, mute);
    init_localheap_tables();
  }
  shared_heap_isinit = true;

  gex_Event_Wait(gex_Coll_BarrierNB( gasnet::handle_of(upcxx::world()), 0));
}

////////////////////////////////////////////////////////////////////////
// from: upcxx/backend.hpp

GASNETT_COLD
void upcxx::init() {
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::none);
  if(0 != backend::init_count++)
    return;

  static int first_init = 1;
  if (!first_init) 
    UPCXXI_FATAL_ERROR("This implementation does not currently support re-initialization "
      "of the UPC++ library after it has been completely finalized in a given process.\n\n"
      "If this capability is important to you, please contact us!");
  first_init = 0;

  noise_log noise("upcxx::init()");
  
  int ok;

  #if UPCXXI_BACKEND_GASNET_SEQ
    gasnet_seq_thread_id = upcxx::detail::thread_id();
  #endif
  detail::persona_tls &tls = detail::the_persona_tls;
  tls.is_primordial_thread = true;

  gex_Client_t client;
  gex_Segment_t segment;

  if (upcxxi_upc_is_linked()) {
    upcxxi_upc_init(&client, &endpoint0, &world_tm);
  } else { 
    // issue 419: we clear init across gex_Client_Init so that any processes forked 
    // inside this call don't appear to have an initialized UPC++ library, unless they 
    // return from this call and finish upcxx::init()
    backend::init_count = 0; 
    ok = gex_Client_Init(&client, &endpoint0, &world_tm, "upcxx", nullptr, nullptr, 0);
    UPCXX_ASSERT_ALWAYS(ok == GASNET_OK);
    UPCXX_ASSERT_ALWAYS(gex_EP_QueryIndex(endpoint0) == 0);
    backend::init_count = 1;
  }

  backend::rank_n = gex_TM_QuerySize(world_tm);
  backend::rank_me = gex_TM_QueryRank(world_tm);
  
  // issue 100: hook up GASNet envvar services
  detail::getenv = ([](const char *key){ return gasnett_getenv(key); });
  detail::getenv_report = ([](const char *key, const char *val, bool is_dflt){ 
                                      gasnett_envstr_display(key,val,is_dflt); });
  
  backend::verbose_noise = os_env<bool>("UPCXX_VERBOSE", false);

  backend::gasnet::watermark_init();

  //////////////////////////////////////////////////////////////////////////////
  // issue 495: forbid GASNet-level progress threads in SEQ mode
  #if UPCXXI_BACKEND_GASNET_SEQ && GASNET_HIDDEN_AM_CONCURRENCY_LEVEL 
    // static information indicates GASNet MIGHT have a progress thread
    // GASNet 2021.8.7 added a dynamic query
    bool maybe_progress_thread = bool(gex_System_QueryHiddenAMConcurrencyLevel());

    if (maybe_progress_thread) 
      UPCXXI_FATAL_ERROR(
        "UPC++ currently does not support GASNet-level progress threads when using threadmode=seq, "
        "and this unsupported feature appears to be enabled.\n"
        "Please see GASNet documentation for configure or envvar knobs to disable that feature.\n"
        "For details, see issue 495."
      );
  #endif
  //////////////////////////////////////////////////////////////////////////////
  // UPCXX_SHARED_HEAP_SIZE environment handling

  // Determine a bound on the max usable shared segment size
  size_t gasnet_max_segsize = gasnet_getMaxLocalSegmentSize();
  if (upcxxi_upc_is_linked()) {
    if (!backend::rank_me && os_env<bool>("UPCXX_WARN_UPC", true)) {
      say() << "WARNING: Integration with Berkeley UPC is now deprecated and may be removed in a future UPC++ release. "
            << "This warning may be silenced by setting envvar: UPCXX_WARN_UPC=0";
    }
    gasnet_max_segsize = gasnet_getMaxGlobalSegmentSize();
    size_t upc_segment_pad = 16*1024*1024; // TODO: replace this hack
    UPCXX_ASSERT_ALWAYS(gasnet_max_segsize > upc_segment_pad);
    gasnet_max_segsize -= upc_segment_pad;
  }

  const char *segment_keyname = "UPCXX_SHARED_HEAP_SIZE";
  { // Accept UPCXX_SEGMENT_MB for backwards compatibility:
    const char *old_keyname = "UPCXX_SEGMENT_MB";
    if (!detail::getenv(segment_keyname) && detail::getenv(old_keyname)) segment_keyname = old_keyname;
  }
  bool use_max = false;
  { // Accept m/MAX/i to request the largest available segment (limited by GASNET_MAX_SEGSIZE)
    const char *val = detail::getenv(segment_keyname);
    if (val) {
      std::string maxcheck = val;
      std::transform( maxcheck.begin(), maxcheck.end(), maxcheck.begin(),
                  [](unsigned char c) { return std::toupper(c); }); 
      use_max = (maxcheck == "MAX");
    }
  }

  size_t segment_size = 0;
  if (use_max) {
    segment_size = gasnet_max_segsize;
    gasnett_envint_display(segment_keyname, segment_size, 0, 1);
  } else {
    int64_t szval = os_env(segment_keyname, 128<<20, 1<<20); // default units = MB
    int64_t minheap = 2*GASNET_PAGESIZE; // space for local scratch and heap
    UPCXX_ASSERT_ALWAYS(szval >= minheap, segment_keyname << " too small!");
    segment_size = szval;
  }
  // page align: page size should always be a power of 2
  segment_size = (segment_size + GASNET_PAGESIZE-1) & (size_t)-GASNET_PAGESIZE;

  // now adjust the segment size if it's less than the GASNET_MAX_SEGSIZE
  if (segment_size > gasnet_max_segsize) {
    noise.warn() <<
      "Requested UPC++ shared heap size (" << noise_log::size(segment_size) << ") "
      "is larger than the GASNet segment size (" << noise_log::size(gasnet_max_segsize) << "). "
      "Adjusted shared heap size to " << noise_log::size(gasnet_max_segsize) << ".";

    segment_size = gasnet_max_segsize;
  }

  // ready master persona
  backend::initial_master_scope = new persona_scope(backend::master);
  UPCXXI_ASSERT_ALWAYS_MASTER();
  
  // Build team upcxx::world()
  ::new(detail::the_world_team.raw()) upcxx::team(
    detail::internal_only(),
    backend::team_base{reinterpret_cast<uintptr_t>(world_tm)},
    detail::digest{0x1111111111111111, 0x1111111111111111},
    backend::rank_n, backend::rank_me
  );
  
  // Create the GEX segment
  if (upcxxi_upc_is_linked()) {
    if(backend::verbose_noise)
      noise.line() << "Activating interoperability support for the Berkeley UPC Runtime.";
  } else {
    ok = gex_Segment_Attach(&segment, world_tm, segment_size);
    UPCXX_ASSERT_ALWAYS(ok == GASNET_OK);
  }

  // AM handler registration
  ok = gex_EP_RegisterHandlers(endpoint0, am_table, sizeof(am_table)/sizeof(am_table[0]));
  UPCXX_ASSERT_ALWAYS(ok == GASNET_OK);

  //////////////////////////////////////////////////////////////////////////////
  // Determine RPC Eager/Rendezvous Threshold
 
  #define UPCXXI_MAX_RPC_AM_ARGS 3
  size_t fpam_medium_size = gex_AM_MaxRequestMedium( world_tm, GEX_RANK_INVALID, GEX_EVENT_NOW, 
                                                     /*flags*/0, UPCXXI_MAX_RPC_AM_ARGS);
  size_t npam_medium_size = gex_AM_MaxRequestMedium( world_tm, GEX_RANK_INVALID, GEX_EVENT_NOW, 
                                                     GEX_FLAG_AM_PREPARE_LEAST_ALLOC, UPCXXI_MAX_RPC_AM_ARGS);
  // the two values above are usually the same, this is just for paranoia:
  size_t am_medium_size = std::min(fpam_medium_size, npam_medium_size);
  
  /* 
   * Original default calculcation, though 2020.11.0 (comments added reflect that version)
   *
   * gasnet::am_size_rdzv_cutover =
   *   am_medium_size < 1<<10 ? 256 : // no current conduits with default configure args
   *   am_medium_size < 8<<10 ? 512 : // ucx-conduit and aries-conduit default (both around 4KB)
   *                            1024; // all other conduits
   *
   * Original Rationale: 
   * (The following misleading statement is based on a misunderstanding of how
   * AMs are actually implemented in most conduits, and should mostly be ignored)
   *
   * This default was pulled this from thin air. We want to lean towards only
   * sending very small messages eagerly so as not to clog the landing
   * zone, which would force producers to block on their next send. By
   * using a low threshold for rendezvous we increase the probability
   * of there being enough landing space to send the notification of
   * yet another rendezvous. I'm using the max medium size as a heuristic
   * means to approximate the landing zone size. This is not at all
   * accurate, we should be doing something conduit dependent.
   *
   */

  #define ENV_THRESH(var, varname, defaultval) do { \
    var = os_env(varname, defaultval, 1/* units: bytes */); \
    /* enforce limits */ \
    if (var < gasnet::am_size_rdzv_cutover_min) { \
      noise.warn() << "Requested "<<varname<<" (" << var \
                   << ") is too small. Raised to minimum value (" << gasnet::am_size_rdzv_cutover_min << ")"; \
      var = gasnet::am_size_rdzv_cutover_min; \
    } \
    if (var > am_medium_size) { \
      noise.warn() << "Requested "<<varname<<" (" << var \
                   << ") is too large. Lowered to current maximum value (" << am_medium_size << ")"; \
      var = am_medium_size; \
    } \
    UPCXX_ASSERT(gasnet::am_size_rdzv_cutover_min <= var); \
  } while(0)

  // compute a default threshold
  // 2020-11: testing across all conduits show that maximizing the eager threshold usually
  //          provides the best microbenchmark performance in practice for network RPC
  #ifndef UPCXXI_RPC_EAGER_THRESHOLD_DEFAULT
    #if GASNET_CONDUIT_UCX
      // except on ucx-conduit which peaks around 2kb
      #define UPCXXI_RPC_EAGER_THRESHOLD_DEFAULT 2048
    #elif GASNET_CONDUIT_ARIES
      // aries maxmedium defaults to ~4k but can be raised higher via configure
      // 2020-11 testing shows the right crossover is around 8kb on knl and haswell
      #define UPCXXI_RPC_EAGER_THRESHOLD_DEFAULT 8192
    #else
      #define UPCXXI_RPC_EAGER_THRESHOLD_DEFAULT am_medium_size
    #endif
  #endif
  ENV_THRESH(gasnet::am_size_rdzv_cutover, "UPCXX_RPC_EAGER_THRESHOLD", 
             std::min(std::size_t(UPCXXI_RPC_EAGER_THRESHOLD_DEFAULT),am_medium_size));

  // optimal PSHM threshold seems to be around 4KB
  #ifndef UPCXXI_RPC_EAGER_THRESHOLD_LOCAL_DEFAULT
    #define UPCXXI_RPC_EAGER_THRESHOLD_LOCAL_DEFAULT 4096
  #endif
  ENV_THRESH(gasnet::am_size_rdzv_cutover_local, "UPCXX_RPC_EAGER_THRESHOLD_LOCAL", 
             std::min(std::size_t(UPCXXI_RPC_EAGER_THRESHOLD_LOCAL_DEFAULT),gasnet::am_size_rdzv_cutover)); 


  //////////////////////////////////////////////////////////////////////////////
  // Determine if we're oversubscribed.
  { 
    gex_Rank_t host_peer_n;
    gex_System_QueryHostInfo(nullptr, &host_peer_n, nullptr);
    bool oversubscribed_default = (int)gasnett_cpu_count() < (int)host_peer_n;
    oversubscribed = os_env<bool>("UPCXX_OVERSUBSCRIBED", oversubscribed_default);

    if(backend::verbose_noise)
      noise.line() << gasnett_cpu_count() 
        << " CPUs " << (oversubscribed ? "ARE" : "ARE NOT")
        << " Oversubscribed: "<<(oversubscribed
        ? "upcxx::progress() may yield to OS"
        : "upcxx::progress() never yields to OS");

    gasnet_set_waitmode(oversubscribed ? GASNET_WAIT_BLOCK : GASNET_WAIT_SPIN);
  }
  
  //////////////////////////////////////////////////////////////////////////////
  // Setup the local-memory neighborhood tables.

  { // setup local_team_position()
    gex_Rank_t set_size = 0, set_rank = (gex_Rank_t)-1;
    gex_System_QueryMyPosition(&set_size, &set_rank, nullptr, nullptr);
    backend::nbrhd_set_size = set_size;  
    backend::nbrhd_set_rank = set_rank;  
    UPCXX_ASSERT_ALWAYS(backend::nbrhd_set_size > 0);
    UPCXX_ASSERT_ALWAYS(backend::nbrhd_set_rank >= 0 && backend::nbrhd_set_rank < backend::nbrhd_set_size); 
  }
  
  gex_RankInfo_t *nbhd;
  gex_Rank_t peer_n, peer_me;
  gex_System_QueryNbrhdInfo(&nbhd, &peer_n, &peer_me);
  UPCXX_ASSERT_ALWAYS(peer_n > 0);
  UPCXX_ASSERT_ALWAYS(peer_me < peer_n);
  void *peer_EP_loc = nullptr;

  // compute local_team membership
  bool contiguous_nbhd = true;
  for(gex_Rank_t p=1; p < peer_n; p++)
    contiguous_nbhd &= (nbhd[p].gex_jobrank == 1 + nbhd[p-1].gex_jobrank);

  bool const local_is_world = ((intrank_t)peer_n == backend::rank_n);
  if (local_is_world) {
    if(backend::verbose_noise)
      noise.line() << "Whole world is in same local team.";
    
    backend::pshm_peer_lb_ = 0;
    backend::pshm_peer_ub = backend::rank_n;
    UPCXX_ASSERT_ALWAYS((intrank_t)peer_n == backend::rank_n);
    UPCXX_ASSERT_ALWAYS((intrank_t)peer_me == backend::rank_me);
    UPCXX_ASSERT_ALWAYS(contiguous_nbhd);
    local_tm = world_tm;
  } else { // !local_is_world
    if(!contiguous_nbhd) {
      #if !UPCXXI_DISCONTIG
        if (!peer_me) {
          UPCXXI_FATAL_ERROR(
            "Two or more processes are co-located in a GASNet neighborhood with discontiguous rank IDs.\n"
            "This usually arises when the job spawner is directed to assign processes\n"
            "to physical nodes in a manner other than a traditional pure-blocked layout.\n"
            "This mode of operation is strongly discouraged for performance reasons,\n"
            "and is prohibited by this build of the UPC++ library.\n\n"
            "Please adjust your job spawning command to select a job layout that\n"
            "consecutively numbers all the processes launched on a given node.\n\n"
            "If you are REALLY sure you want to run with a discontiguous job layout,\n"
            "then you'll need to reconfigure the UPC++ library with option: --enable-discontig-ranks\n"
            "and please contact the UPC++ maintainers to report your interest in this capability!"
          );
        }
      #endif
      // Discontiguous rank-set is collapsed to singleton set of "me"
      backend::pshm_peer_lb_ = backend::rank_me;
      backend::pshm_peer_ub = backend::rank_me + 1;
      peer_n = 1;
      peer_me = 0;
    }
    else {
      // True subset local team
      backend::pshm_peer_lb_ = nbhd[0].gex_jobrank;
      backend::pshm_peer_ub = nbhd[0].gex_jobrank + peer_n;
    }
  }
  backend::pshm_peer_n = peer_n;

  // determine (upper bound on) scratch requirements for local_team
  if (!local_is_world) { // only if we are creating a GEX-level team
    // build local_team membership table, avoiding split comms
    gex_EP_Location_t *peer_ids = new gex_EP_Location_t[peer_n];
    peer_EP_loc = (void *)peer_ids;
    for (gex_Rank_t i = 0; i < peer_n; i++) {
      peer_ids[i].gex_rank = backend::pshm_peer_lb + i;
      peer_ids[i].gex_ep_index = 0;
    }
    
    local_scratch_sz = gex_TM_Create(
      nullptr, 1,
      world_tm,
      peer_ids, peer_n,
      nullptr, 0,
      GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED |
      GEX_FLAG_TM_LOCAL_SCRATCH | GEX_FLAG_RANK_IS_JOBRANK
    );

    UPCXX_ASSERT_ALWAYS(local_scratch_sz);
  }

  // setup shared segment allocator
  heap_init_internal(segment_size, noise);
  
    
  if (!local_is_world) {
    if(backend::verbose_noise) {
      struct local_team_stats {
        int count;
        int min_size, max_size;
        gex_Rank_t min_discontig_rank;
      };
      
      local_team_stats stats = {peer_me == 0 ? 1 : 0, (int)peer_n, (int)peer_n,
                                (contiguous_nbhd ? GEX_RANK_INVALID : backend::rank_me)};
      
      gex_Event_Wait(gex_Coll_ReduceToOneNB(
          world_tm, 0,
          &stats, &stats,
          GEX_DT_USER, sizeof(local_team_stats), 1,
          GEX_OP_USER,
          (gex_Coll_ReduceFn_t)[](const void *arg1, void *arg2_out, std::size_t n, const void*) {
            const auto *in = (local_team_stats*)arg1;
            auto *acc = (local_team_stats*)arg2_out;
            for(std::size_t i=0; i != n; i++) {
              acc[i].count += in[i].count;
              acc[i].min_size = std::min(acc[i].min_size, in[i].min_size);
              acc[i].max_size = std::max(acc[i].max_size, in[i].max_size);
              acc[i].min_discontig_rank = std::min(acc[i].min_discontig_rank, in[i].min_discontig_rank);
            }
          },
          nullptr, 0
        )
      );
      
      noise.line()
        <<"Local team statistics:"<<'\n'
        <<"  local teams = "<<stats.count<<'\n'
        <<"  min rank_n = "<<stats.min_size<<'\n'
        <<"  max rank_n = "<<stats.max_size<<'\n'
        <<"  min discontig_rank = "<<(stats.min_discontig_rank==GEX_RANK_INVALID?"None":to_string(stats.min_discontig_rank));

      if(stats.count == backend::rank_n)
        noise.warn()<<"All local team's are singletons. Memory sharing between ranks will never succeed.";

      if (stats.min_discontig_rank != GEX_RANK_INVALID) 
        noise.warn()<<"One or more processes (including rank " << stats.min_discontig_rank << ")"
          << " are co-located in a GASNet neighborhood with discontiguous rank IDs. "
          << "As a result, these ranks will use a singleton local_team().\n"
          << "This generally arises when the job spawner is directed to assign processes "
          << "to nodes in a manner other than pure-blocked layout.\n"
          << "For details, see issue #438";

    } // verbose_noise
    
    UPCXX_ASSERT_ALWAYS( local_scratch_sz && local_scratch_ptr );

    gex_EP_Location_t *peer_ids = (gex_EP_Location_t *)peer_EP_loc;
    peer_EP_loc = nullptr;
    gex_TM_Create(
      &local_tm, 1, 
      world_tm,
      peer_ids, peer_n,
      &local_scratch_ptr, local_scratch_sz,
      GEX_FLAG_TM_LOCAL_SCRATCH | GEX_FLAG_RANK_IS_JOBRANK
    );
    delete [] peer_ids;

    if (!upcxxi_upc_is_linked()) // UPC mode has custom local_tm scratch cleanup
      gex_TM_SetCData(local_tm, local_scratch_ptr );
  } // !local_is_world
  
  
  // Build upcxx::local_team()
  ::new(detail::the_local_team.raw()) upcxx::team(
    detail::internal_only(),
    backend::team_base{reinterpret_cast<uintptr_t>(local_tm)},
    // we use different digests even if local_tm==world_tm
    (detail::digest{0x2222222222222222, 0x2222222222222222}).eat(backend::pshm_peer_lb),
    peer_n, peer_me
  );
  
  // Setup local peer address translation tables
  init_localheap_tables();

  // Automatically verify segments on init() in debug mode
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
  int16_t default_max_segments = detail::segmap_cache::segment_count() + 256;
  detail::segmap_cache::max_segments_ = std::min<int32_t>(std::max<int32_t>(os_env<int32_t>("UPCXX_CCS_MAX_SEGMENTS", default_max_segments), default_max_segments), std::numeric_limits<int16_t>::max());
  detail::segmap_cache::indexed_segment_starts_ = new std::atomic<std::uintptr_t>[detail::segmap_cache::max_segments_]();
  if (os_env<bool>("UPCXX_CCS_AUTOVERIFY", true))
    detail::segmap_cache::verify_all();
#endif

  noise.show();

  #if UPCXXI_DISCONTIG
    // issue 600: Need to "fix up" backend::nbrhd_set_{size,rank} to accomodate 
    // singleton local_teams arising from discontiguous nbrhd ranks, to ensure
    // correct results are reported by upcxx::local_team_position().
    // Lacking a scan collective there is unfortunately no convenient and scalable way to do this.
    std::vector<intrank_t> local_team_base(backend::rank_n);
    gasnet_coll_gather_all(GASNET_TEAM_ALL, 
                           local_team_base.data(), &backend::pshm_peer_lb_, sizeof(intrank_t), 
                           GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
    intrank_t last_base = -1;
    intrank_t pos = -1;
    for (intrank_t i = 0; i < backend::rank_n; i++) {
      if (local_team_base[i] != last_base) {
        pos++;
        last_base = local_team_base[i];
      }
      if (i == backend::rank_me) {
        UPCXX_ASSERT_ALWAYS(pos >= backend::nbrhd_set_rank);
        backend::nbrhd_set_rank = pos;
      }
    }
    pos++;
    UPCXX_ASSERT_ALWAYS(pos >= backend::nbrhd_set_size);
    backend::nbrhd_set_size = pos;
  #endif

  if (os_env<bool>("UPCXX_VERBOSE_ID", backend::verbose_noise) && peer_me == 0) {
    // output process identity information, for validating job layout matches user intent
    say s(std::cerr,"");
    s << "UPCXX: Process ";
    auto rankw = std::setw(to_string(backend::rank_n-1).size());
    if (backend::nbrhd_set_size == backend::rank_n) { // All singleton local teams
      s << rankw << backend::rank_me << "/" << backend::rank_n;
    } else { // first process in each local_team reports
      if (peer_n > 1) 
        s << rankw << backend::rank_me << "-" << rankw << + std::left << (backend::rank_me+peer_n-1);
      else            
        s << rankw << " " << " " << rankw << backend::rank_me;
      s << "/" << backend::rank_n;
    }
    s << " (local_team: " << peer_n << " rank" << (peer_n>1?"s)":") ")
      << " on " << gasnett_gethostname() << " (" << gasnett_cpu_count() << " processors)";
  }

  //////////////////////////////////////////////////////////////////////////////
  // Exit barrier
  
  gex_Event_Wait(gex_Coll_BarrierNB( gasnet::handle_of(upcxx::world()), 0));
}

namespace {
GASNETT_COLD
void init_localheap_tables(void) {
  const gex_Rank_t peer_n = backend::pshm_peer_n;

  backend::pshm_local_minus_remote.reset(new uintptr_t[peer_n]);
  backend::pshm_vbase.reset(new uintptr_t[peer_n]);
  backend::pshm_size.reset(new uintptr_t[peer_n]);
  pshm_owner_vbase.reset(new uintptr_t[peer_n]);
  pshm_owner_peer.reset(new intrank_t[peer_n]);
  
  for(gex_Rank_t p=0; p < peer_n; p++) {
    char *owner_vbase, *local_vbase;
    void *owner_vbase_vp, *local_vbase_vp;
    uintptr_t size;

    // silence "may be used uninitialized" warnings in the presence of -Wall + LTO
    owner_vbase_vp = local_vbase_vp = 0;
    size = 0;

    gex_Event_Wait( 
      gex_EP_QueryBoundSegmentNB(
        /*team=*/world_tm,
        /*rank=*/backend::pshm_peer_lb + p,
        &owner_vbase_vp, 
        &local_vbase_vp, 
        &size,
        /*flags=*/0
      )
    );
    owner_vbase = reinterpret_cast<char*>(owner_vbase_vp);
    local_vbase = reinterpret_cast<char*>(local_vbase_vp);
    UPCXX_ASSERT_ALWAYS(owner_vbase && local_vbase && size);

    if (upcxxi_upc_is_linked() && !use_upc_alloc) { 
    #if UPCXXI_STRICT_SEGMENT // this logic prevents UPCR shared objects from passing upcxx::try_global_ptr
      // We have the GEX segment info for the local peer, but
      // the UPC++ shared heap is a subset of the GEX segment.
      // Determine the necessary adjustment to locate our shared heap:
      std::pair<uintptr_t, uintptr_t> info(
        reinterpret_cast<char *>(shared_heap_base) - owner_vbase, // base offset on owner
        shared_heap_sz // size
      );
      gex_Event_Wait(gex_Coll_BroadcastNB( local_tm, p, &info, &info, sizeof(info), 0));

      UPCXX_ASSERT_ALWAYS(info.first < size);
      owner_vbase += info.first;
      local_vbase += info.first;
      UPCXX_ASSERT_ALWAYS(info.second <= size);
      size = info.second;
    #endif
    }
    
    backend::pshm_local_minus_remote[p] = reinterpret_cast<uintptr_t>(local_vbase) - reinterpret_cast<uintptr_t>(owner_vbase);
    backend::pshm_vbase[p] = reinterpret_cast<uintptr_t>(local_vbase);
    backend::pshm_size[p] = size;
    
    pshm_owner_peer[p] = p; // initialize peer indices as identity permutation
  }

  // Sort peer indices according to their vbase. We use `std::qsort` instead of
  // `std::sort` because performance is not critical and qsort *hopefully*
  // generates a lot less code in the executable binary.
  std::qsort(
    /*first*/pshm_owner_peer.get(),
    /*count*/peer_n,
    /*size*/sizeof(intrank_t),
    /*compare*/[](void const *pa, void const *pb)->int {
      intrank_t a = *static_cast<intrank_t const*>(pa);
      intrank_t b = *static_cast<intrank_t const*>(pb);

      uintptr_t va = backend::pshm_vbase[a];
      uintptr_t vb = backend::pshm_vbase[b];
      
      return va < vb ? -1 : va == vb ? 0 : +1;
    }
  );

  // permute vbase's into sorted order
  for(gex_Rank_t i=0; i < peer_n; i++)
    pshm_owner_vbase[i] = backend::pshm_vbase[pshm_owner_peer[i]];
}
}

namespace {
  GASNETT_COLD
  void quiesce_rdzv(bool in_finalize, noise_log &noise) {
    int64_t iters = 0;
    int64_t n;

    do {
      n = gasnet::sheap_footprint_rdzv.count;
      if(iters == (in_finalize ? 1000 : 100000)) {
        if(in_finalize) {
          if(upcxx::rank_me()==0)
            noise.warn()<<
            /*> WARN*/"It appears that the application has not quiesced its usage "
            "of upcxx communcation, violating the semantics of upcxx::finalize(), "
            "which may lead to undefined behavior. "
            "upcxx will now attempt to ignore this activity and proceed with finalization.";
          noise.show(); // flush output before potential crash
          return;
        }
        else {
          if(upcxx::rank_me()==0)
            noise.warn()<<
            /*> WARN*/"It appears that the application has not quiesced its usage "
            "of upcxx communication. It is necessary that all internal upcxx buffers "
            "be reclaimed before proceeding, so upcxx shall continue to wait...";
          noise.show(); // don't pause forever without output
        }
      }
      
      gex_Event_t e = gex_Coll_ReduceToAllNB(
        gasnet::handle_of(upcxx::world()),
        &n, &n,
        GEX_DT_I64, sizeof(int64_t), 1,
        GEX_OP_MAX,
        nullptr, nullptr, 0
      );

      do upcxx::progress(progress_level::internal);
      while(0 != gex_Event_Test(e));

      iters += 1;
    } while(n != 0);
  }
}

GASNETT_COLD
void upcxx::finalize() {
  UPCXXI_ASSERT_INIT();
  UPCXXI_ASSERT_ALWAYS_MASTER();
  UPCXXI_ASSERT_COLLECTIVE_SAFE(entry_barrier::user);
  UPCXX_ASSERT_ALWAYS(backend::init_count > 0);
  
  if (backend::init_count > 1) {
    backend::init_count--;
    return;
  } // final decrement is performed at end
  
  noise_log noise("upcxx::finalize()");

  { // barrier
    // DO NOT convert this loop to backend::quiesce() which lacks the desired behavior
    gex_Event_t e = gex_Coll_BarrierNB( gasnet::handle_of(upcxx::world()), 0);
    do {
      // issue 384: ensure we invoke user-level progress at least once during
      // runtime teardown, to harvest any runtime-deferred actions.
      upcxx::progress();
    } while (gex_Event_Test(e) != 0);
  }

  quiesce_rdzv(/*in_finalize=*/true, noise);
  
  struct popn_stats_t {
    int64_t sum, min, max;
  };
  
  auto reduce_popn_to_rank0 = [](int64_t arg)->popn_stats_t {
    // We could use `gex_Coll_ReduceToOne`, but this gives us a test of reductions
    // using our "internal only" completions.
    return upcxx::reduce_one(
        popn_stats_t{arg, arg, arg},
        [](popn_stats_t a, popn_stats_t b)->popn_stats_t {
          return {a.sum + b.sum, std::min(a.min, b.min), std::max(a.max, b.max)};
        },
        /*root=*/0, upcxx::world(),
        operation_cx_as_internal_future
      ).wait_internal(upcxx::detail::internal_only{});
  };
  
  if(backend::verbose_noise) {
    int64_t objs_local = detail::registry.size() - 2; // minus `world` and `local_team`
    popn_stats_t objs = reduce_popn_to_rank0(objs_local);
    
    if(objs.sum != 0) {
      noise.warn()
        <<"Objects remain within registries at finalize (could be teams, "
          "dist_object's, or outstanding collectives).\n"
        <<"  total = "<<objs.sum<<'\n'
        <<"  per rank min = "<<objs.min<<'\n'
        <<"  per rank max = "<<objs.max;
    }
  }
  
  if(backend::verbose_noise) {
    int64_t live_local = gasnet::sheap_footprint_user.count;
    
    #if 0 // local_team scratch is no longer credited as a user allocation
    if(gasnet::handle_of(detail::the_local_team.value()) !=
       gasnet::handle_of(detail::the_world_team.value())
       && !upcxxi_upc_is_linked())
       live_local -= 1; // minus local_team scratch
    #endif
    
    popn_stats_t live = reduce_popn_to_rank0(live_local);
    
    if(live.sum != 0) {
      noise.warn()
        <<"Shared segment allocations live at finalize:\n"
        <<"  total = "<<live.sum<<'\n'
        <<"  per rank min = "<<live.min<<'\n'
        <<"  per rank max = "<<live.max;
    }
  }
  
  { // Tear down local_team
    if(gasnet::handle_of(detail::the_local_team.value()) !=
       gasnet::handle_of(detail::the_world_team.value())) {
       if (upcxxi_upc_is_linked()) {
         // local_tm scratch has special handling in UPC mode
         if (local_scratch_ptr) { 
           if (upcxxi_upc_is_pthreads()) upcxxi_upc_free(local_scratch_ptr);
           else upcxxi_upc_all_free(local_scratch_ptr);
           local_scratch_ptr = nullptr;
         }
       }
    }
    
    detail::the_local_team.value().destroy(detail::internal_only(), entry_barrier::none);
    detail::the_local_team.destruct();
    local_tm = GEX_TM_INVALID;
  }
  
  // can't just destroy world, it needs special attention
  detail::registry.erase(detail::the_world_team.value().id().dig_);
  
  if(backend::initial_master_scope != nullptr)
    delete backend::initial_master_scope;

#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
  delete[] detail::segmap_cache::indexed_segment_starts_;
#endif

  noise.show();
  UPCXX_ASSERT_ALWAYS(backend::init_count == 1);
  backend::init_count = 0;
}

void upcxx::liberate_master_persona() {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &backend::master);
  UPCXX_ASSERT_ALWAYS(backend::initial_master_scope != nullptr);
  
  delete backend::initial_master_scope;
  
  backend::initial_master_scope = nullptr;
}

void* upcxx::allocate(size_t size, size_t alignment) {
  UPCXXI_ASSERT_INIT();
  return gasnet::allocate_or_null(size, alignment, &gasnet::sheap_footprint_user);
}

void  upcxx::deallocate(void *p) {
  UPCXXI_ASSERT_INIT();
  gasnet::deallocate(p, &gasnet::sheap_footprint_user);
}

GASNETT_COLD
std::string upcxx::detail::shared_heap_stats() {
  std::stringstream ss;
  ss
    <<"Local shared heap statistics:\n"
    <<"  Shared heap size on process "<<rank_me()<<":             "
    <<                       noise_log::size(shared_heap_sz) << '\n'
    <<"  User allocations:      "<<setw(10)<<gasnet::sheap_footprint_user.count<<" objects, "
    <<                       noise_log::size(gasnet::sheap_footprint_user.bytes)<<'\n'
    <<"  Internal rdzv buffers: "<<setw(10)<<gasnet::sheap_footprint_rdzv.count<<" objects, "
    <<                       noise_log::size(gasnet::sheap_footprint_rdzv.bytes)<<'\n'
    <<"  Internal misc buffers: "<<setw(10)<<gasnet::sheap_footprint_misc.count<<" objects, "
    <<                       noise_log::size(gasnet::sheap_footprint_misc.bytes)<<'\n';
  return ss.str();
}

int64_t upcxx::shared_segment_size() {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT(shared_heap_isinit);
  return shared_heap_sz;
}

int64_t upcxx::shared_segment_used() {
  UPCXXI_ASSERT_INIT();
  UPCXX_ASSERT(shared_heap_isinit);
  return gasnet::sheap_footprint_user.bytes
       + gasnet::sheap_footprint_rdzv.bytes
       + gasnet::sheap_footprint_misc.bytes;
}
  
void* gasnet::allocate_or_null(size_t size, size_t alignment, sheap_footprint_t *foot) noexcept {
  UPCXX_ASSERT(shared_heap_isinit);
  UPCXXI_ASSERT_MASTER_HELD_IFSEQ();

  std::lock_guard<detail::par_mutex> locked{segment_lock_};
  
  void *p;
  if (use_upc_alloc) {
    // must overallocate and pad to ensure alignment
    UPCXX_ASSERT(alignment < 1U<<31);
    alignment = std::max(alignment, (size_t)16);
    UPCXX_ASSERT((alignment & (alignment-1)) == 0); // assumed to be power-of-two
    uintptr_t base = (uintptr_t)upcxxi_upc_alloc(size+alignment);
    uintptr_t user = (base+alignment) & ~(alignment-1);
    uintptr_t pad = (user - base);
    UPCXX_ASSERT(pad >= 16 && pad <= alignment);
    *(reinterpret_cast<uint64_t*>(user-8)) = pad; // store padding amount in header
    *(reinterpret_cast<uint64_t*>(user-16)) = size+alignment; // store footrpint size in header
    p = reinterpret_cast<void *>(user);

    foot->bytes += size+alignment;
    foot->count += 1;
  } else {
    p = mspace_memalign(segment_mspace_, alignment, size);
    if_pt(p) {
      foot->bytes += mspace_usable_size(p);
      foot->count += 1;
    }
  }

  UPCXX_ASSERT(reinterpret_cast<uintptr_t>(p) % alignment == 0);
  return p;
}

void gasnet::deallocate(void *p, sheap_footprint_t *foot) {
  UPCXX_ASSERT(shared_heap_isinit);
  UPCXXI_ASSERT_MASTER_HELD_IFSEQ();

  std::lock_guard<detail::par_mutex> locked{segment_lock_};
  
  if_pf (!p) return;
  
  if (use_upc_alloc) {
    // parse alignment header to recover original base ptr
    uintptr_t user = reinterpret_cast<uintptr_t>(p);
    UPCXX_ASSERT((user & 0x3) == 0);
    uint64_t pad = *(reinterpret_cast<uint64_t*>(user-8));
    uint64_t foot_size = *(reinterpret_cast<uint64_t*>(user-16));
    uintptr_t base = user-pad;
    upcxxi_upc_free(reinterpret_cast<void *>(base));
    foot->bytes -= foot_size;
    foot->count -= 1;
  } else {
    foot->bytes -= mspace_usable_size(p);
    foot->count -= 1;
    mspace_free(segment_mspace_, p);
  }
}

//////////////////////////////////////////////////////////////////////
// from: upcxx/backend.hpp

void backend::quiesce(const team &tm, upcxx::entry_barrier eb) {
  UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
  switch(eb) {
  case entry_barrier::none:
    break;
  case entry_barrier::internal:
  case entry_barrier::user: {
      // memory fencing is handled inside gex_Coll_BarrierNB + gex_Event_Test
      //std::atomic_thread_fence(std::memory_order_release);
      
      UPCXX_ASSERT(!upcxx::in_progress()); // issue #412 / spec issue 169/185
     
      gex_Event_t e = gex_Coll_BarrierNB( gasnet::handle_of(tm), 0);

      while(0 != gex_Event_Test(e)) {
          UPCXXI_SPINLOOP_HINT();
          upcxx::progress(
            eb == entry_barrier::internal
              ? progress_level::internal
              : progress_level::user
          );
      }
      
      //std::atomic_thread_fence(std::memory_order_acquire);
    } break;
  default:
    UPCXXI_FATAL_ERROR("Invalid entry_barrier value = " << (int)eb);
  }
}

GASNETT_COLD
void backend::warn_empty_rma(const char *fnname) {
  static bool warn = os_env<bool>("UPCXX_WARN_EMPTY_RMA", true);
  if (warn) {
    say() << "WARNING: Issued a zero-length " << fnname << " operation. "
          << "This is semantically permitted, but the implementation is currently sub-optimal. "
          << "If performance of zero-length RMA matters to you, please let us know in issue 484!\n"
          << "This warning is issued at most once, and may be silenced by setting envvar: UPCXX_WARN_EMPTY_RMA=0";
    warn = false;
  }
}

tuple<intrank_t/*rank*/, uintptr_t/*raw*/> backend::globalize_memory(void const *addr) {
  intrank_t peer_n = pshm_peer_n;
  UPCXX_ASSERT(peer_n == pshm_peer_ub - pshm_peer_lb);
  uintptr_t uaddr = reinterpret_cast<uintptr_t>(addr);

  // key is a pointer to one past the last vbase less-or-equal to addr.
  uintptr_t *key = std::upper_bound(
    pshm_owner_vbase.get(),
    pshm_owner_vbase.get() + peer_n,
    uaddr
  );

  int key_ix = key - pshm_owner_vbase.get();

  #define bad_memory "Local memory "<<addr<<" is not in any local rank's shared segment."

  UPCXX_ASSERT(key_ix > 0, bad_memory);
  UPCXX_ASSERT(key_ix <= peer_n);
  
  intrank_t peer = pshm_owner_peer[key_ix-1];
  UPCXX_ASSERT(peer >= 0 && peer < peer_n);

  UPCXX_ASSERT(uaddr - pshm_vbase[peer] <= pshm_size[peer], bad_memory);
  
  return std::make_tuple(
    pshm_peer_lb + peer,
    uaddr - pshm_local_minus_remote[peer]
  );

  #undef bad_memory
}

tuple<intrank_t/*rank*/, uintptr_t/*raw*/>  backend::globalize_memory(
    void const *addr,
    tuple<intrank_t/*rank*/, uintptr_t/*raw*/> otherwise
  ) {
  intrank_t peer_n = pshm_peer_n;
  UPCXX_ASSERT(peer_n == pshm_peer_ub - pshm_peer_lb);
  uintptr_t uaddr = reinterpret_cast<uintptr_t>(addr);

  // key is a pointer to one past the last vbase less-or-equal to addr.
  uintptr_t *key = std::upper_bound(
      pshm_owner_vbase.get(),
      pshm_owner_vbase.get() + peer_n,
      uaddr
    );

  int key_ix = key - pshm_owner_vbase.get();
  
  if(key_ix <= 0)
    return otherwise;
  UPCXX_ASSERT(key_ix <= peer_n);
  
  intrank_t peer = pshm_owner_peer[key_ix-1];
  UPCXX_ASSERT(peer >= 0 && peer < peer_n);

  if(uaddr - pshm_vbase[peer] <= pshm_size[peer])
    return std::make_tuple(
        pshm_peer_lb + peer,
        uaddr - pshm_local_minus_remote[peer]
      );
  else
    return otherwise;
}

intrank_t backend::team_rank_from_world(const team &tm, intrank_t rank) {
  gex_Rank_t got = gex_TM_TranslateJobrankToRank(gasnet::handle_of(tm), rank);
  UPCXX_ASSERT(got != GEX_RANK_INVALID);
  return got;
}

intrank_t backend::team_rank_from_world(const team &tm, intrank_t rank, intrank_t otherwise) {
  gex_Rank_t got = gex_TM_TranslateJobrankToRank(gasnet::handle_of(tm), rank);
  return got == GEX_RANK_INVALID ? otherwise : (intrank_t)got;
}

intrank_t backend::team_rank_to_world(const team &tm, intrank_t peer) {
  return gex_TM_TranslateRankToJobrank(gasnet::handle_of(tm), peer);
}

GASNETT_COLD
void backend::validate_global_ptr(bool allow_null, intrank_t rank, void *raw_ptr, std::uint32_t heap_idx,
                                  memory_kind dynamic_kind, memory_kind Kind, size_t T_align, const char *T_name, 
                                  const char *short_context, const char *context) {
  if_pf (!upcxx::initialized()) return; // don't perform checking before init
  if_pf (!T_name) T_name = "";

  // system sanity checks
  UPCXX_ASSERT_ALWAYS(backend::rank_n > 0);
  UPCXX_ASSERT_ALWAYS(backend::rank_me < backend::rank_n);

  auto pretty_type = [&]() {
    return std::string("global_ptr<") + 
            T_name + ", " + detail::to_string(Kind) + ">";
  };

  bool error = false;
  std::stringstream ss;

  do { // run diagnostics
    bool is_null = !raw_ptr;
      // TODO: this will need adjustment when introducing offset-addressable kinds

    if (is_null) {
      if_pf(heap_idx != 0 || rank != 0) {
        ss << pretty_type() << " representation corrupted, bad null\n";
        error = true; break;
      }

      if_pf (!allow_null) {
        ss << pretty_type() << " may not be null";
        error = true; break;
      }
      break; // end of null pointer checks
    }

    if_pf ((uint64_t)rank >= (uint64_t)backend::rank_n) {
      ss << pretty_type() << " representation corrupted, bad rank\n";
      error = true; break;
    }

    if_pf (
        (Kind == memory_kind::host && heap_idx != 0) // host should always be heap_idx 0
     || (Kind != memory_kind::host && Kind != memory_kind::any && heap_idx == 0) // non-host gptr cannot ref host device
     || (heap_idx >= backend::heap_state::max_heaps) // invalid heap_idx
      ) {
      ss << pretty_type() << " representation corrupted, bad heap_idx\n";
      error = true; break;
    }

    if_pf ( !detail::is_valid_memory_kind(dynamic_kind) || dynamic_kind == memory_kind::any // invalid garbage
         || (Kind != memory_kind::any && dynamic_kind != Kind) // static type mismatch
         || ((dynamic_kind == memory_kind::host) != (heap_idx == 0)) // dynamic_type/heap_idx mismatch
      ) {
      ss << pretty_type() << " representation corrupted, bad dynamic_kind\n";
      error = true; break;
    }

    #ifndef UPCXXI_GPTR_CHECK_SCALE
    #define UPCXXI_GPTR_CHECK_SCALE INT_MAX // unlimited
    #endif
    if (backend::rank_n <= UPCXXI_GPTR_CHECK_SCALE || rank_is_local(rank)) {
      // compute segment bounds
      void *owner_vbase = nullptr;
      size_t size = 0;

      gex_TM_t tm = GEX_TM_INVALID;
      if (heap_idx == 0) { // host memory segment
        if (rank == backend::rank_me // optimization: local device, bounds are trivially available
            && !upcxxi_upc_is_linked()) {  // avoid complications with UPCXX_USE_UPC_ALLOC=0
          owner_vbase = shared_heap_base;
          size = shared_heap_sz;
        } else tm = world_tm; // not this process, ask GASNet
      } else { // device segment
        if (rank == backend::rank_me) { // local device, we have bounds
          backend::heap_state *hs = backend::heap_state::get(heap_idx, true);
          if_pf (!hs || !hs->alloc_base) {
            ss << pretty_type() << " representation corrupted or stale pointer, "
               << "heap_idx does not correspond to an active device segment\n";
            error = true; break;
          }
          if_pf (dynamic_kind != hs->kind()) {
            ss << pretty_type() << " representation corrupted or stale pointer, "
               << "dynamic_kind does not correspond to an active device segment\n";
            error = true; break;
          }
          std::tie(owner_vbase, size) = hs->alloc_base->seg_.segment_range();
          UPCXX_ASSERT(owner_vbase && size);
        }
        else if (detail::native_gex_mk(dynamic_kind)) { // query GEX for remote device EP
          UPCXX_ASSERT(endpoint0 != GEX_EP_INVALID);
          tm = gex_TM_Pair(endpoint0, heap_idx);
        }
      }

      if (tm != GEX_TM_INVALID) {
        // GEX_FLAG_IMMEDIATE ensures correct operation when called in handler context (issue #440),
        // and also avoids introducing communication in our checking logic
        gex_Event_t result = gex_EP_QueryBoundSegmentNB(tm, rank, &owner_vbase, nullptr, &size, GEX_FLAG_IMMEDIATE);
        if (result == GEX_EVENT_NO_OP) { 
          // information not locally available, need to conservatively assume validity
        } else {
          UPCXX_ASSERT(result == GEX_EVENT_INVALID);
          if (!owner_vbase || !size) { // can only be device heap that does not exist
            UPCXX_ASSERT(heap_idx > 0);
            UPCXX_ASSERT(rank != backend::rank_me);
            ss << pretty_type() << " representation corrupted or stale pointer, "
               << "heap_idx does not correspond to an active device segment\n";
            error = true; break;
          }
          UPCXX_ASSERT(owner_vbase && size);
        }
      }

      if (owner_vbase) {
        UPCXX_ASSERT(size);
        void *owner_vlim = (void *)(((char*)owner_vbase) + size - 1);
        if_pf (raw_ptr < owner_vbase || raw_ptr > owner_vlim) {
          const char *seg = (heap_idx ? "device" : "host");
          ss << pretty_type() << " out-of-bounds of " << seg << " segment [" 
             << owner_vbase << ", " << owner_vlim << "]\n";
          error = true; break;
        }
      }
    }

    if (T_align > 1) {
      if_pf ((uintptr_t)raw_ptr % T_align != 0) {
        ss << pretty_type() << " is not properly aligned to a " 
           << T_align << "-byte boundary\n";
        error = true; break;
      }
    }

  } while (0);

  if_pf (error) {
    if (short_context && *short_context) ss << " in " << short_context;
    ss << "\n  rank = " << rank << ", raw_ptr = " << raw_ptr 
       << ", heap_idx = " << heap_idx << ", dynamic_kind = " << detail::to_string(dynamic_kind);
    detail::fatal_error(ss.str(), "fatal global_ptr error", context);
  }
}

////////////////////////////////////////////////////////////////////////
// from: upcxx/backend/gasnet/runtime.hpp

void *gasnet::prepare_npam_medium(
    intrank_t recipient, std::size_t buf_size, 
    int numargs, std::uintptr_t &npam_nonce) {
  UPCXX_ASSERT(numargs >= 0 && numargs <= UPCXXI_MAX_RPC_AM_ARGS);
  UPCXX_ASSERT(buf_size <= gex_AM_MaxRequestMedium(world_tm, recipient, GEX_EVENT_NOW, GEX_FLAG_AM_PREPARE_LEAST_ALLOC, numargs));

  gex_AM_SrcDesc_t sd = 
    gex_AM_PrepareRequestMedium(
      world_tm, recipient,
      /*gex buf*/nullptr, 
      buf_size, buf_size, 
      /*lc_opt*/nullptr, /*flags*/0, numargs);

  UPCXX_ASSERT(sd != GEX_AM_SRCDESC_NO_OP);
  UPCXX_ASSERT(gex_AM_SrcDescSize(sd) >= buf_size);
  void *buf = gex_AM_SrcDescAddr(sd);
  UPCXX_ASSERT(buf);
  npam_nonce = reinterpret_cast<std::uintptr_t>(sd);
  UPCXX_ASSERT(npam_nonce != 0);
  return buf;
}



void gasnet::send_am_eager_restricted(
    intrank_t recipient,
    void *buf,
    std::size_t buf_size,
    std::size_t buf_align,
    std::uintptr_t npam_nonce
  ) {
 
  if (npam_nonce) {
    gex_AM_CommitRequestMedium1(
      reinterpret_cast<gex_AM_SrcDesc_t>(npam_nonce),
      id_am_eager_restricted, buf_size,
      buf_align
    );
  } else { // FPAM
    gex_AM_RequestMedium1(
      world_tm, recipient,
      id_am_eager_restricted, buf, buf_size,
      GEX_EVENT_NOW, /*flags*/0,
      buf_align
    );
  }
  
  after_gasnet();
}

void gasnet::send_am_eager_master(
    progress_level level,
    intrank_t recipient,
    void *buf,
    std::size_t buf_size,
    std::size_t buf_align,
    std::uintptr_t npam_nonce
  ) {
  gex_AM_Arg_t const a0 = buf_align<<1 | (level == progress_level::user ? 1 : 0);
  
  if (npam_nonce) {
    gex_AM_CommitRequestMedium1(
      reinterpret_cast<gex_AM_SrcDesc_t>(npam_nonce),
      id_am_eager_master, buf_size,
      a0
    );
  } else { // FPAM
    gex_AM_RequestMedium1(
      world_tm, recipient,
      id_am_eager_master, buf, buf_size,
      GEX_EVENT_NOW, /*flags*/0,
      a0
    );
  }
  
  after_gasnet();
}

void gasnet::send_am_eager_persona(
    progress_level level,
    intrank_t recipient_rank,
    persona *recipient_persona,
    void *buf,
    std::size_t buf_size,
    std::size_t buf_align,
    std::uintptr_t npam_nonce
  ) {
  gex_AM_Arg_t const a0 = buf_align<<1 | (level == progress_level::user ? 1 : 0);
  gex_AM_Arg_t const a1 = am_arg_encode_ptr_lo(recipient_persona);
  gex_AM_Arg_t const a2 = am_arg_encode_ptr_hi(recipient_persona);

  if (npam_nonce) {
    gex_AM_CommitRequestMedium3(
      reinterpret_cast<gex_AM_SrcDesc_t>(npam_nonce),
      id_am_eager_persona, buf_size,
      a0, a1, a2
    );
  } else { // FPAM
    gex_AM_RequestMedium3(
      world_tm, recipient_rank,
      id_am_eager_persona, buf, buf_size,
      GEX_EVENT_NOW, /*flags*/0,
      a0, a1, a2
    );
  }
  
  after_gasnet();
}

namespace {
  template<typename Fn>
  void rma_get(
      void *buf_d,
      intrank_t rank_s,
      void const *buf_s,
      size_t buf_size,
      Fn fn
    ) {
    
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();

    auto *cb = gasnet::make_handle_cb(std::move(fn));
    
    gex_Event_t h = gex_RMA_GetNB(
      gasnet::handle_of(upcxx::world()),
      buf_d, rank_s, const_cast<void*>(buf_s), buf_size,
      /*flags*/0
    );
    cb->handle = reinterpret_cast<uintptr_t>(h);
    
    gasnet::register_cb(cb);
    gasnet::after_gasnet();
  }
}

void gasnet::send_am_rdzv(
    progress_level level,
    intrank_t rank_d,
    persona *persona_d,
    void *buf_s,
    size_t cmd_size,
    size_t cmd_align
  ) {
  
  intrank_t rank_s = backend::rank_me;
  
  backend::send_am_persona<progress_level::internal>(
    rank_d, persona_d,
    [=]() {
      if(backend::rank_is_local(rank_s)) {
        void *payload = backend::localize_memory_nonnull(rank_s, reinterpret_cast<std::uintptr_t>(buf_s));
        
        rpc_as_lpc *m = new rpc_as_lpc;
        m->payload = payload;
        m->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(rpc_as_lpc::reader_of(m));
        m->vtbl = &m->the_vtbl;
        m->is_rdzv = true;
        m->rdzv_rank_s = rank_s;
        m->rdzv_rank_s_local = true;
        
        auto &tls = detail::the_persona_tls;
        tls.enqueue(*tls.get_top_persona(), level, m, /*known_active=*/std::true_type());
      }
      else {
        rpc_as_lpc *m = rpc_as_lpc::build_rdzv_lz(/*use_sheap=*/false, cmd_size, cmd_align);
        m->rdzv_rank_s = rank_s;
        m->rdzv_rank_s_local = false;
        
        rma_get(
          m->payload, rank_s, buf_s, cmd_size,
          [=]() {
            auto &tls = detail::the_persona_tls;
            int rank_s = m->rdzv_rank_s;
            
            m->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(rpc_as_lpc::reader_of(m));
            tls.enqueue(*tls.get_top_persona(), level, m, /*known_active=*/std::true_type());
            
            // Notify source rank it can free buffer.
            gasnet::send_am_restricted( rank_s,
              [=]() { gasnet::deallocate(buf_s, &gasnet::sheap_footprint_rdzv); }
            );
          }
        );
      }
    }
  );
}

GASNETT_COLD
void gasnet::bcast_am_master_eager(
    progress_level level,
    const upcxx::team &tm,
    intrank_t rank_d_ub, // in range [0, 2*rank_n-1)
    bcast_payload_header *payload,
    size_t cmd_size, size_t cmd_align
  ) {
  
  intrank_t rank_me = tm.rank_me();
  intrank_t rank_n = tm.rank_n();
  
  gex_TM_t tm_gex = handle_of(tm);
  
  // loop over targets
  while(true) {
    intrank_t rank_d_mid = rank_me + 15*int64_t(rank_d_ub - rank_me)/16;
    
    // Send-to-self is stop condition.
    if(rank_d_mid == rank_me)
      break;
    
    intrank_t translate = rank_n <= rank_d_mid ? rank_n : 0;
    
    // Sub-interval bounds. Lower must be in [0,rank_n).
    intrank_t sub_lb = rank_d_mid - translate;
    intrank_t sub_ub = rank_d_ub - translate;
    
    payload->eager_subrank_ub = sub_ub;
    gex_AM_RequestMedium1(
      tm_gex, sub_lb,
      id_am_bcast_master_eager, payload, cmd_size,
      GEX_EVENT_NOW, /*flags*/0,
      cmd_align<<1 | (level == progress_level::user ? 1 : 0)
    );
    
    rank_d_ub = rank_d_mid;
  }
  
  gasnet::after_gasnet();
}

GASNETT_COLD
void gasnet::bcast_am_master_rdzv(
    progress_level level,
    const upcxx::team &tm,
    intrank_t rank_d_ub, // in range [0, 2*rank_n-1)
    intrank_t wrank_owner, // self or a local peer (in world)
    bcast_payload_header *payload_owner, // in owner address space
    bcast_payload_header *payload_sender, // in my address space
    size_t cmd_size,
    size_t cmd_align
  ) {
  
  intrank_t rank_n = tm.rank_n();
  intrank_t rank_me = tm.rank_me();
  intrank_t wrank_sender = backend::rank_me;

  { // precompute number of references to add as num messages to be sent
    int messages = 0;
    intrank_t hi = rank_d_ub;
    
    while(true) {
      intrank_t mid = rank_me + (hi - rank_me)/2;
      // Send-to-self is stop condition.
      if(mid == rank_me)
        break;
      messages += 1;
      hi = mid;
    }

    if(payload_owner == payload_sender) {
      // If we are the owner of the refcount, then nobody else could be concurrently
      // decrementing until we do the message sends. Thus we can increment non-atomically now.
      int64_t refs = payload_sender->rdzv_refs.load(std::memory_order_relaxed);
      refs += messages;
      payload_sender->rdzv_refs.store(refs, std::memory_order_relaxed);

      if(messages == 0) { // leaf of bcast tree
        if(refs == 0) { // no outstanding need of the rdzv buffer
          // This can only happen for a bcast in a singleton team, in which case
          // materializing the am payload was pointless since it is sent to
          // nobody. Seeing this as an unlikely kind of bcast, we will forfeit
          // optimizing out this wasted effort.
          gasnet::deallocate((void*)payload_sender, &gasnet::sheap_footprint_rdzv);
        }
        return;
      }
    }
    else {
      if(messages == 0) // leaf of bcast tree
        return;
      payload_sender->rdzv_refs.fetch_add(messages, std::memory_order_relaxed);
    }
  }
  
  // loop over targets
  while(true) {
    intrank_t rank_d_mid = rank_me + (rank_d_ub - rank_me)/2;
    
    // Send-to-self is stop condition.
    if(rank_d_mid == rank_me)
      break;
    
    intrank_t translate = rank_n <= rank_d_mid ? rank_n : 0;
    
    // Sub-interval bounds. Lower must be in [0,rank_n).
    intrank_t sub_lb = rank_d_mid - translate;
    intrank_t sub_ub = rank_d_ub - translate;
    
    backend::send_am_master<progress_level::internal>(
      backend::team_rank_to_world(tm, sub_lb),
      [=]() {
        if(backend::rank_is_local(wrank_sender)) {
          bcast_payload_header *payload_target =
            (bcast_payload_header*)backend::localize_memory_nonnull(
              wrank_owner, reinterpret_cast<std::uintptr_t>(payload_owner)
            );
          
          detail::serialization_reader r(payload_target);
          r.unplace(detail::storage_size_of<bcast_payload_header>());
          
          bcast_as_lpc *m = new bcast_as_lpc;
          m->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(r);
          m->payload = payload_target;
          m->vtbl = &m->the_vtbl;
          m->is_rdzv = true;
          m->rdzv_rank_s = wrank_owner;
          m->rdzv_rank_s_local = true;

          bcast_am_master_rdzv(
              level, payload_target->tm_id.here(), sub_ub,
              wrank_owner, payload_owner, payload_target,
              cmd_size, cmd_align
            );
          
          auto &tls = detail::the_persona_tls;
          tls.enqueue(*tls.get_top_persona(), level, m, /*known_active=*/std::true_type());
        }
        else {
          bcast_as_lpc *m = rpc_as_lpc::build_rdzv_lz<bcast_as_lpc>(/*use_sheap=*/true, cmd_size, cmd_align);
          m->rdzv_rank_s = wrank_owner;
          m->rdzv_rank_s_local = false;
          
          rma_get(
            m->payload, wrank_owner, payload_owner, cmd_size,
            [=]() {
              intrank_t wrank_owner = m->rdzv_rank_s;
              bcast_payload_header *payload_here = (bcast_payload_header*)m->payload;

              new(&payload_here->rdzv_refs) std::atomic<int64_t>(1); // 1 ref for rpc execution

              {
                detail::serialization_reader r(payload_here);
                r.unplace(detail::storage_size_of<bcast_payload_header>());
                m->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(r);
              }
              
              bcast_am_master_rdzv(
                  level, payload_here->tm_id.here(), sub_ub,
                  backend::rank_me, payload_here, payload_here,
                  cmd_size, cmd_align
                );
              
              auto &tls = detail::the_persona_tls;
              tls.enqueue(*tls.get_top_persona(), level, m, /*known_active=*/std::true_type());
              
              // Notify source rank it can free buffer.
              send_am_restricted( wrank_owner,
                [=]() {
                  if(0 == -1 + payload_owner->rdzv_refs.fetch_add(-1, std::memory_order_acq_rel))
                    gasnet::deallocate(payload_owner, &gasnet::sheap_footprint_rdzv);
                }
              );
            }
          );
        }
      }
    );
    
    rank_d_ub = rank_d_mid;
  }
}

namespace upcxx {
namespace backend {
namespace gasnet {
  namespace {
    template<bool restricted>
    void cleanup_rpc_as_lpc_maybe_rdzv(detail::lpc_base *me1) {
      rpc_as_lpc *me = static_cast<rpc_as_lpc*>(me1);
      
      if(!me->is_rdzv) {
        if(!restricted) std::free(me->payload);
      }
      else {
        if(me->rdzv_rank_s_local) {
          // Notify source rank it can free buffer.
          void *buf_s = reinterpret_cast<void*>(
              backend::globalize_memory_nonnull(me->rdzv_rank_s, me->payload)
            );
           
          send_am_restricted( me->rdzv_rank_s,
            [=]() { gasnet::deallocate(buf_s, &gasnet::sheap_footprint_rdzv); }
          );
          
          delete me;
        }
        else {
          // rpc_as_lpc::build_rdzv_lz(use_sheap=false, ...)
          UPCXX_ASSERT(!( // should not be in segment
            shared_heap_base <= me->payload &&
            (char*)me->payload < (char*)shared_heap_base + shared_heap_sz
          ));
          std::free(me->payload); 
        }
      }
    }
  }
  
  template<>
  void rpc_as_lpc::cleanup</*never_rdzv=*/false, /*restricted=*/false>(detail::lpc_base *me) {
    cleanup_rpc_as_lpc_maybe_rdzv<false>(me);
  }
  
  template<>
  void rpc_as_lpc::cleanup</*never_rdzv=*/false, /*restricted=*/true>(detail::lpc_base *me) {
    cleanup_rpc_as_lpc_maybe_rdzv<true>(me);
  }
  
  template<>
  void bcast_as_lpc::cleanup</*never_rdzv=*/false>(detail::lpc_base *me1) {
    bcast_as_lpc *me = static_cast<bcast_as_lpc*>(me1);
    
    if(!me->is_rdzv) {
      if(0 == --me->eager_refs)
        std::free(me->payload);
    }
    else {
      bcast_payload_header *hdr = (bcast_payload_header*)me->payload;
      int64_t refs_now = -1 + hdr->rdzv_refs.fetch_add(-1, std::memory_order_acq_rel);
      
      if(me->rdzv_rank_s_local) {
        if(0 == refs_now) {
          // Notify source rank it can free buffer.
          void *buf_s = reinterpret_cast<void*>(
              backend::globalize_memory_nonnull(me->rdzv_rank_s, hdr)
            );
          
          send_am_restricted( me->rdzv_rank_s,
            [=]() { gasnet::deallocate(buf_s, &gasnet::sheap_footprint_rdzv); }
          );
        }
        
        delete me;
      }
      else {
        if(0 == refs_now) {
          // rpc_as_lpc::build_rdzv_lz(use_sheap=true, ...)
          UPCXX_ASSERT( // should be in segment
            shared_heap_base <= me->payload &&
            (char*)me->payload < (char*)shared_heap_base + shared_heap_sz
          );
          gasnet::deallocate(me->payload, &gasnet::sheap_footprint_rdzv);
        }
      }
    }
  }
}}}

template<typename RpcAsLpc>
RpcAsLpc* rpc_as_lpc::build_eager(
    void *cmd_buf,
    std::size_t cmd_size,
    std::size_t cmd_alignment
  ) {
  
  UPCXX_ASSERT(cmd_alignment >= alignof(RpcAsLpc));
  std::size_t msg_size = cmd_size;
  msg_size = (msg_size + alignof(RpcAsLpc)-1) & -alignof(RpcAsLpc);
  
  std::size_t msg_offset = msg_size;
  msg_size += sizeof(RpcAsLpc);
  
  void *msg_buf = detail::alloc_aligned(msg_size, cmd_alignment);
  
  if(cmd_buf != nullptr) {
    // NOTE: we CANNOT safely assume the cmd_buf source has any particular alignment
    std::memcpy(msg_buf, cmd_buf, cmd_size);
  }

  char *p = (char*)msg_buf + msg_offset;
  UPCXX_ASSERT(detail::is_aligned(p, alignof(RpcAsLpc)));
  RpcAsLpc *m = ::new(p) RpcAsLpc;
  m->payload = msg_buf;
  m->vtbl = &m->the_vtbl;
  m->is_rdzv = false;
  
  if(cmd_buf != nullptr)
    m->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(RpcAsLpc::reader_of(m));
  
  return m;
}

template<typename RpcAsLpc>
RpcAsLpc* rpc_as_lpc::build_rdzv_lz(
    bool use_sheap,
    std::size_t cmd_size,
    std::size_t cmd_alignment // alignment requirement of packing
  ) {
  std::size_t offset = (cmd_size + alignof(RpcAsLpc)-1) & -alignof(RpcAsLpc);
  std::size_t buf_size = offset + sizeof(RpcAsLpc);
  std::size_t buf_align = std::max(cmd_alignment, alignof(RpcAsLpc));
  
  void *buf;
  if(use_sheap)
    buf = gasnet::allocate(buf_size, buf_align, &gasnet::sheap_footprint_rdzv);
  else
    buf = detail::alloc_aligned(buf_size, buf_align);
  UPCXX_ASSERT_ALWAYS(buf != nullptr);

  char *p = (char*)buf + offset;
  UPCXX_ASSERT(detail::is_aligned(p, alignof(RpcAsLpc)));
  RpcAsLpc *m = ::new(p) RpcAsLpc;
  m->the_vtbl.execute_and_delete = nullptr; // filled in when GET completes
  m->vtbl = &m->the_vtbl;
  m->payload = buf;
  m->is_rdzv = true;
  
  return m;
}

namespace {
  GASNETT_HOT
  void burst_device(persona *per) {
  #if UPCXXI_CUDA_ENABLED
    while(backend::device_cb *cb = per->UPCXXI_INTERNAL_ONLY(device_state_).cuda.cbs.peek()) {
      CUevent hEvent = (CUevent)cb->event;
      if(cuEventQuery(hEvent) == CUDA_SUCCESS) {
        // push onto device free list
        auto st = (backend::cuda_heap_state *)cb->hs;
        { std::lock_guard<detail::par_mutex> g(st->lock);
          st->eventFreeList.push(hEvent);
        }

        per->UPCXXI_INTERNAL_ONLY(device_state_).cuda.cbs.dequeue();
        cb->execute_and_delete();
      }
      else
        break;
    }
  #endif
  #if UPCXXI_HIP_ENABLED
    while(backend::device_cb *cb = per->UPCXXI_INTERNAL_ONLY(device_state_).hip.cbs.peek()) {
      hipEvent_t hEvent = (hipEvent_t)cb->event;
      if(hipEventQuery(hEvent) == hipSuccess) {
        // push onto device free list
        auto st = (backend::hip_heap_state *)cb->hs;
        { std::lock_guard<detail::par_mutex> g(st->lock);
          st->eventFreeList.push(hEvent);
        }

        per->UPCXXI_INTERNAL_ONLY(device_state_).hip.cbs.dequeue();
        cb->execute_and_delete();
      }
      else
        break;
    }
  #endif
  #if UPCXXI_ZE_ENABLED
    while(backend::device_cb *cb = per->UPCXXI_INTERNAL_ONLY(device_state_).ze.cbs.peek()) {
      ze_fence_handle_t hFence = (ze_fence_handle_t)(cb->event);
      ze_result_t fenceQueryResult;
      if ((fenceQueryResult = zeFenceQueryStatus(hFence)) == ZE_RESULT_NOT_READY) break;
      UPCXXI_ZE_CHECK(fenceQueryResult);

      ze_command_list_handle_t hCommandList = (ze_command_list_handle_t)(cb->extra);
      // reset objects for next use
      UPCXXI_ZE_CHECK( zeCommandListReset(hCommandList) );
      UPCXXI_ZE_CHECK( zeFenceReset(hFence) );
      // push onto device free list
      auto st = (backend::ze_heap_state *)cb->hs;
      { std::lock_guard<detail::par_mutex> g(st->lock);
        st->cmdFreeList.emplace(hCommandList, hFence);
      }

      per->UPCXXI_INTERNAL_ONLY(device_state_).ze.cbs.dequeue();
      cb->execute_and_delete();
    }
  #endif
  }
}

GASNETT_HOT
void gasnet::after_gasnet() {
  detail::persona_tls &tls = detail::the_persona_tls;
  
  if(tls.get_progressing() >= 0 || !tls.is_burstable(progress_level::internal))
    return;
  tls.set_progressing((int)progress_level::internal);
  
  int total_exec_n = 0;
  int exec_n;
  
  do {
    exec_n = 0;
    
    tls.foreach_active_as_top([&](persona &p) {
      burst_device(&p);
      
      #if UPCXXI_BACKEND_GASNET_SEQ
        if(&p == &backend::master)
          exec_n += gasnet::master_hcbs.burst(/*spinning=*/false);
      #elif UPCXXI_BACKEND_GASNET_PAR
        exec_n += p.UPCXXI_INTERNAL_ONLY(backend_state_).hcbs.burst(/*spinning=*/false);
      #endif
      
      exec_n += tls.burst_internal(p);
    });
    
    total_exec_n += exec_n;
  }
  while(total_exec_n < 100 && exec_n != 0);
  //while(0);
  
  tls.set_progressing(-1);
}

////////////////////////////////////////////////////////////////////////
// from: upcxx/backend.hpp

int upcxx::detail::progressing() {
  return the_persona_tls.get_progressing();
}

template<upcxx::progress_level level>
static inline void do_progress() {
  detail::persona_tls &tls = detail::the_persona_tls;
  
  if_pf (tls.get_progressing() >= 0) return;
  tls.set_progressing((int)level);
 
  UPCXX_ASSERT(!tls.is_burstable(progress_level::user));
  if (level == progress_level::user)
    tls.flip_burstable(progress_level::user); // enable
  
  int total_exec_n = 0;
  int exec_n;

  UPCXXI_ASSERT_NOEXCEPTIONS_BEGIN
  
  if(!UPCXXI_BACKEND_GASNET_SEQ || gasnet_seq_thread_id == detail::thread_id())
    gasnet_AMPoll();
  
  do {
    exec_n = 0;
    
    tls.foreach_active_as_top([&](persona &p) {
      burst_device(&p);
      
      #if UPCXXI_BACKEND_GASNET_SEQ
        if(&p == &backend::master)
          exec_n += gasnet::master_hcbs.burst(/*spinning=*/true);
      #elif UPCXXI_BACKEND_GASNET_PAR
        exec_n += p.UPCXXI_INTERNAL_ONLY(backend_state_).hcbs.burst(/*spinning=*/true);
      #endif
      
      exec_n += tls.burst_internal(p);
      
      if(level == progress_level::user) {
        tls.flip_burstable(progress_level::user); // disable
        exec_n += tls.burst_user(p);
        tls.flip_burstable(progress_level::user); // enable
      }
    });
    
    total_exec_n += exec_n;
  }
  // Try really hard to do stuff before leaving attentiveness.
  while(total_exec_n < 1000 && exec_n != 0);
  //while(0);
  
  UPCXXI_ASSERT_NOEXCEPTIONS_END

  if(oversubscribed) {
    /* In SMP tests we typically oversubscribe ranks to cpus. This is
     * an attempt at heuristically determining if this rank is just
     * spinning fruitlessly hogging the cpu from another who needs it.
     * It would be a lot more effective if we included knowledge of
     * whether outgoing communication was generated between progress
     * calls, then we would really know that we're just idle. Well,
     * almost. There would still exist the case where this rank is
     * receiving nothing, sending nothing, but is loaded with compute
     * and is only periodically progressing to be "nice".
     */
    static __thread int consecutive_nothings = 0;
    
    if(total_exec_n != 0)
      consecutive_nothings = 0;
    else if(++consecutive_nothings == 10) {
      gasnett_sched_yield();
      consecutive_nothings = 0;
    }
  }
  
  if(level == progress_level::user)
    tls.flip_burstable(progress_level::user); // disable
  tls.set_progressing(-1);
}

GASNETT_HOT
void upcxx::detail::progress_user() {
  do_progress<progress_level::user>();
}
GASNETT_HOT
void upcxx::detail::progress_internal() {
  do_progress<progress_level::internal>();
}

////////////////////////////////////////////////////////////////////////
// anonymous namespace

namespace {
  void am_eager_restricted(
      gex_Token_t,
      void *buf, size_t buf_size,
      gex_AM_Arg_t buf_align
    ) {

    void *tmp;
    if(0 == (reinterpret_cast<uintptr_t>(buf) & (buf_align-1)))
      tmp = buf;
    else {
      tmp = detail::alloc_aligned(buf_size, buf_align);
      std::memcpy((void**)tmp, (void**)buf, buf_size);
    }

    gasnet::rpc_as_lpc dummy;
    dummy.payload = tmp;
    dummy.is_rdzv = false;
    command<detail::lpc_base*>::get_executor(detail::serialization_reader(tmp))(&dummy);

    if(tmp != buf)
      std::free(tmp);
  }
  
  void am_eager_master(
      gex_Token_t,
      void *buf, size_t buf_size,
      gex_AM_Arg_t buf_align_and_level
    ) {
    
    UPCXX_ASSERT(backend::rank_n != -1);
    
    size_t buf_align = buf_align_and_level>>1;
    bool level_user = buf_align_and_level & 1;
    
    rpc_as_lpc *m = rpc_as_lpc::build_eager(buf, buf_size, buf_align);
    
    detail::persona_tls &tls = detail::the_persona_tls;
    
    tls.enqueue(
      backend::master,
      level_user ? progress_level::user : progress_level::internal,
      m,
      /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
    );
  }
  
  void am_eager_persona(
      gex_Token_t,
      void *buf, size_t buf_size,
      gex_AM_Arg_t buf_align_and_level,
      gex_AM_Arg_t per_lo,
      gex_AM_Arg_t per_hi
    ) {
    
    UPCXX_ASSERT(backend::rank_n != -1);
    
    size_t buf_align = buf_align_and_level>>1;
    bool level_user = buf_align_and_level & 1;
    
    persona *per;
    if(per_lo & 0x1) // low bit used to discriminate persona** vs persona*
      per = *am_arg_decode_ptr<persona*>(per_lo ^ 0x1, per_hi);
    else
      per = am_arg_decode_ptr<persona>(per_lo, per_hi);
    
    per = per == nullptr ? &backend::master : per; 
    
    rpc_as_lpc *m = rpc_as_lpc::build_eager(buf, buf_size, buf_align);
    
    detail::persona_tls &tls = detail::the_persona_tls;
    
    tls.enqueue(
      *per,
      level_user ? progress_level::user : progress_level::internal,
      m,
      /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
    );
  }
  
  void am_bcast_master_eager(
      gex_Token_t,
      void *buf, size_t buf_size,
      gex_AM_Arg_t buf_align_and_level
    ) {
    using gasnet::bcast_as_lpc;
    
    size_t buf_align = buf_align_and_level>>1;
    bool level_user = buf_align_and_level & 1;
    progress_level level = level_user ? progress_level::user : progress_level::internal;
    
    bcast_as_lpc *m = rpc_as_lpc::build_eager<bcast_as_lpc>(buf, buf_size, buf_align);
    m->eager_refs = 2;
    
    detail::persona_tls &tls = detail::the_persona_tls;
    
    constexpr auto known_active = std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>();
    
    tls.defer(
      backend::master,
      progress_level::internal,
      [=]() {
        bcast_payload_header *payload = (bcast_payload_header*)m->payload;
        
        gasnet::bcast_am_master_eager(level, payload->tm_id.here(), payload->eager_subrank_ub, payload, buf_size, buf_align);
        
        if(0 == --m->eager_refs)
          std::free(m->payload);
      },
      known_active
    );
    
    tls.enqueue(backend::master, level, m, known_active);
  }
}

////////////////////////////////////////////////////////////////////////////////
// rma_put_then_am_master

namespace {
  par_atomic<std::uint32_t> rma_put_then_am_nonce_bumper(0);
}

template<gasnet::rma_put_then_am_sync sync_lb, bool packed_protocol>
gasnet::rma_put_then_am_sync gasnet::rma_put_then_am_master_protocol(
    intrank_t rank_d,
    void *buf_d, void const *buf_s, std::size_t buf_size,
    progress_level am_level, void *am_cmd, std::size_t am_size, std::size_t am_align,
    gasnet::handle_cb *src_cb,
    gasnet::reply_cb *rem_cb
  ) {

  gex_Event_t src_h = GEX_EVENT_INVALID, *src_ph;

  switch(sync_lb) {
  case rma_put_then_am_sync::src_ignore:
    // issue 455: source_cx is being ignored, so dump it into NBI that we never sync
    src_ph = GEX_EVENT_GROUP;
    break;
  case rma_put_then_am_sync::src_now:
    src_ph = GEX_EVENT_NOW;
    break;
  case rma_put_then_am_sync::src_cb:
  default: // suppress exhaustiveness warning
    src_ph = &src_h;
    break;
  }
  
  size_t am_long_max = gex_AM_MaxRequestLong(world_tm, rank_d, src_ph, /*flags*/0, 16);
  
  if(am_long_max < buf_size) {
    (void)gex_RMA_PutBlocking(
      world_tm, rank_d,
      buf_d, const_cast<void*>(buf_s), buf_size - am_long_max,
      /*flags*/0
    );
    buf_d = (char*)buf_d + (buf_size - am_long_max);
    buf_s = (char const*)buf_s + (buf_size - am_long_max);
    buf_size = am_long_max;
  }
  
  if(packed_protocol) {
    gex_AM_Arg_t cmd_size_align15_level1 = am_size<<16 | am_align<<1 |
                                           (am_level == progress_level::user ? 1 : 0);
    gex_AM_Arg_t cmd_arg[13];
    UPCXX_ASSERT(am_size <= 13*sizeof(gex_AM_Arg_t));
    std::memcpy((void*)cmd_arg, am_cmd, am_size);
    
    gex_AM_RequestLong16(
      world_tm, rank_d,
      id_am_long_master_packed_cmd,
      const_cast<void*>(buf_s), buf_size, buf_d,
      src_ph,
      /*flags*/0,
      am_arg_encode_ptr_lo(rem_cb), am_arg_encode_ptr_hi(rem_cb),
      cmd_size_align15_level1,
      cmd_arg[0], cmd_arg[1], cmd_arg[2], cmd_arg[3],
      cmd_arg[4], cmd_arg[5], cmd_arg[6], cmd_arg[7],
      cmd_arg[8], cmd_arg[9], cmd_arg[10], cmd_arg[11],
      cmd_arg[12]
    );
  }
  else {
    gex_AM_Arg_t am_align_level1 =
      am_align<<1 |
      (am_level == progress_level::user ? 1 : 0);

    uint32_t nonce_u = rma_put_then_am_nonce_bumper.fetch_add(1, std::memory_order_relaxed);
    gex_AM_Arg_t nonce;
    std::memcpy(&nonce, &nonce_u, sizeof(gex_AM_Arg_t));
    
    (void)gex_AM_RequestLong5(
      world_tm, rank_d,
      id_am_long_master_payload_part,
      const_cast<void*>(buf_s), buf_size, buf_d,
      src_ph,
      /*flags*/0,
      nonce, am_size, am_align_level1,
      am_arg_encode_ptr_lo(rem_cb), am_arg_encode_ptr_hi(rem_cb)
    );

    size_t part_size_max = gex_AM_MaxRequestMedium(
      world_tm, rank_d, GEX_EVENT_NOW, /*flags*/0, /*num_args*/4
    );
    size_t part_offset = 0;
    
    while(part_offset < am_size) {
      size_t part_size = std::min(part_size_max, am_size - part_offset);
      
      (void)gex_AM_RequestMedium4(
        world_tm, rank_d,
        id_am_long_master_cmd_part,
        (char*)am_cmd + part_offset, part_size,
        GEX_EVENT_NOW,
        /*flags*/0,
        nonce, am_size, am_align_level1,
        part_offset
      );
      
      part_offset += part_size;
    }
  }

  // look for chance to escalate actual synchronization achieved
  if(sync_lb == rma_put_then_am_sync::src_cb) {
    if(0 == gex_Event_Test(src_h))
      return rma_put_then_am_sync::src_now;
    src_cb->handle = reinterpret_cast<uintptr_t>(src_h);
  }
  
  return sync_lb; // no sync escalation
}

// instantiate all cases of rma_put_then_am_master_protocol
#define INSTANTIATE(sync_lb, packed_protocol) \
  template \
  gasnet::rma_put_then_am_sync \
  gasnet::rma_put_then_am_master_protocol<sync_lb, packed_protocol>( \
      intrank_t rank_d, \
      void *buf_d, void const *buf_s, std::size_t buf_size, \
      progress_level am_level, void *am_cmd, std::size_t am_size, std::size_t am_align, \
      gasnet::handle_cb *src_cb, \
      gasnet::reply_cb *rem_cb \
    );

INSTANTIATE(gasnet::rma_put_then_am_sync::src_now, true)
INSTANTIATE(gasnet::rma_put_then_am_sync::src_now, false)
INSTANTIATE(gasnet::rma_put_then_am_sync::src_cb, true)
INSTANTIATE(gasnet::rma_put_then_am_sync::src_cb, false)
INSTANTIATE(gasnet::rma_put_then_am_sync::src_ignore, true)
INSTANTIATE(gasnet::rma_put_then_am_sync::src_ignore, false)
#undef INSTANTIATE

namespace {
  void am_long_master_packed_cmd(
      gex_Token_t token,
      void *payload, size_t payload_size,
      gex_AM_Arg_t reply_cb_lo, gex_AM_Arg_t reply_cb_hi,
      gex_AM_Arg_t cmd_size_align15_level1,
      gex_AM_Arg_t a0, gex_AM_Arg_t a1, gex_AM_Arg_t a2, gex_AM_Arg_t a3,
      gex_AM_Arg_t a4, gex_AM_Arg_t a5, gex_AM_Arg_t a6, gex_AM_Arg_t a7,
      gex_AM_Arg_t a8, gex_AM_Arg_t a9, gex_AM_Arg_t a10, gex_AM_Arg_t a11,
      gex_AM_Arg_t a12
    ) {
    
    size_t cmd_size = cmd_size_align15_level1>>(1+15);
    size_t cmd_align = (cmd_size_align15_level1>>1) & ((1<<15)-1);
    bool level_user = cmd_size_align15_level1 & 1;
    
    gex_AM_Arg_t buf[13] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12};
    rpc_as_lpc *m = rpc_as_lpc::build_eager((void*)buf, cmd_size, cmd_align);
    
    detail::persona_tls &tls = detail::the_persona_tls;
    
    tls.enqueue(
      backend::master,
      level_user ? progress_level::user : progress_level::internal,
      m,
      /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
    );

    if(!(reply_cb_lo == 0x0 && reply_cb_hi == 0x0))
      gex_AM_ReplyShort2(token, id_am_reply_cb, 0, reply_cb_lo, reply_cb_hi);
  }

  struct am_long_reassembly_state: rpc_as_lpc {
    par_atomic<int> credits{0};
    gex_AM_Arg_t reply_cb_lo, reply_cb_hi;
  };

  par_mutex am_long_reassembly_lock;
  std::unordered_map<std::uint64_t, am_long_reassembly_state*> am_long_reassembly_table;
  
  void am_long_master_payload_part(
      gex_Token_t token,
      void *payload_part, size_t payload_part_size,
      gex_AM_Arg_t nonce,
      gex_AM_Arg_t cmd_size,
      gex_AM_Arg_t cmd_align_level1,
      gex_AM_Arg_t reply_cb_lo, gex_AM_Arg_t reply_cb_hi
    ) {

    gex_Token_Info_t info;
    gex_Token_Info(token, &info, GEX_TI_SRCRANK);
    uint64_t key = uint64_t(info.gex_srcrank)<<32 ^ nonce;
    
    int credits_total = 1 + cmd_size;

    int cmd_align = cmd_align_level1 >> 1;
    int cmd_level = cmd_align_level1 & 1;
    
    am_long_reassembly_state *st;

    am_long_reassembly_lock.lock();
    {
      auto got = am_long_reassembly_table.insert({key, nullptr});
      if(got.second) {
        st = rpc_as_lpc::build_eager<am_long_reassembly_state>(nullptr, cmd_size, cmd_align);
        got.first->second = st;
      }
      else
        st = got.first->second;

      st->reply_cb_lo = reply_cb_lo;
      st->reply_cb_hi = reply_cb_hi;
      int credits_after = 1 + st->credits.fetch_add(1, std::memory_order_acq_rel);
      
      if(credits_after == credits_total) {
        am_long_reassembly_table.erase(got.first);
        // completion handled outside of lock below...
      }
      else
        st = nullptr; // disable completion below
    }
    am_long_reassembly_lock.unlock();

    if(st != nullptr) {
      // completion
      detail::persona_tls &tls = detail::the_persona_tls;
      tls.enqueue(
        backend::master,
        cmd_level ? progress_level::user : progress_level::internal,
        st,
        /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
      );
      
      if(!(reply_cb_lo == 0x0 && reply_cb_hi == 0x0))
        gex_AM_ReplyShort2(token, id_am_reply_cb, 0, reply_cb_lo, reply_cb_hi);
    }
  }

  void am_long_master_cmd_part(
      gex_Token_t token,
      void *cmd_part, size_t cmd_part_size,
      gex_AM_Arg_t nonce,
      gex_AM_Arg_t cmd_size,
      gex_AM_Arg_t cmd_align_level1,
      gex_AM_Arg_t cmd_part_offset
    ) {

    gex_Token_Info_t info;
    gex_Token_Info(token, &info, GEX_TI_SRCRANK);
    uint64_t key = uint64_t(info.gex_srcrank)<<32 ^ nonce;
    
    int credits_total = 1 + cmd_size;
    int credits_after_optimistic;
    
    int cmd_align = cmd_align_level1 >> 1;
    int cmd_level = cmd_align_level1 & 1;

    am_long_reassembly_state *st;

    am_long_reassembly_lock.lock();
    {
      auto got = am_long_reassembly_table.insert({key, nullptr});
      if(got.second) {
        st = rpc_as_lpc::build_eager<am_long_reassembly_state>(nullptr, cmd_size, cmd_align);
        got.first->second = st;
      }
      else
        st = got.first->second;

      credits_after_optimistic = cmd_part_size + st->credits.load(std::memory_order_relaxed);
      
      if(credits_after_optimistic == credits_total)
        am_long_reassembly_table.erase(got.first);
    }
    am_long_reassembly_lock.unlock();

    std::memcpy((char*)st->payload + cmd_part_offset, cmd_part, cmd_part_size);

    if(cmd_part_offset == 0)
      st->the_vtbl.execute_and_delete = command<detail::lpc_base*>::get_executor(am_long_reassembly_state::reader_of(st));
    
    int credits_after = cmd_part_size + st->credits.fetch_add(cmd_part_size, std::memory_order_acq_rel);

    if(credits_after == credits_total) {
      if(credits_after_optimistic != credits_total) {
        am_long_reassembly_lock.lock();
        am_long_reassembly_table.erase(key);
        am_long_reassembly_lock.unlock();
      }
      
      // completion
      gex_AM_Arg_t reply_cb_lo = st->reply_cb_lo;
      gex_AM_Arg_t reply_cb_hi = st->reply_cb_hi;
      
      detail::persona_tls &tls = detail::the_persona_tls;
      tls.enqueue(
        backend::master,
        cmd_level ? progress_level::user : progress_level::internal,
        st,
        /*known_active=*/std::integral_constant<bool, !UPCXXI_BACKEND_GASNET_PAR>()
      );
      
      if(!(reply_cb_lo == 0x0 && reply_cb_hi == 0x0))
        gex_AM_ReplyShort2(token, id_am_reply_cb, 0, reply_cb_lo, reply_cb_hi);
    }
  }
    
  void am_reply_cb(gex_Token_t, gex_AM_Arg_t cb_lo, gex_AM_Arg_t cb_hi) {
    gasnet::reply_cb *cb = am_arg_decode_ptr<gasnet::reply_cb>(cb_lo, cb_hi);
    cb->fire();
  }
}

////////////////////////////////////////////////////////////////////////

GASNETT_HOT
inline int handle_cb_queue::burst(bool maybe_spinning) {
  // Gasnet present's its asynchrony through pollable handles which is
  // problematic for us since we need to guess a good strategy for choosing
  // handles to poll which minimizes time wasted polling non-ready handles.
  // A simple heuristic is that handles retire approximately in injection order,
  // thus we ought to focus our time polling the front of the queue as that's
  // where ready handles are likely to be clustered. Our default strategy is to
  // begin at the front of the queue and keep polling until we hit N consecutive
  // non-ready handles, after which we abort. If our heuristic is accurate and
  // N is good, then after N handles are observed non-ready we can assume that
  // all handles that follow are probably also non-ready, justifying our abort.
  //
  // If the `maybe_spinning` flag is set, then we assume the CPU has no better
  // work to do than finding ready handles, and so we dynamically modify our abort
  // criteria to compsensate for the case when there are ready handles following
  // an unfortunate N-cluster of non-ready ones. If an invocation of burst()
  // finds no ready handles and exits via the abort path, we increment the
  // aborted_burst_n_ counter. Upon beginning a burst() scan, we set our N value
  // to linearly increase with the abort counter so that a recent history of
  // repeated failure motivates us to look harder for ready handles.

  int exec_n = 0;
  handle_cb **pp = &this->head_;

  int aborted_burst_n = this->aborted_burst_n_;
  // If we aren't in a progress() spin, just ignore the abort counter.
  int aborted_burst_n_and_spinning = maybe_spinning ? aborted_burst_n : 0;
  
  // This is N, the number of consecutive non-ready handles which will trigger
  // an abort.
  int miss_limit = 4 + aborted_burst_n_and_spinning;

  // Our current tally of consecutive non-ready handles as we scan the queue.
  // A negative initial value has the effect of polling that many non-ready
  // handles without contributing to the abort-triggering condition. As a
  // history of aborted burst()'s pile up, this grows in negativity to allow us
  // to see beyond the nefarious N-cluster which may have percolated to the front.
  int miss_n = -4*aborted_burst_n_and_spinning;
  
  while(*pp != nullptr) {
    handle_cb *p = *pp;
    gex_Event_t ev = reinterpret_cast<gex_Event_t>(p->handle);
    
    if(0 == gex_Event_Test(ev)) {
      // remove from queue
      *pp = p->next_;
      if(*pp == nullptr)
        this->set_tailp(pp);
      
      // do it!
      p->execute_and_delete(handle_cb_successor{this, pp});
      
      exec_n += 1;
      
      // Break the miss streak. Intentionally clobber negative values since now
      // that the app has learned of completed communication, it may have more
      // productive work to do outside of progress(), so we should feel pressure
      // to abort.
      miss_n = 0;
      
      aborted_burst_n = 0; // Reset abort history.
    }
    else {
      miss_n += 1;
      if(miss_n == miss_limit) { // Miss streak triggered abort.
        // Only increase abort history if we are spinning and this burst() was
        // entirely fruitless.
        if(maybe_spinning && exec_n == 0)
          aborted_burst_n = std::min<int>(aborted_burst_n + 1, 1024);
        break;
      }
      
      pp = &p->next_;
    }
  }

  this->aborted_burst_n_ = aborted_burst_n;
  
  return exec_n;
}

////////////////////////////////////////////////////////////////////////
// from: upcxx/os_env.hpp

namespace upcxx { namespace experimental {
  template<> // bool specialization for yes/no
  bool os_env(const std::string &name, const bool &otherwise) {
    return !!gasnett_getenv_yesno_withdefault(name.c_str(), otherwise);
  }
  // overload for mem_size_multiplier
  int64_t os_env(const std::string &name, const int64_t &otherwise, size_t mem_size_multiplier) {
    return gasnett_getenv_int_withdefault(name.c_str(), otherwise, mem_size_multiplier);
  }
} } // namespace

////////////////////////////////////////////////////////////////////////
// Other library ident strings live in watermark.cpp

GASNETT_IDENT(UPCXXI_IdentString_Network, "$UPCXXNetwork: " _STRINGIFY(GASNET_CONDUIT_NAME) " $");

