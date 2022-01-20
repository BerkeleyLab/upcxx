#include <string>

#include <upcxx/upcxx.hpp>
#include "util.hpp"

#ifndef USE_HOST
#define USE_HOST 1
#endif

#ifndef USE_CUDA
  #if UPCXX_KIND_CUDA 
    #define USE_CUDA 1
  #endif
#endif
#if USE_CUDA && !UPCXX_KIND_CUDA
  #error requested USE_CUDA but this UPC++ install does not have CUDA support
#endif

#ifndef HEAPS_PER_KIND
#define HEAPS_PER_KIND 2
#endif
#ifndef ALLOCS_PER_HEAP
#define ALLOCS_PER_HEAP 3
#endif
constexpr unsigned heaps_per_kind = HEAPS_PER_KIND;
constexpr unsigned allocs_per_heap = ALLOCS_PER_HEAP;

int dev_n_cuda = 0;

using namespace upcxx;

using val_t = std::uint32_t;
#define VAL(rank, step, idx) ((val_t)(((rank)&0xFFFF << 16) | ((step)&0xFF << 8) | ((idx)&0xFF) ))

using any_ptr = global_ptr<val_t, memory_kind::any>;
long errs = 0;

// Factored kind-independent device buffer state
template<typename Device>
struct DeviceState {
  static int device_n() {
    int dev_n = Device::device_n();
    int lo = upcxx::reduce_all(dev_n, upcxx::op_fast_min).wait();
    int hi = upcxx::reduce_all(dev_n, upcxx::op_fast_max).wait();

    if(!rank_me()) { // output
      if (lo != hi)
        say("")<<"Notice: not all ranks report the same number of GPUs: min="<<lo<<" max="<<hi;
      if (!lo)
        say("")<<"WARNING: UPC++ GPU support is compiled-in, but could not find sufficient GPU support at runtime.";
    }
    return lo; // ensure single-valued device count
  }

  Device* gpu[heaps_per_kind] = {};
  device_allocator<Device>* seg[heaps_per_kind] = {};
  global_ptr<val_t, Device::kind> dev_ptrs[heaps_per_kind][allocs_per_heap] = {};

  // Collectively create heaps_per_kind device heaps for this Device kind, spread across dev_n GPUs
  // Allocate allocs_per_heap objects of maxelems*2 val_t's
  // and insert corresponding buffers into ptrs_out, spread across neighbors
  void create(size_t maxelems, int dev_n, std::vector<any_ptr> &ptrs_out) {
    int me = upcxx::rank_me();
    int ranks = upcxx::rank_n();
    for (unsigned dev = 0; dev < heaps_per_kind; dev++) {
      size_t align = Device::template default_alignment<val_t>();
      size_t allocsz = maxelems*2*sizeof(val_t);
      allocsz = align*((allocsz+align-1)/align);
      align = 4096;
      if (allocsz > align) { // more than one page gets a full page
        allocsz = align*((allocsz+align-1)/align);
      }
      UPCXX_ASSERT(!gpu[dev] && !seg[dev]);
      gpu[dev] = new Device(dev%dev_n);
      seg[dev] = new device_allocator<Device>(*gpu[dev], allocsz*allocs_per_heap);
      for (unsigned i=0; i < allocs_per_heap; i++) {
        dev_ptrs[dev][i] = seg[dev]->template allocate<val_t>(maxelems*2);
        assert(dev_ptrs[dev][i]);
        int rank = (me+i)%ranks;
        dist_object<any_ptr> dobj(dev_ptrs[dev][i]);
        any_ptr gp = dobj.fetch(rank).wait();
        ptrs_out.push_back(gp);
        barrier();
      }
    }
  }

  // clean up all the resources allocated by create()
  void destroy() {
    for (unsigned dev = 0; dev < heaps_per_kind; dev++) {
      UPCXX_ASSERT(gpu[dev] && seg[dev]);
      for (unsigned i=0; i < allocs_per_heap; i++) {
        seg[dev]->deallocate(dev_ptrs[dev][i]);
        dev_ptrs[dev][i] = nullptr;
      }
      delete seg[dev]; seg[dev] = nullptr;
      gpu[dev]->destroy();
      delete gpu[dev]; gpu[dev] = nullptr;
    }
  }
};

int main(int argc, char *argv[]) {
  upcxx::init();
  print_test_header();

  int me = upcxx::rank_me();
  int ranks = upcxx::rank_n();
  long iters = 0;
  if (argc > 1) iters = std::atol(argv[1]);
  if (iters <= 0) iters = 10;
  size_t maxelems;
  { size_t bufsz = 0;
    if (argc > 2) bufsz = std::atol(argv[2]);
    if (bufsz <= 0) bufsz = 1024*1024;
    if (bufsz < sizeof(val_t)) bufsz = sizeof(val_t);
    maxelems = bufsz / sizeof(val_t);
    if (!me) say("") << "Running with iters=" << iters << " bufsz=" << maxelems*sizeof(val_t) << " bytes\n"
                     << "  using " << heaps_per_kind << " heaps per device kind and "
                     << allocs_per_heap << " buffer allocations per heap.";
  }

  {
    if(me == 0 && ranks < 3)
      say("") << "Advice: consider using 3 (or more) ranks to cover three-party cases for upcxx::copy.";

    std::vector<any_ptr> ptrs;
    // fill ptrs with global ptrs to buffers, with allocs_per_heap

    global_ptr<val_t> host_ptrs[allocs_per_heap] = {};
    #if USE_HOST
      for (unsigned i=0; i < allocs_per_heap; i++) {
        host_ptrs[i] = upcxx::new_array<val_t>(maxelems*2);
        int rank = (me+i)%ranks;
        dist_object<any_ptr> dobj(host_ptrs[i]);
        any_ptr gp = dobj.fetch(rank).wait();
        ptrs.push_back(gp);
        barrier();
      }
    #endif

    #if USE_CUDA
      // open the devices, allocate and distribute device buffers, appending to ptrs:
      DeviceState<cuda_device> devstate_cuda;
      dev_n_cuda = devstate_cuda.device_n();
      if (dev_n_cuda) devstate_cuda.create(maxelems, dev_n_cuda, ptrs);
    #endif
    say()<<"Running with "<<dev_n_cuda<<" CUDA GPUs";

    val_t *priv_src = new val_t[maxelems];
    val_t *priv_dst = new val_t[maxelems];
    const int bufcnt = ptrs.size();

    uint64_t step = 0;
    static uint64_t rc_count = 0;
    for (int round = 0; round < iters; round++) {
     #if UPCXX_THREADMODE
     persona p;
     persona_scope ps(p);
     #endif

     bool talk = !me && (iters <= 10 || round % ((iters+9)/10) == 0);
     if (talk) {
        say("") << "Round "<< round << " (" << round*100/iters << " %)";
     }
     upcxx::barrier();
     for (size_t bufelems = 1; bufelems < 2*maxelems; bufelems *= 2) {
      if (bufelems > maxelems) bufelems = maxelems;
      
      for (int A=0; A < bufcnt; A++) {
      for (int B=0; B < bufcnt; B++) {
        any_ptr bufA = ptrs[A];
        any_ptr bufB = ptrs[B] + maxelems;
        #if SKIP_KILL
          int killfreq = 0;
        #else
          int killfreq = 7;
        #endif
        const val_t dead = (val_t)step;
        #if SKIP_RC_ONLY
          const bool rconly = false;
        #else
          const bool rconly = (step%5 == 1);
        #endif

        for(size_t i=0; i < bufelems; i++) {
          priv_src[i] = VAL(me, step, i);
          priv_dst[i] = 0;
        }

        #if SKIP_COPY // for tester validation
          memcpy(priv_dst, priv_src, bufelems*sizeof(val_t));
        #else
          auto cxs = operation_cx::as_future() | source_cx::as_future();
          auto rc = [](int rank) { 
            UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
            UPCXX_ASSERT_ALWAYS(rank == upcxx::rank_me());
            #if UPCXX_SPEC_VERSION >= 20201000
            UPCXX_ASSERT_ALWAYS(upcxx::in_progress());
            #endif
            rc_count++;
          };
          future<> of, sf;
          future<> kf1 = make_future(), kf2 = make_future(), kf3 = make_future();
          const bool kill1 = killfreq && !((step*3+1)%killfreq);
          const bool kill2 = killfreq && !((step*3+2)%killfreq);
          const bool kill3 = killfreq && !((step*3+3)%killfreq);

        if (rconly) { // use only remote completion events

          promise<> p1,p2,p3;
          // private -> heapA
          upcxx::copy<val_t>(priv_src, bufA, bufelems, 
            remote_cx::as_rpc([=,&p1]() {
              rc(bufA.where()); // at heapA
              // notify initiator
              rpc_ff(me, [&]() { p1.fulfill_anonymous(1); });
            }));
          p1.get_future().wait();

          // heapA -> heapB
          upcxx::copy<val_t>(bufA, bufB, bufelems,
            remote_cx::as_rpc([=,&p2]() {
              rc(bufB.where()); // at heapB
              // notify initiator
              rpc_ff(me, [&]() { p2.fulfill_anonymous(1); });
            }));
          p2.get_future().wait();

          // heapB -> private
          upcxx::copy<val_t>(bufB, priv_dst, bufelems,
            remote_cx::as_rpc([=,&p3]() {
              rc(me); // at me
              p3.fulfill_anonymous(1); 
            }));
          p3.get_future().wait();

        } else { // exercise all three completion events

          // private -> heapA
          std::tie(of, sf) = upcxx::copy<val_t>(priv_src, bufA, bufelems, 
                                                cxs | remote_cx::as_rpc(rc,bufA.where()));
          if (kill1) {
            kf1 = sf.then([=]() { priv_src[0] = dead; });
          } else sf.wait(); 
          of.wait();

          // heapA -> heapB
          std::tie(of, sf) = upcxx::copy<val_t>(bufA, bufB, bufelems,
                                                cxs | remote_cx::as_rpc(rc,bufB.where()));
          if (kill2) {
            kf2 = sf.then([&]() { return upcxx::copy<val_t>(&dead, bufA, 1); });
          } else sf.wait(); 
          of.wait();

          // heapB -> private
          std::tie(of, sf) = upcxx::copy<val_t>(bufB, priv_dst, bufelems,
                                                cxs | remote_cx::as_rpc(rc,me));
          if (kill3) {
            kf3 = sf.then([&]() { return upcxx::copy<val_t>(&dead, bufB, 1); });
          } else sf.wait(); 
          of.wait();

          when_all(kf1, kf2, kf3).wait();

        }
        #endif

        std::string mismatch;
        for(size_t i=0; i < bufelems; i++) {
          val_t got = priv_dst[i];
          val_t expect = VAL(me, step, i);
          if (got != expect && !mismatch.size()) {
            std::ostringstream oss;
            oss << " i=" << i << " expect=" << expect << " got=" << got;
            mismatch = oss.str();
          }
        }
        if (mismatch.size()) { // diagnose failure
          auto who = [=](int rank) { 
            if (rank == me) return "my "; 
            if (rank == (me+1)%ranks) return "his ";
            if (rank == (me+2)%ranks) return "her ";
            return "other ";
          };
          const char * Awhere = who(bufA.where());
          const char * Aheap  = (bufA.dynamic_kind() == memory_kind::host ? "host" : "device");
          const char * Bwhere = who(bufB.where());
          const char * Bheap  = (bufB.dynamic_kind() == memory_kind::host ? "host" : "device");
          say() << "ERROR: Mismatch at round="<<round<<" bufsz="<<(bufelems*sizeof(val_t))
                <<" step="<<step
                <<" A="<<A<<"("<<Awhere<<Aheap<<")"
                <<" B="<<B<<"("<<Bwhere<<Bheap<<")"
                <<mismatch
                <<(kill1?", kill1":"")<<(kill2?", kill2":"")<<(kill3?", kill3":"");
          errs++;
        }

        step++;
      }} // A/B bufs

      uint64_t rc_expected = 3 * uint64_t(bufcnt)*bufcnt;
      do { upcxx::progress(); } while (rc_count < rc_expected);
      UPCXX_ASSERT_ALWAYS(rc_count == rc_expected);
      rc_count = 0;
      upcxx::barrier();
     } // bufelems
    } // round
    
    UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
    upcxx::barrier();

    // cleanup

    delete [] priv_src;
    delete [] priv_dst;

    #if USE_HOST
      for (unsigned i=0; i < allocs_per_heap; i++) {
        upcxx::delete_array(host_ptrs[i]);
      }
    #endif
    
    #if USE_CUDA
      if (dev_n_cuda) devstate_cuda.destroy();
    #endif
  }
    
  UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
  print_test_success(errs == 0);
  
  upcxx::finalize();
  return 0;
}
