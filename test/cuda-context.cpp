// This test validates that various UPC++ operations using the
// cuda_device memory kind don't perturb the thread's 
// visible CUDA runtime/driver state in a harmful way.
#include <upcxx/upcxx.hpp>
#include <iostream>
#include <stdlib.h>
#include "util.hpp"
#include <cuda.h>
#include <cuda_runtime.h>

#undef assert
#define assert UPCXX_ASSERT_ALWAYS

bool success = true;

using namespace upcxx;

#define CU_CHECK(expr) do { \
  CUresult res_ = (expr); \
  if (res_ != CUDA_SUCCESS) { \
    const char *errname="", *errstr=""; \
    cuGetErrorName(res_, &errname); \
    cuGetErrorString(res_, &errstr); \
    say() << "CUDA ERROR: " << #expr << " : " << errname << ": " << errstr; \
    abort(); \
  } \
} while(0)
#define CURT_CHECK(expr) do { \
  cudaError_t res_ = (expr); \
  if (res_ != cudaSuccess) { \
    say() << "CUDA ERROR: " << #expr << " : " \
          << cudaGetErrorName(res_) << ": " << cudaGetErrorString(res_); \
    abort(); \
  } \
} while(0)

int dev_n = -1;

CUcontext *allctx;

int test_phase = 0;

void checkdev() {

  // cudaSetDevice() overwrites the top of the driver context stack
  // see https://stackoverflow.com/questions/62877646
  // Which means the two levels of CUDA thread "state" interfere with each other
  // As such, we test each form of manipulation in isolation
  
  if (test_phase == 0) { // CUDA Runtime API active device 

    static int curr_dev = -1;
    int dev = -1;
    CURT_CHECK(cudaGetDevice(&dev));
    if (curr_dev >= 0) assert(dev == curr_dev);
    curr_dev = (curr_dev + 1 ) % dev_n;
    assert(curr_dev >= 0 && curr_dev < dev_n);
    CURT_CHECK(cudaSetDevice(curr_dev));
    dev = -1;
    CURT_CHECK(cudaGetDevice(&dev));
    assert(dev == curr_dev);

  } else { // CUDA Driver API contexts

    static int curr_ctx = -1;
    CUcontext ctx = 0;
    CUdevice cdev = -1;
    if (curr_ctx < 0) { // first call
      curr_ctx = 0; 
    } else {
      CU_CHECK(cuCtxGetCurrent(&ctx));
      assert(ctx == allctx[curr_ctx]);
      CU_CHECK(cuCtxGetDevice(&cdev));
      assert(cdev == curr_ctx);

      ctx = 0;
      CU_CHECK(cuCtxPopCurrent(&ctx));
      assert(ctx == allctx[curr_ctx]);
    } 
    curr_ctx = (curr_ctx + dev_n - 1) % dev_n;
    assert(curr_ctx >= 0 && curr_ctx < dev_n);
    CU_CHECK(cuCtxPushCurrent(allctx[curr_ctx]));

    ctx = 0; cdev = -1;
    CU_CHECK(cuCtxGetCurrent(&ctx));
    assert(ctx == allctx[curr_ctx]);
    CU_CHECK(cuCtxGetDevice(&cdev));
    assert(cdev == curr_ctx);

  }

}


int main(int argc, char **argv) {
  upcxx::init();

  print_test_header();

  CU_CHECK(cuInit(0));

  CURT_CHECK(cudaGetDeviceCount(&dev_n));
  #if UPCXX_VERSION >= 20210903
    assert(dev_n == cuda_device::device_n());
  #endif

  say() << "Running with " << dev_n << " CUDA GPUs";

  allctx = new CUcontext[dev_n];
  for (int i=0; i < dev_n; i++) {
    CU_CHECK(cuDevicePrimaryCtxRetain(&allctx[i],i));
  }

  for (test_phase = 0; test_phase < 2; test_phase++) {
    barrier();
    if (!rank_me()) say("") << "Testing CUDA " << (test_phase?"Driver":"Runtime") << " API...";
    barrier();

    for (int i=0; i < 20; i++) {
      checkdev();
    }

    using gp_t = global_ptr<int,memory_kind::cuda_device>;
    gp_t last_gp;
    cuda_device *last_dev = nullptr;
    device_allocator<cuda_device> *last_alloc;
    for (int i=0; i < 15; i++) {
      checkdev();
      auto dev = new cuda_device(i%dev_n);
      auto alloc = new device_allocator<cuda_device>(*dev, 4*1024*1024);
      checkdev();

      size_t sz = 1024;
      auto gp = alloc->allocate<int>(sz);
      auto gp2 = alloc->allocate<int>(sz);
      dist_object<gp_t> dobj(gp2);
      auto gp3 = dobj.fetch((rank_me()+1)%rank_n()).wait();
      say() << "i="<<i<<": " << gp;
      barrier();

      copy(gp, gp2, sz).wait();
      checkdev();

      barrier();
      copy(gp, gp3, sz).wait();
      checkdev();
      copy(gp3, gp, sz).wait();
      checkdev();
      barrier();
      checkdev();

      if (last_gp) {
        copy(gp, last_gp, sz).wait();
        checkdev();
        copy(last_gp, gp, sz).wait();
        checkdev();

        barrier();
        copy(gp3, last_gp, sz).wait();
        checkdev();
        copy(last_gp, gp3, sz).wait();
        checkdev();
        barrier();
        checkdev();
      }
      copy(gp, gp2, sz).wait();
      checkdev();
     // SKIP_DEVICE_FREE: workaround bug 4396 by leaking all the device segments
     #if !SKIP_DEVICE_FREE
      if (last_dev) {
        last_dev->destroy();
        checkdev();
        delete last_dev;
        delete last_alloc;
      }
     #endif
      last_dev = dev;
      last_alloc = alloc;
      last_gp = gp;
      checkdev();
      barrier();
    }
   #if !SKIP_DEVICE_FREE
    last_dev->destroy();
    checkdev();
    delete last_dev;
    delete last_alloc;
   #endif
    checkdev();

    barrier();
  } // test_phase

  print_test_success(success);
  checkdev();

  upcxx::finalize();
  checkdev();
  delete [] allctx;

  return 0;
}
