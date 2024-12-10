/*
 * driver.h
 *
 *  Created on: 28 Oct 2024
 *      Author: bertac64
 */

#ifndef DRIVER_H_
#define DRIVER_H_



// ----------------------------------------------------------------------------------------
#define EPSMD_IOCTL_READ 0x100
#define EPSMD_IOCTL_WRITE 0x101
// ----------------------------------------------------------------------------------------
struct epcore_regacc {
    unsigned int address;
    unsigned int data;
};
// ----------------------------------------------------------------------------------------
#define EPSMD_DEV_NAME "/dev/epsmdrv0"
// ----------------------------------------------------------------------------------------





#define CUSTOM_IP_BASEADDR 0x43c00000
#define SRAM_IP_HIGHADDR 0x3FFFFFFF		// 1GB
#define SRAM_IP_BASEADDR 0x20000000		// 512MB
#define SRAM_TR_BASEADDR 0x3FFF0000		// 64KB (16K Word 32 bit word)
#define RAMSIZE ((SRAM_IP_HIGHADDR + 1) - SRAM_IP_BASEADDR)		// 512MB (128M 32 bit word)
#define PGSIZE 4096*32
#define PG_MASK ((uint64_t)(long)~(PGSIZE - 1))
#define FPGA_MEMORY_SIZE (uint32_t)((SRAM_IP_HIGHADDR + 1) - SRAM_IP_BASEADDR)	// 32 bit addressable space (1024M)
#define DATA_BUFFER_SIZE (uint32_t)(SRAM_IP_HIGHADDR-SRAM_TR_BASEADDR+1)

int epsmdrv_open();
int epsmdrv_close();

int64_t writeregister(uint32_t offset, uint32_t value);
int64_t readregister(uint32_t offset);
int32_t writemem(uint32_t offset, uint32_t value);
int32_t readmem(uint32_t offset);
int64_t memopen(uint32_t rambase, size_t dim);
void memclose(size_t dim);

#endif /* DRIVER_H_ */
