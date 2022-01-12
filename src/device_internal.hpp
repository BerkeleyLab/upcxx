#ifndef _986ac560_8bc4_4d86_acbd_e89d7f742629
#define _986ac560_8bc4_4d86_acbd_e89d7f742629

#include <upcxx/device_fwd.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>
#if UPCXXI_GEX_MK_ANY
#include <gasnet_mk.h>
#endif
#include <upcxx/reduce.hpp>

namespace upcxx { namespace backend {

  template<typename Device>
  struct device_heap_state; // : public device_heap_state_base<Device>

  template<typename Device>
  struct device_heap_state_base : public backend::heap_state {
    using DevPtr = typename Device::template pointer<void>;
    static constexpr DevPtr nullp = Device::template null_pointer<void>();
    static constexpr bool use_gex_mk = Device::use_gex_mk(detail::internal_only());
    DevPtr segment_to_free;

    #if UPCXXI_GEX_MK_ANY
      // objects for using GEX memory kinds
      gex_EP_t ep;
      gex_MK_t kind;
      gex_Segment_t segment;
    #endif

    device_heap_state_base() : heap_state(Device::kind) {
      #if UPCXXI_GEX_MK_ANY
        ep =      GEX_EP_INVALID;
        kind =    GEX_MK_INVALID;
        segment = GEX_SEGMENT_INVALID;
      #endif
      segment_to_free = nullp;
    }

    static device_heap_state<Device> *get(std::int32_t heap_idx, bool allow_null = false) {
      heap_state *hs = heap_state::get(heap_idx, allow_null);
      if (hs) UPCXX_ASSERT(hs->kind() == Device::kind);
      return static_cast<device_heap_state<Device>*>(hs);
    }

    #if UPCXXI_GEX_MK_ANY
      void create_endpoint(gex_MK_Create_args_t args, int heap_idx, const char *where) {
        UPCXX_ASSERT_ALWAYS(use_gex_mk);
        UPCXX_ASSERT_ALWAYS(ep == GEX_EP_INVALID && kind == GEX_MK_INVALID 
                         && segment == GEX_SEGMENT_INVALID, 
                         "internal error in create_endpoint for " << where);
        
        gex_TM_t TM0 = gasnet::handle_of(upcxx::world()); UPCXX_ASSERT(TM0 != GEX_TM_INVALID);
        gex_Client_t client = gex_TM_QueryClient(TM0);

        int ok = gex_MK_Create(&this->kind, client, &args, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK, "gex_MK_Create failed for " << where);

        ok = gex_EP_Create(&this->ep, client, GEX_EP_CAPABILITY_RMA, 0);
        UPCXX_ASSERT_ALWAYS(ok == GASNET_OK, "gex_EP_Create failed for heap_idx " << heap_idx << ", " << where);

        gex_EP_Index_t epidx  = gex_EP_QueryIndex(this->ep);
        UPCXX_ASSERT_ALWAYS(epidx == heap_idx, "gex_EP_Create generated unexpected EP_Index "
                                                << epidx << "for heap_idx " << heap_idx << ", " << where);
      }

      void destroy_endpoint(const char *where) {
        UPCXX_ASSERT_ALWAYS(use_gex_mk);
        // TODO: once they are provided, eventually will do:
        //   gex_Segment_Destroy()
        //   gex_MK_Destroy()
        //   gex_EP_Destroy()  
        // and modify heap_state to allow recycling of heap_idx
        this->segment = GEX_SEGMENT_INVALID;
        this->ep =      GEX_EP_INVALID;
        this->kind =    GEX_MK_INVALID;
      }
    #endif

    // make_gpu_segment: collectively construct segment_allocators for GPU device segments, 
    //   using provided raw device allocators and performing GASNet registration as needed
    //   with state fields saved into the provided device_heap_state
    template<typename device_alloc, typename device_free>
    static detail::segment_allocator make_gpu_segment(device_heap_state_base *st, 
                                       int heap_idx, DevPtr base, size_t size, 
                                       const char *where,
                                       device_alloc &&dev_alloc, device_free &&dev_free) {
      uint64_t failed_alloc = 0;
 
      if (st) { // creating a real device heap, possibly allocating memory
        UPCXX_ASSERT_ALWAYS(size != 0, where << " requested invalid segment size="<<size);
        UPCXX_ASSERT_ALWAYS(st->segment_to_free == nullp, where << " internal error");
  
        if (-size == 1) { // undocumented "largest available"
          UPCXX_ASSERT_ALWAYS(base == nullp, where << " invalid size with non-null base"); 
          size_t lo=1<<20, hi=size_t(16)<<30;
      
          while(hi-lo > 64<<10) { // search largest to within 64KiB
            if(base != nullp) {
              dev_free(base);
            }
            size = (lo + hi)/2;
            
            base = dev_alloc(size);
            if (base == nullp) { // out of memory
              hi = size;
            } else {
              lo = size;
            }
          }
          st->segment_to_free = base;
          if (base == nullp) failed_alloc = size;

        } else if(base == nullp) { // allocate a particular size

          base = dev_alloc(size);
          if (base == nullp) { // out of memory
            failed_alloc = size;
          }
          st->segment_to_free = base;
  
        } else { // client-provided segment
          st->segment_to_free = nullp;
        }
      }

      // once segment is collectively created, decide whether we are keeping it.
      // this is a effectively a user-level barrier, but not a documented guarantee.
      // This could be a simple boolean bit_or reduction to test if any process failed.
      // However in order to improve error reporting upon allocation failure, 
      // we instead reduce a max over a value constructed as: 
      //   high 44 bits: size_that_failed | low 20 bits: rank_that_failed
      uint64_t alloc_report_max = uint64_t(1LLU<<44) - 1;
      uint64_t reduceval = std::min(failed_alloc, alloc_report_max);
      uint64_t rank_report_max = (1LLU<<20) - 1;
      uint64_t rank_tmp = std::min(std::uint64_t(upcxx::rank_me()), rank_report_max);
      reduceval = (reduceval << 20) | rank_tmp;
      reduceval = upcxx::reduce_all<uint64_t>(reduceval, upcxx::op_fast_max).wait();
      uint64_t largest_failure = reduceval >> 20;
 
 
      if (largest_failure > 0) { // single-valued
        // at least one process failed to allocate, so collectively unwind and throw
        if (st && st->segment_to_free) {
          dev_free(st->segment_to_free);
          st->segment_to_free = nullp;
        }
  
        // collect info to report about the failing request in exn.what()
        uint64_t report_size;
        upcxx::intrank_t report_rank;
        if (failed_alloc) { // ranks that failed report themselves directly
          report_size = failed_alloc;
          report_rank = upcxx::rank_me();
        } else { // others report the largest failing allocation request from the reduction
          UPCXX_ASSERT(largest_failure <= alloc_report_max);
          if (largest_failure == alloc_report_max) report_size = 0; // size overflow
          else report_size = largest_failure;
          reduceval &= rank_report_max;
          if (reduceval == rank_report_max) report_rank = -1; // rank overflow, don't know who
          else report_rank = reduceval;
        }
  
        throw upcxx::bad_segment_alloc(detail::to_string(Device::kind).c_str(), report_size, report_rank);
      } 

      if (use_gex_mk) { // perform GASNet-EX segment registration
        #if UPCXXI_GEX_MK_ANY
        gex_TM_t TM0 = upcxx::backend::gasnet::handle_of(upcxx::world()); UPCXX_ASSERT(TM0 != GEX_TM_INVALID);
        if (st) {
          gex_Client_t client = gex_TM_QueryClient(TM0);
  
          UPCXX_ASSERT(st->ep   != GEX_EP_INVALID);
          UPCXX_ASSERT(st->kind != GEX_MK_INVALID);
          UPCXX_ASSERT(st->segment == GEX_SEGMENT_INVALID);
          int ok = gex_Segment_Create(&st->segment, client, 
                                      (void*)base, size, st->kind, 0);
          UPCXX_ASSERT_ALWAYS(ok == GASNET_OK && st->segment != GEX_SEGMENT_INVALID,
            "gex_Segment_Create("<<size<<") failed in " << where);
  
          gex_EP_BindSegment(st->ep, st->segment, 0);
          UPCXX_ASSERT(gex_EP_QuerySegment(st->ep) == st->segment,
            "gex_EP_BindSegment() failed in " << where);
  
          ok = gex_EP_PublishBoundSegment(TM0, &st->ep, 1, 0);
          UPCXX_ASSERT_ALWAYS(ok == GASNET_OK,
            "gex_EP_PublishBoundSegment() failed in " << where);
        } else { // matching collective call for inactive ranks
          int ok = gex_EP_PublishBoundSegment(TM0, nullptr, 0, 0);
          UPCXX_ASSERT_ALWAYS(ok == GASNET_OK,
            "gex_EP_PublishBoundSegment() failed in " << where);
        }
        #endif
      }
  
      return detail::segment_allocator((void*)base, size);
    } // make_gpu_segment

  }; // device_heap_state_base

}} // namespace


#endif
