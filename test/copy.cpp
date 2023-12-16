#include <upcxx/upcxx.hpp>

// Note: This test is an "open-box" validation test for UPC++ memory kind functionality
// that deliberately makes redundant direct calls to the GPU library to validate 
// correct operation of UPC++ memory kinds. Much of the cruft below is unnecessary
// to normal use of memory kinds and should NOT be used as an example for that feature.
//
// This test runs with at most one device kind, in the following priority order:
#undef DEVICE
#if UPCXX_KIND_HIP
  #define DEVICE hip_device
  #include <hip/hip_runtime_api.h>
  constexpr int max_dev_n = 32;
  int dev_n;
  #define HIP_CHECK(expr) do { \
    hipError_t resxxx = (expr); \
    if (resxxx != hipSuccess) { \
      const char *errname = hipGetErrorName(resxxx); \
      const char *errstr  = hipGetErrorString(resxxx); \
      upcxx::experimental::say() << "HIP ERROR: "  \
        << errname << "(" << resxxx << ")" << ": " << errstr \
        << "\n  in " << #expr \
        << "\n  at " << __FILE__ << ":" << __LINE__; \
      std::abort(); \
    } \
  } while (0)
  #define DEVICE_INIT()    HIP_CHECK(hipInit(0))
  #define DEVICE_SET(id)   HIP_CHECK(hipSetDevice(id))
  #define DEVICE_MEMCPY_D2H(dst, src, sz) \
         HIP_CHECK(hipMemcpyDtoH(dst, reinterpret_cast<hipDeviceptr_t>(src), sz))
  #define DEVICE_MEMCPY_H2D(dst, src, sz) \
         HIP_CHECK(hipMemcpyHtoD(reinterpret_cast<hipDeviceptr_t>(dst), src, sz))
  #define DEVICE_SYNC()    HIP_CHECK(hipDeviceSynchronize())
#elif UPCXX_KIND_CUDA
  #define DEVICE cuda_device
  #include <cuda_runtime_api.h>
  #include <cuda.h>
  constexpr int max_dev_n = 32;
  int dev_n;
  #define CU_CHECK(expr) do { \
    CUresult resxxx = (CUresult)(expr); \
    if (resxxx != CUDA_SUCCESS) { \
      const char *errname="", *errstr=""; \
      cuGetErrorName(resxxx, &errname); \
      cuGetErrorString(resxxx, &errstr); \
      if (!errname) { \
        errname = cudaGetErrorName((cudaError_t)resxxx); \
        errstr = cudaGetErrorString((cudaError_t)resxxx); \
      } \
      upcxx::experimental::say() << "CUDA ERROR: "  \
        << errname << "(" << resxxx << ")" << ": " << errstr \
        << "\n  in " << #expr \
        << "\n  at " << __FILE__ << ":" << __LINE__; \
      std::abort(); \
    } \
  } while (0)
  #define DEVICE_INIT()    CU_CHECK(cuInit(0))
  #define DEVICE_SET(id)   CU_CHECK(cudaSetDevice(id))
  #define DEVICE_MEMCPY_D2H(dst, src, sz) \
         CU_CHECK(cuMemcpyDtoH(dst, reinterpret_cast<CUdeviceptr>(src), sz))
  #define DEVICE_MEMCPY_H2D(dst, src, sz) \
         CU_CHECK(cuMemcpyHtoD(reinterpret_cast<CUdeviceptr>(dst), src, sz))
  #define DEVICE_SYNC() do { \
          CU_CHECK(cuCtxSynchronize()); /* issue #241 */ \
          CU_CHECK(cudaDeviceSynchronize()); \
    } while(0)
#elif UPCXX_KIND_ZE
  #define DEVICE ze_device
  #include <level_zero/ze_api.h>
  using upcxx::ze_device;
  constexpr int max_dev_n = 32;
  int dev_n;
  int cur_dev = -1;
  int group_ordinal = 0;
  std::vector<ze_device_handle_t> zeDev;
  std::vector<ze_context_handle_t> zeCtx;
  std::vector<ze_command_queue_handle_t> zeQueue;
  std::vector<ze_command_list_handle_t> zeCmd;

  #define ZE_CHECK(expr) do { \
    ze_result_t resxxx = (expr); \
    if (resxxx != ZE_RESULT_SUCCESS) { \
      /* level zero lacks proper runtime error code support */ \
      upcxx::experimental::say() << "ZE ERROR: "  \
        << "0x" << std::hex << resxxx \
        << "\n  in " << #expr \
        << "\n  at " << __FILE__ << ":" << std::dec << __LINE__; \
      std::abort(); \
    } \
  } while (0)
  void DEVICE_INIT() {
    ZE_CHECK(zeInit(0));
    const int dev_n = ze_device::device_n();
    zeDev.reserve(dev_n);
    zeCtx.reserve(dev_n);
    zeQueue.reserve(dev_n);
    zeCmd.reserve(dev_n);
    for (int d = 0; d < dev_n; d++) {
      zeDev[d] = ze_device::device_id_to_device_handle(d);
      zeCtx[d] = ze_device::get_driver_context(zeDev[d]);
      ze_command_queue_desc_t cmdQueueDesc = {ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
      cmdQueueDesc.ordinal = group_ordinal;
      cmdQueueDesc.index = 0;
      // makes zeCommandQueueExecuteCommandLists block for completion:
      cmdQueueDesc.mode = ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS;
      ZE_CHECK( zeCommandQueueCreate(zeCtx[d], zeDev[d], &cmdQueueDesc, &zeQueue[d]));
      ze_command_list_desc_t commandListDesc = { ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC };
      commandListDesc.commandQueueGroupOrdinal = group_ordinal;
      ZE_CHECK( zeCommandListCreate(zeCtx[d], zeDev[d], &commandListDesc, &zeCmd[d]));
    }
  }
  void ze_copy(void *dst, void *src, size_t sz) {
    // this is a bare-minimum fully blocking copy, 
    // and should not be considered a good example.
    ZE_CHECK( zeCommandListAppendMemoryCopy(zeCmd[cur_dev], dst, src, sz, nullptr, 0, nullptr));
    ZE_CHECK( zeCommandListClose(zeCmd[cur_dev]));
    ZE_CHECK( zeCommandQueueExecuteCommandLists(zeQueue[cur_dev], 1, &zeCmd[cur_dev], nullptr)); 
    ZE_CHECK( zeCommandListReset(zeCmd[cur_dev]));
  }
  #define DEVICE_SET(id)   (cur_dev = (id))
  #define DEVICE_MEMCPY_D2H(dst, src, sz) ze_copy(dst, src, sz)
  #define DEVICE_MEMCPY_H2D(dst, src, sz) ze_copy(dst, src, sz)
  #define DEVICE_SYNC() do { \
    for (int d = 0; d < dev_n; d++) { \
      ZE_CHECK( zeCommandQueueSynchronize(zeQueue[d],  std::numeric_limits<uint64_t>::max())); \
    } \
  } while(0)
#else
  constexpr int max_dev_n = 0; // set to num GPU/process
  constexpr int dev_n = 0;
#endif

#include "util.hpp" // defines Device = DEVICE

constexpr int rounds = 4;
    
using namespace upcxx;

template<typename T>
using any_ptr = global_ptr<T, memory_kind::any>;

int main() {
  upcxx::init();
  print_test_header();
  {
    int me = upcxx::rank_me();
    if (upcxx::rank_n() < 2) {
      print_test_skipped("test requires two or more ranks");
      upcxx::finalize();
      return 0;
    }

    if(me == 0 && upcxx::rank_n() < 3)
      say("") << "Advice: consider using 3 (or more) ranks to cover three-party cases for upcxx::copy.";

    #ifdef DEVICE
    {
      DEVICE_INIT();
      dev_n = Device::device_n();
      if(dev_n >= max_dev_n)
        dev_n = max_dev_n-1;

      int lo = upcxx::reduce_all(dev_n, upcxx::op_fast_min).wait();
      int hi = upcxx::reduce_all(dev_n, upcxx::op_fast_max).wait();

      if(me == 0 && lo != hi)
        say("")<<"Notice: not all ranks report the same number of GPUs: min="<<lo<<" max="<<hi;

      dev_n = lo;
      if (me == 0 && !dev_n)
        say("")<<"WARNING: UPC++ GPU support is compiled-in, but could not find sufficient GPU support at runtime.";
    }
    #endif

    if(me == 0) say("")<<"Running with devices="<<dev_n;
    
    // buf[rank][1+device][shadow=0|1]
    std::array<std::array<any_ptr<int>,2>,1+max_dev_n> buf[2];

    if(me < 2) {
      buf[me][0][0] = upcxx::new_array<int>(1<<20);
      buf[me][0][1] = upcxx::new_array<int>(1<<20);
      for(int i=0; i < 1<<20; i++)
        buf[me][0][0].local()[i] = (i%(1<<17)%10) + (i>>17)*10 + (0*100) + (me*1000);
    }

    #ifdef DEVICE
      Device* gpu[max_dev_n];
      device_allocator<Device>* seg[max_dev_n];
      for(int dev=1; dev < 1+dev_n; dev++) {
        if(me < 2) {
          gpu[dev-1] = new Device(dev-1);
          seg[dev-1] = new device_allocator<Device>(*gpu[dev-1], 32<<20);

          buf[me][dev][0] = seg[dev-1]->allocate<int>(1<<20);
          buf[me][dev][1] = seg[dev-1]->allocate<int>(1<<20);

          int *tmp = new int[1<<20];
          for(int i=0; i < 1<<20; i++)
            tmp[i] = (i%(1<<17)%10) + (i>>17)*10 + (dev*100) + (me*1000);
          DEVICE_SET(dev-1);
          DEVICE_MEMCPY_H2D(
              seg[dev-1]->local( upcxx::static_kind_cast<Device::kind>(buf[me][dev][0]) ),
              tmp, sizeof(int)<<20
          );
          DEVICE_SYNC();
          delete[] tmp;
        }
        else {
          gpu[dev-1] = new Device(Device::invalid_device_id);
          seg[dev-1] = new device_allocator<Device>(*gpu[dev-1], 0);
        }
      }
    #endif
    
    upcxx::broadcast(&buf[0], 1, 0).wait();
    upcxx::broadcast(&buf[1], 1, 1).wait();

    persona per_other;
    persona *pers[2] = { &upcxx::master_persona(), 
                       #if UPCXX_THREADMODE // issue #423
                         &per_other
                       #else
                         &upcxx::master_persona()
                       #endif
                       };

    for(int initiator=0; initiator < upcxx::rank_n(); initiator++) {
      for(int per=0; per < 2; per++) {
        persona_scope pscope(*pers[per]);
        
        if(me == initiator) {
          for(int round=0; round < rounds; round++) {
            // Logically, ranks 0 and 1 each has one buffer per GPU plus one for
            // the shared segment. The logical buffers are globally ordered in a
            // ring. Each logical buffer has a "shadow" buffer used for double
            // buffering. The following loop issues copy's to rotate the contents
            // of the buffers into the shadows. Rotation is at the granularity of
            // "parts" where a part is 1<<17 elements (therefor there are 8 parts
            // in a buffer of 1<<20 elements).
          
            future<> all = upcxx::make_future();
            
            for(int dr=0; dr < 2; dr++) { // dest rank loop
              for(int dd=0; dd < 1+dev_n; dd++) { // dest dev loop
                for(int dp=0; dp < 8; dp++) { // dest part loop
                  // compute source rank,dev,part using overflowing increment per round
                  int sp = dp, sd = dd, sr = dr;
                  for(int r=0;  r < round+1; r++) {
                    sp = (sp + 1) % 8;
                    sd = (sd + (sp == 0 ? 1 : 0)) % (1+dev_n);
                    sr = (sr + (sd == 0 ? 1 : 0)) % 2;
                  }

                  // use round%2 to determine which buffer is logical and which is shadow
                  auto src = buf[sr][sd][(round+0)%2] + (sp<<17);
                  auto dst = buf[dr][dd][(round+1)%2] + (dp<<17);
                  all = upcxx::when_all(all,
                    upcxx::copy(src, dst, 1<<17)
                    .then([=,&pers]() {
                      UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == pers[per]);
                    })
                  );
                }
              }
            }

            all.wait();
            say()<<"done round="<<round<<" initiator="<<initiator;
          }
        }
        
        upcxx::barrier();
      }

      UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
      UPCXX_ASSERT_ALWAYS(upcxx::master_persona().active_with_caller());
    }

    if(me < 2) {
      for(int dd=0; dd < 1+dev_n; dd++) { // dev loop
        for(int dp=0; dp < 8; dp++) { // part loop
          int sp = dp, sd = dd, sr = me;
          // compute source part,dev,rank for all rounds over all initiators
          for(int initiator=0; initiator < upcxx::rank_n(); initiator++) {
            for(int per=0; per < 2; per++) {
              for(int round=0; round < rounds; round++) {
                for(int r=0;  r < round+1; r++) {
                  sp = (sp + 1) % 8;
                  sd = (sd + (sp == 0 ? 1 : 0)) % (1+dev_n);
                  sr = (sr + (sd == 0 ? 1 : 0)) % 2;
                }
              }
            }
          }

          int *tmp;
          if(dd == 0)
            tmp = buf[me][dd][rounds%2].local() + (dp<<17);
          else {
          #ifdef DEVICE
            tmp = new int[1<<17];
            DEVICE_SET(dd-1);
            DEVICE_MEMCPY_D2H(tmp,
                seg[dd-1]->local( static_kind_cast<Device::kind>(buf[me][dd][rounds%2]) ) + (dp<<17),
                sizeof(int)<<17
            );
          #endif
          }
          
          for(int i=0; i < 1<<17; i++) {
            int expect = (i%10) + (sp*10) + (sd*100) + (sr*1000);
            UPCXX_ASSERT_ALWAYS(tmp[i] == expect, "Expected "<<expect<<" got "<<tmp[i]);
          }

          if(dd != 0)
            delete[] tmp;
        }
      }
    }
    
    upcxx::barrier();

    if(me < 2) {
      upcxx::delete_array(upcxx::static_kind_cast<memory_kind::host>(buf[me][0][0]));
      upcxx::delete_array(upcxx::static_kind_cast<memory_kind::host>(buf[me][0][1]));
    }
    
    #ifdef DEVICE
      for(int dev=1; dev < 1+dev_n; dev++) {
        if(me < 2) {
          seg[dev-1]->deallocate(upcxx::static_kind_cast<Device::kind>(buf[me][dev][0]));
          seg[dev-1]->deallocate(upcxx::static_kind_cast<Device::kind>(buf[me][dev][1]));
        }
        gpu[dev-1]->destroy();
        delete gpu[dev-1];
        delete seg[dev-1]; // delete segment after device since that's historically buggy in implementation
      }
    #endif
  }
    
  print_test_success();
  UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
  
  upcxx::finalize();
  return 0;
}
