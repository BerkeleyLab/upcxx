#include <stdio.h>
#include "kernels.hpp"

#define CHECK_CUDA(func) { \
    cudaError_t err = (func); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA Error @ %s:%d - %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
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

    CHECK_CUDA(cudaSetDevice(dev));
    init_kernel<<<blocks_per_grid, threads_per_block>>>(dA, dB, N);
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaGetLastError());
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

    CHECK_CUDA(cudaSetDevice(dev));
    vecadd_kernel<<<blocks_per_grid, threads_per_block>>>(dA + start, dB + start,
            dC + start, N);
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaGetLastError());
}
