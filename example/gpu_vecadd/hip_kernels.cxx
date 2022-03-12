#include "hip/hip_runtime.h"
#include <stdio.h>
#include "kernels.hpp"

#define CHECK_HIP(func) { \
    hipError_t err = (func); \
    if (err != hipSuccess) { \
        fprintf(stderr, "HIP Error @ %s:%d - %s\n", __FILE__, __LINE__, hipGetErrorString(err)); \
        abort(); \
    } \
}

__global__ void init_kernel(double *A, double *B, int N) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < N) {
        A[tid] = tid;
        B[tid] = 2 * tid;
    }
}

void initialize_device_arrays(double *dA, double *dB, int N, int dev) {
    int threads_per_block = 128;
    int blocks_per_grid = (N + threads_per_block - 1) / threads_per_block;

    CHECK_HIP(hipSetDevice(dev));
    hipLaunchKernelGGL(init_kernel, blocks_per_grid, threads_per_block, 0, 0, dA, dB, N);
    CHECK_HIP(hipDeviceSynchronize());
    CHECK_HIP(hipGetLastError());
}

__global__ void vecadd_kernel(double *A, double *B, double *C, int N) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < N) {
        C[tid] = A[tid] + B[tid];
    }
}

void gpu_vector_sum(double *dA, double *dB, double *dC, int start, int end, int dev) {
    int N = end - start;
    int threads_per_block = 128;
    int blocks_per_grid = (N + threads_per_block - 1) / threads_per_block;

    CHECK_HIP(hipSetDevice(dev));
    hipLaunchKernelGGL(vecadd_kernel, blocks_per_grid, threads_per_block, 0, 0, dA + start, dB + start,
            dC + start, N);
    CHECK_HIP(hipDeviceSynchronize());
    CHECK_HIP(hipGetLastError());
}
