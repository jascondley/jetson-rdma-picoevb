/*
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../kernel-module/picoevb-rdma-ioctl.h"

#define MAX_TRANSFER_SIZE (8 * 1024 * 1024)

int main(int argc, char **argv)
{
	int fd, ret;
	struct picoevb_rdma_card_info card_info;
	uint64_t transfer_size;
	void *src;
	struct picoevb_rdma_h2c_dma dma_params;
	struct timespec tvs, tve;
	uint64_t tdelta_us;

	if (argc != 1) {
		fprintf(stderr, "usage: rdma-h2c-perf\n");
		return 1;
	}

	fd = open("/dev/picoevb", O_RDWR);
	if (fd < 0) {
		perror("open() failed");
		return 1;
	}

	int CHUNK_SAMPLES = log(MAX_TRANSFER_SIZE) / log(2);
	int NUM_AVERAGES = 100;   // Number of tests at each chunk size

	uint64_t len_array[CHUNK_SAMPLES];
	for (int i = 0; i < CHUNK_SAMPLES; i++) {
		len_array[i] = pow(2, i+1);
		if (len_array[i] > MAX_TRANSFER_SIZE)
			len_array[i] = MAX_TRANSFER_SIZE;
	}

	uint64_t time_array[CHUNK_SAMPLES];
	for (int i = 0; i < CHUNK_SAMPLES; i++)
		time_array[i] = 0;

	for (int i = 0; i < CHUNK_SAMPLES; i++) {
		transfer_size = len_array[i];
		printf("Test transfer size = %lu\n", transfer_size);

		src = calloc(transfer_size, 1);
		if (!src) {
			fprintf(stderr, "malloc(src) failed\n");
			return 1;
		}

		for (int j = 0; j < NUM_AVERAGES; j++) {
			dma_params.src = (__u64)src;
			dma_params.dst = 0;
			dma_params.len = transfer_size;
			dma_params.flags = 0;
			clock_gettime(CLOCK_REALTIME, &tvs);
			ret = ioctl(fd, PICOEVB_IOC_H2C_DMA, &dma_params);
			if (ret < 0) {
				fprintf(stderr, "ioctl(DMA) failed: %d\n", ret);
				perror("ioctl() failed");
				return 1;
			}

			clock_gettime(CLOCK_REALTIME, &tve);

			tdelta_us = ((tve.tv_sec - tvs.tv_sec) * 1000000) + (tve.tv_nsec - tvs.tv_nsec)/1000;
			time_array[i] = time_array[i] + tdelta_us;
		}

		free(src);
	}

	for (int i = 0; i < CHUNK_SAMPLES; i++) {
		printf("Bytes:%lu usecs:%lu MB/s:%lf\n", len_array[i], time_array[i],
		       (double)len_array[i]/ ((double)time_array[i] / NUM_AVERAGES));
	}

	ret = close(fd);
	if (ret < 0) {
		perror("close() failed");
		return 1;
	}

	return 0;
}

