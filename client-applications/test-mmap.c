#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "../kernel-module/picoevb-rdma-ioctl.h"

#define MAP_SIZE 1024
int main(int argc, char **argv)
{
	int fd, ret;
	void *map_base;
	
	if (argc != 1) {
		fprintf(stderr, "usage: test-mmap\n");
		exit(1);
	}

	/* First device should work anywhere with the appropriate path.
	 * Second device is for use with the pcioevb rdma driver
	 * Third device is for use with the XDMA driver
	 */
	//fd = open("/sys/devices/141a0000.pcie/pci0005:00/0005:00:00.0/0005:01:00.0/resource0", O_RDWR | O_SYNC);
	fd = open("/dev/picoevb_user", O_RDWR | O_SYNC);
	//fd = open("/dev/xdma0_user", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open() failed");
		exit(1);
	}

	map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if( map_base == (void *)-1) {
		perror("mmap failed\n");
		exit(1);
	}
	*((unsigned *) map_base) = 0xABCDEF;
	*((unsigned *) map_base) = 0xABCDEF;
	munmap(map_base, MAP_SIZE);
}


