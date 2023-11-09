#include "kernels.hpp"
#include <sycl/sycl.hpp>
#include <level_zero/ze_api.h>
#include <sycl/ext/oneapi/backend/level_zero.hpp>

static sycl::device& get_device() {
   static sycl::device instance{sycl::gpu_selector_v};
   return instance;
}

ze_device_handle_t handle_init() {
   ze_device_handle_t handle = sycl::get_native<sycl::backend::ext_oneapi_level_zero>(get_device());
   return handle;
}

void initialize_device_arrays(double *dA, double *dB, int N, int dev_id) {
	sycl::queue q(get_device());
	q.parallel_for(static_cast<size_t>(N), [=](auto i) { dA[i]=i; dB[i]=2*i; });
}

void gpu_vector_sum(double *dA, double *dB, double *dC, int start, int end, int dev_id) {
	sycl::queue q(get_device());
	q.parallel_for(static_cast<size_t>(end-start), [=](auto i) { (dC+start)[i] = (dA+start)[i] + (dB+start)[i]; });
}
