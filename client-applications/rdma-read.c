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

#define XFER_SIZE (128 * 1024 )

int main(int argc, char **argv)
{
	int fd, ret;
	uint32_t *dst;
	struct picoevb_rdma_c2h_dma dma_params;

	if (argc != 1) {
		fprintf(stderr, "usage: rdma-malloc\n");
		return 1;
	}

	fd = open("/dev/picoevb", O_RDWR);
	if (fd < 0) {
		perror("open() failed");
		return 1;
	}

	dst = (uint32_t *) calloc(XFER_SIZE, 1);
	if (!dst) {
		fprintf(stderr, "malloc(dst) failed\n");
		return 1;
	}

	dma_params.src = 0;
	dma_params.dst = (__u64)dst;
	dma_params.len = XFER_SIZE;
	dma_params.flags = 0;
	ret = ioctl(fd, PICOEVB_IOC_C2H_DMA, &dma_params);
	if (ret != 0) {
		fprintf(stderr, "ioctl(DMA) failed: %d\n", ret);
		perror("ioctl() failed");
		return 1;
	}
	for(unsigned i = 0; i < (XFER_SIZE/sizeof(uint32_t)); ++i) {
		if( (i % 16) == 0) {
			printf("\n[%08x] ", i);
		}
		printf("%08x ", dst[i]);
	}
	printf("\n");

	free(dst);

	ret = close(fd);
	if (ret < 0) {
		perror("close() failed");
		return 1;
	}

	return 0;
}
