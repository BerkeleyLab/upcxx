#ifndef _KERNELS_HPP
#define _KERNELS_HPP

void initialize_device_arrays(double *dA, double *dB, int N, int dev);
void gpu_vector_sum(double *dA, double *dB, double *dC, int start, int end, int dev);

#endif // _KERNELS_HPP
