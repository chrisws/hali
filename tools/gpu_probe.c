#include <cuda_runtime.h>
#include <stdio.h>

/*
 * gpu_probe — minimal VRAM detector for hali setup.sh
 *
 * Prints total GPU VRAM in megabytes to stdout.
 * Exits 0 on success, 1 if no GPU found.
 *
 * Build:  nvcc -o gpu_probe gpu_probe.c -lcudart
 * Run:    ./gpu_probe   →  "8192"
 */

int main(void) {
  int count = 0;
  if (cudaGetDeviceCount(&count) != cudaSuccess) {
    return 1;
  }
  if (count == 0) {
    return 1;
  }
  cudaDeviceProp prop;
  if (cudaGetDeviceProperties(&prop, 0) != cudaSuccess) {
    return 1;
  }
  /* totalGlobalMem is in bytes; print as MB */
  unsigned long mb = (unsigned long)(prop.totalGlobalMem / (1024UL * 1024UL));
  printf("%lu\n", mb);
  return 0;
}
