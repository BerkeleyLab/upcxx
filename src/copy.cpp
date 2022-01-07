#include <upcxx/copy.hpp>
#include <upcxx/cuda_internal.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

#include <cstring>

using namespace std;

namespace detail = upcxx::detail;
namespace gasnet = upcxx::backend::gasnet;
namespace cuda = upcxx::cuda;

using upcxx::memory_kind;
using upcxx::detail::lpc_base;

void upcxx::detail::rma_copy_local(
    int heap_d, void *buf_d,
    int heap_s, void const *buf_s, std::size_t size,
    backend::device_cb *cb
  ) {

  const bool host_d = (heap_d == host_heap || heap_d == private_heap);
  const bool host_s = (heap_s == host_heap || heap_s == private_heap);

  if( host_d && host_s) { // both sides in local host memory
    UPCXX_ASSERT((char*)buf_d + size <= buf_s || (char*)buf_s + size <= buf_d,
                 "Source and destination regions in upcxx::copy must not overlap");
    std::memcpy(buf_d, buf_s, size);
    cb->execute_and_delete();
  }
  else { // one or both sides on device
  #if UPCXXI_CUDA_ENABLED
    int heap_main = !host_d ? heap_d : heap_s;
    UPCXX_ASSERT(heap_main > 0);
    cuda::device_state *st = cuda::device_state::get(heap_main);
    
    CU_CHECK(cuCtxPushCurrent(st->context));

    if(!host_d && !host_s) {
      cuda::device_state *st_d = cuda::device_state::get(heap_d);
      cuda::device_state *st_s = cuda::device_state::get(heap_s);
      
      // device to device
      CU_CHECK(cuMemcpyPeerAsync(
        reinterpret_cast<CUdeviceptr>(buf_d), st_d->context,
        reinterpret_cast<CUdeviceptr>(buf_s), st_s->context,
        size, st->stream
      ));
    }
    else if(!host_d) {
      // host to device
      CU_CHECK(cuMemcpyHtoDAsync(reinterpret_cast<CUdeviceptr>(buf_d), buf_s, size, st->stream));
    }
    else {
      UPCXX_ASSERT(!host_s);
      // device to host
      CU_CHECK(cuMemcpyDtoHAsync(buf_d, reinterpret_cast<CUdeviceptr>(buf_s), size, st->stream));
    }

    CUevent event;
    CU_CHECK(cuEventCreate(&event, CU_EVENT_DISABLE_TIMING));
    CU_CHECK(cuEventRecord(event, st->stream));
    cb->event = (void*)event;

    persona *per = detail::the_persona_tls.get_top_persona();
    per->UPCXXI_INTERNAL_ONLY(device_state_).cuda.cbs.enqueue(cb);
    
    {CUcontext dump; CU_CHECK(cuCtxPopCurrent(&dump));}
  #else
    UPCXXI_FATAL_ERROR("Unrecognized heaps in upcxx::copy() -- gptr corruption?");
  #endif
  }
}

void upcxx::detail::rma_copy_remote(
    int heap_s, intrank_t rank_s, void const * buf_s,
    int heap_d, intrank_t rank_d, void * buf_d,
    std::size_t size, 
    gasnet::handle_cb *cb
  ) {
#if UPCXXI_GEX_MK_CUDA
  const bool isput = (rank_s == upcxx::rank_me());

  gex_EP_Index_t local_ep_idx;
  if (isput) {
    if (heap_s == private_heap) local_ep_idx = 0;
    else local_ep_idx = heap_s;
    UPCXX_ASSERT(heap_d != private_heap);
    UPCXX_ASSERT(heap_d < backend::heap_state::max_heaps);
  } else { // isget
    if (heap_d == private_heap) local_ep_idx = 0;
    else local_ep_idx = heap_d;
    UPCXX_ASSERT(heap_s != private_heap);
    UPCXX_ASSERT(heap_s < backend::heap_state::max_heaps);
  }

  gex_EP_t local_ep;
  if (local_ep_idx == 0) { // local using EP0
    gex_TM_t TM0 = upcxx::backend::gasnet::handle_of(upcxx::world()); 
    UPCXX_ASSERT(TM0 != GEX_TM_INVALID);
    local_ep = gex_TM_QueryEP(TM0);
  } else { // local using device EP
    cuda::device_state *st = cuda::device_state::get(local_ep_idx);
    local_ep = st->ep;
    UPCXX_ASSERT(st->segment != GEX_SEGMENT_INVALID);
  }
  UPCXX_ASSERT(gex_EP_QueryIndex(local_ep) == local_ep_idx);
  
  // We cannot pass GEX_FLAG_PEER_NEVER_NBRHD in the RMA calls below, 
  // because this function is used to handle interprocess transfers
  // that involve a device on one/both sides, even between local_team peers.
  // We could safely pass GEX_FLAG_PEER_NEVER_SELF,
  // but there is currently no benefit obtained by passing that flag.
  gex_Event_t h;
  if (isput) {
    h = gex_RMA_PutNB(
      gex_TM_Pair(local_ep, heap_d),
      rank_d, buf_d, const_cast<void*>(buf_s), size,
      GEX_EVENT_DEFER, // TODO: real source completion
      /*flags*/0
    );
  } else { // isget
    h = gex_RMA_GetNB(
      gex_TM_Pair(local_ep, heap_s),
      buf_d, rank_s, const_cast<void*>(buf_s), size,
      /*flags*/0
    );
  }

  cb->handle = reinterpret_cast<uintptr_t>(h);
  gasnet::register_cb(cb);
  gasnet::after_gasnet();
#else // !UPCXXI_GEX_MK_CUDA
    UPCXXI_FATAL_ERROR("Internal error in upcxx::copy()");
#endif
}

void upcxx::detail::rma_copy_get_nonlocal(
    void *buf_d, intrank_t rank_s, void const *buf_s, std::size_t size,
    gasnet::handle_cb *cb
  ) {
  UPCXX_ASSERT(!backend::rank_is_local(rank_s)); // bypass handled in header

  gex_Event_t h = gex_RMA_GetNB(
    gasnet::handle_of(upcxx::world()),
    buf_d, rank_s, const_cast<void*>(buf_s), size,
    UPCXXI_GEX_FLAG_PEER_NEVER_NBRHD
  );
  cb->handle = reinterpret_cast<uintptr_t>(h);
  gasnet::register_cb(cb);
  gasnet::after_gasnet();
}

void upcxx::detail::rma_copy_get(
    void *buf_d, intrank_t rank_s, void const *buf_s, std::size_t size,
    gasnet::handle_cb *cb
  ) {
  #if UPCXXI_GEX_MK_CUDA
    UPCXXI_FATAL_ERROR("Internal error in upcxx::copy() -- unexpected call to detail::rma_copy_get");
  #endif

  gex_Event_t h = gex_RMA_GetNB(
    gasnet::handle_of(upcxx::world()),
    buf_d, rank_s, const_cast<void*>(buf_s), size,
    /*flags*/0
  );
  cb->handle = reinterpret_cast<uintptr_t>(h);
  gasnet::register_cb(cb);
  gasnet::after_gasnet();
}

void upcxx::detail::rma_copy_put(
    intrank_t rank_d, void *buf_d, void const *buf_s, std::size_t size,
    gasnet::handle_cb *cb
  ) {
  #if UPCXXI_GEX_MK_CUDA
    UPCXXI_FATAL_ERROR("Internal error in upcxx::copy() -- unexpected call to detail::rma_copy_put");
  #endif

  gex_Event_t h = gex_RMA_PutNB(
    gasnet::handle_of(upcxx::world()),
    rank_d, buf_d, const_cast<void*>(buf_s), size,
    GEX_EVENT_DEFER,
    /*flags*/0
  );
  cb->handle = reinterpret_cast<uintptr_t>(h);
  gasnet::register_cb(cb);
  gasnet::after_gasnet();
}
