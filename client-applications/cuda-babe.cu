#include <cuda.h>
#include <cuda_runtime_api.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../kernel-module/picoevb-rdma-ioctl.h"

#define SURFACE_W  256
#define SURFACE_H  256
#define SURFACE_SIZE  (SURFACE_W * SURFACE_H)

#define OFFSET(x, y)  (((y) * SURFACE_W) + x)
#define DATA(x, y)  (((y & 0xffff) << 16) | ((x) & 0xffff))

extern "C" __global__ void fill_surface(uint32_t *output, uint32_t xor_val)
{
  unsigned int pos_x = (blockIdx.x * blockDim.x) + threadIdx.x;
  unsigned int pos_y = (blockIdx.y * blockDim.y) + threadIdx.y;

  output[OFFSET(pos_x, pos_y)] = DATA(pos_x, pos_y) ^ xor_val;
}

extern "C" __global__ void reorder_bytes(uint32_t* gpu_data)
{
  unsigned int pos_x = (blockIdx.x * blockDim.x) + threadIdx.x;
  unsigned int pos_y = (blockIdx.y * blockDim.y) + threadIdx.y;

  const uint32_t word = gpu_data[OFFSET(pos_x, pos_y)];
  const uint8_t b0 = word & 0xFF;
  const uint8_t b1 = (word >> 8) & 0xFF;
  const uint8_t b2 = (word >> 16) & 0xFF;
  const uint8_t b3 = (word >> 24) & 0xFF;

  gpu_data[OFFSET(pos_x, pos_y)] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

void HexDump(const uint8_t* bytes, size_t size)
{
  if (!size) return;

  const size_t bytes_per_line = 16;
  const size_t total_lines = ((size - 1) / bytes_per_line) + 1;

  for (size_t line = 0; line < total_lines; ++line) {
    const unsigned int offset = line * bytes_per_line;
    // Show the offset each line:
    printf("%08x", offset);

    // Hex bytes
    for (size_t i = offset; i < offset + bytes_per_line; ++i) {
      // Add a bit of space for visual clarity
      if (i % (bytes_per_line / 2) == 0)
        printf(" ");
      if (i < size)
        printf(" %02x", bytes[i]);
      else
        printf("   ");
    }

    // printable characters
    printf("  ");
    for (size_t i = offset; i < offset + bytes_per_line && i < size; ++i) {
      char c = bytes[i];
      const char first_printable = ' ';
      const char last_printable = '\x7e';
      if (c < first_printable || c > last_printable)
        printf(".");
      else
        printf("%c", c);
    }
    printf("\n");
    fflush(stdout);
  }
}

int main(int argc, char **argv)
{
  cudaError_t ce;
  CUresult cr;
  uint32_t* src_d;
  int fd, ret;
  unsigned int flag = 1;
  struct picoevb_rdma_pin_cuda pin_params_src;
  struct picoevb_rdma_h2c2h_dma dma_params;
  struct picoevb_rdma_unpin_cuda unpin_params_src;

  if (argc != 1) {
    fprintf(stderr, "usage: cuda-babe\n");
    return 1;
  }

  fd = open("/dev/picoevb", O_RDWR);
  if (fd < 0) {
    perror("open() failed");
    return 1;
  }

  ce = cudaHostAlloc(&src_d, SURFACE_SIZE * sizeof(*src_d),
    cudaHostAllocDefault);

  if (ce != cudaSuccess) {
    fprintf(stderr, "Allocation of src_d failed: %d\n", ce);
    return 1;
  }

  cr = cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS,
    (CUdeviceptr)src_d);
  if (cr != CUDA_SUCCESS) {
    fprintf(stderr, "cuPointerSetAttribute(src_d) failed: %d\n", cr);
    return 1;
  }

  pin_params_src.va = (__u64)src_d;
  pin_params_src.size = SURFACE_SIZE * sizeof(*src_d);
  ret = ioctl(fd, PICOEVB_IOC_PIN_CUDA, &pin_params_src);
  if (ret != 0) {
    fprintf(stderr, "ioctl(PIN_CUDA src) failed: ret=%d errno=%d\n", ret, errno);
    return 1;
  }

#if (SURFACE_W < 16) || (SURFACE_H < 16)
#error Grid and block sizes must be shrunk for small surfaces
#endif
#if (SURFACE_W & 15) || (SURFACE_H & 15)
#error Grid and block sizes are not a multiple of the surface size
#endif
  // Here's the missing piece. There needs to be some way to get the
  // memory from FPGA directly to the src_d that we've pinned. I'm going to
  // start by assuming that the memory has to be pinned to DMA to do it.
  dma_params.src = (__u64)src_d;
  dma_params.dst = 0;
  dma_params.len = SURFACE_SIZE * sizeof(*src_d);
  dma_params.flags = 0;
  ret = ioctl(fd, PICOEVB_IOC_C2H_DMA, &dma_params);
  if (ret != 0) {
    fprintf(stderr, "ioctl(DMA) failed: %d\n", ret);
    perror("ioctl() failed");
    return 1;
  }

  unpin_params_src.handle = pin_params_src.handle;
  ret = ioctl(fd, PICOEVB_IOC_UNPIN_CUDA, &unpin_params_src);
  if (ret != 0) {
    fprintf(stderr, "ioctl(UNPIN_CUDA src) failed: %d\n", ret);
    return 1;
  }

  dim3 dimGrid(SURFACE_W / 16, SURFACE_H / 16);
  dim3 dimBlock(16, 16);
  reorder_bytes<<<dimGrid, dimBlock>>>(src_d);

  ce = cudaDeviceSynchronize();
  if (ce != cudaSuccess) {
    fprintf(stderr, "cudaDeviceSynchronize() failed: %d\n", ce);
    return 1;
  }

  // If this works, it's because of some weird zero-copy logic.
  HexDump((uint8_t*)src_d, SURFACE_SIZE * sizeof(*src_d));

  ce = cudaFreeHost(src_d);

  if (ce != cudaSuccess) {
    fprintf(stderr, "Free of src_d failed: %d\n", ce);
    return 1;
  }

  ret = close(fd);
  if (ret < 0) {
    perror("close() failed");
    return 1;
  }
}

