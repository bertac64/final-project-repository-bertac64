/*
 * driver.c
 *
 *  Created on: 28 Oct 2024
 *      Author: bertac64
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <byteswap.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>

/* local libraries */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "epxcore.h"
#include "driver.h"

#define PAGE_SIZE ((size_t)getpagesize())
#define PAGE_MASK ((uint64_t)(long)~(PAGE_SIZE - 1))

extern volatile uint32_t *mm;
extern Memory SRAM;


int epsmdrv_open()
{
	SRAM.fd_dev = open ( EPSMD_DEV_NAME, O_RDWR);
	if ( SRAM.fd_dev < 0 ) {
		log_error("open(%s) failed (%d)\n", EPSMD_DEV_NAME, errno);
		return 1;
	}
	memopen( 0x20000000, 0x20000000 );

	return 0;
}

int epsmdrv_close()
{
	close(SRAM.fd_dev);
	SRAM.fd_dev = -1;

	memclose(0x20000000);
	return 0;
}

int64_t writeregister(uint32_t offset, uint32_t value){
	struct epcore_regacc reg;

	reg.address = offset*4;
	reg.data	= value;

	if ( ioctl(SRAM.fd_dev, EPSMD_IOCTL_WRITE, &reg) < 0 ) {
		log_error("EPSMD_IOCTL_WRITE failed (%d)\n", errno);
		return -1;
	}
	return (0);
}

int64_t readregister(uint32_t offset){
	struct epcore_regacc reg;

	reg.address = offset*4;
	reg.data    = 0xDEADBEEF;

	if ( ioctl(SRAM.fd_dev, EPSMD_IOCTL_READ, &reg) < 0 ) {
		log_error("EPSMD_IOCTL_WRITE failed (%d)\n", errno);
		return -1;
	}
	return reg.data;
}

int64_t memopen(uint32_t rambase, size_t dim){
	off_t base = 0;
	int64_t retval = 0;

	base = (off_t)rambase & PG_MASK;
	printf("MMAP : %08zX : %08zX\n", (long unsigned int)base, dim); fflush(stdout);

	/* opening the file descriptor to get access to the memory */
	SRAM.fd_mem = open("/dev/mem", O_RDWR);
	if (SRAM.fd_mem < 0) {
		log_error("open(/dev/mem) failed (%d)\n", errno);
		return -1;
	}

	mm = (volatile uint32_t *)mmap(NULL, (size_t)(dim), PROT_READ|PROT_WRITE, MAP_SHARED,
			SRAM.fd_mem, base );
	if (mm == MAP_FAILED) {
		printf("mmap64(0x%zx@0x%zx) failed (%d:%s)\n",
				dim, base, errno, strerror(errno));
		retval = -1;
	}
	return retval;
}

void memclose(size_t dim){

	munmap((void *)mm, (size_t)(dim*8));
}
/* RAM writing
 * Returns the written data
 */

int32_t writemem(uint32_t offset, uint32_t value){
	int32_t retval = 0;

//	printf("writemem: mm: %08X - offset: %08X \n", (uint32_t)mm, offset);
	mm[offset] = value;
	retval = value;

	return (retval);
}

/* RAM reading
 * Returns the read data
 */
int32_t readmem(uint32_t offset){
	uint32_t value;
	int32_t retval = 0;

	value = mm[offset];
	retval = value;

	return (retval);
}

