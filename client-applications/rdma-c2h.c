#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "../kernel-module/picoevb-rdma-ioctl.h"

#define MAX_TRANSFER_SIZE (256ULL * 1024ULL)

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
  int fd, ret;
  struct picoevb_rdma_card_info card_info;
  uint64_t transfer_size;
  void *dst;
  struct picoevb_rdma_c2h_dma dma_params;
  struct timeval  tvs, tve;
  uint64_t tdelta_us;

  if (argc != 1) {
    fprintf(stderr, "usage: rdma-malloc\n");
    return 1;
  }

  fd = open("/dev/picoevb", O_RDWR);
  if (fd < 0) {
    perror("open() failed");
    return 1;
  }

  ret = ioctl(fd, PICOEVB_IOC_CARD_INFO, &card_info);
  if (ret != 0) {
    fprintf(stderr, "ioctl(CARD_INFO) failed: %d\n", ret);
    perror("ioctl() failed");
    return 1;
  }
  transfer_size = card_info.fpga_ram_size;
  if (transfer_size > MAX_TRANSFER_SIZE)
    transfer_size = MAX_TRANSFER_SIZE;

  dst = calloc(transfer_size, 1);
  if (!dst) {
    fprintf(stderr, "malloc(dst) failed\n");
    return 1;
  }

  dma_params.src = 0;
  dma_params.dst = (__u64)dst;
  dma_params.len = transfer_size;
  dma_params.flags = 0;
  gettimeofday(&tvs, NULL);
  ret = ioctl(fd, PICOEVB_IOC_C2H_DMA, &dma_params);
  gettimeofday(&tve, NULL);
  if (ret != 0) {
    fprintf(stderr, "ioctl(DMA) failed: %d\n", ret);
    perror("ioctl() failed");
    return 1;
  }

  tdelta_us = ((tve.tv_sec - tvs.tv_sec) * 1000000) + tve.tv_usec - tvs.tv_usec;
  printf("Bytes:%lu usecs:%lu MB/s:%lf\n", transfer_size, tdelta_us,
         (double)transfer_size / (double)tdelta_us);

  HexDump(dst, transfer_size);
  free(dst);

  ret = close(fd);
  if (ret < 0) {
    perror("close() failed");
    return 1;
  }

  return 0;
}
