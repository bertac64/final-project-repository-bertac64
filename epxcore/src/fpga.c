/**
 * fpga.c - Access functions for the FPGA - sinchronized with Mutex.
 *
 * (C) bertac64
 *
 */

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "fpga.h"

//#define DEBUG			1		// debug enable
#define ENDIANNESS_BUG	1		// To change data from short to big endian
								
#define BULKBLOCK	2*1024*1024	// Max memory dimension 
#define MDELAY		20			// delay time between setting registers and r/w bulk start
#define USB_TOUT	1000		// Timeout r/w USB bulk in ms
#define MINBLOCK	512			// Min data block for read/write (in word)
#define RW_FRAGMENT	32768 		// R/W block dimention
#define	RW_USPAUSE	0			// pause between R/W in us
#define	NRETRY		10			// tentative number in case of error
#define	NRETRY_INIT	8			// tentative number for fpga init

#define min(a,b)	(((a)<(b))?(a):(b))

/*****************************************************************************/
// static functions

static pthread_mutex_t S_mutex = PTHREAD_MUTEX_INITIALIZER;


///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Main test program, program entry point.
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * DESCRIPTION
 * 	Reads a word from the FPGA at the addr address.
 *
 * RETURN
 * 	Number of read words or -1 if error.
 */
fpga_data_t fpga_peek(uint32_t addr)
{
	fpga_data_t data;

	pthread_mutex_lock(&S_mutex);
	data = readregister(addr);
	pthread_mutex_unlock(&S_mutex);
	return data;
}

/**
 * DESCRIPTION
 * 	Writes a word into the FPGA at the addr address.
 *
 * RETURN
 * 	0 for success, -1 for error.
 */

int fpga_poke(uint32_t addr, uint32_t data)
{
	int ret = 0;

	pthread_mutex_lock(&S_mutex);
	writeregister(addr, data);
	pthread_mutex_unlock(&S_mutex);
	return ret;
}

ssize_t fpga_write(uint32_t addr, uint32_t *data, uint32_t count){
	uint32_t i=0;

	for(i=0;i<count;i++){
//		printf("offset: %08X - data: %08X\n",(addr+(i*4)), data[i]);
		writemem(addr+i,data[i]);
	}
	return 0;
}

ssize_t fpga_read(uint32_t addr, uint32_t *data, uint32_t count){
	uint32_t i=0;

	for(i=0;i<count;i++){
		data[i] = readmem(addr+i);
//		printf("offset: %08X - data: %08X\n",(addr+(i*4)), data[i]);
	}

	return i;
}


/*************************************************************************************/
/* User-friendly management of registers */

struct DECODER {
	const char * label;
	uint32_t reg;
} S_registers[] = {
	{ "Command",	r_Command },	//IO
	{ "Gpr1",		r_Gpr1 },		//IO
	{ "Version",	r_Version },	//O
	{ "State", 		r_State },		//O
	{ "Alarm", 		r_Alarm },		//O
	{ 0, 0 }
};

const char *fpga_r2s(uint reg)
{
	for (int32_t j=0; S_registers[j].label != 0; j++)
		if (S_registers[j].reg == reg)
			return S_registers[j].label;
	return 0;
}

uint fpga_s2r(const char *label)
{
	for (int32_t j=0; S_registers[j].label != 0; j++)
		if (strcmp(S_registers[j].label, label) == 0)
			return S_registers[j].reg;
	return 0xFFFFFFFF;
}

/**
 * DESCRIPTION
 * 	Makes a scan of the registers of the FPGA; at the beginning status has to
 *	be 0 and every next call the function update its value.
 *	At the end of the iteration, the function returns NULL.
 * RETORN VALUES
 * 	Label of the register + value in val; NULL if error/end table.
 */

const char * fpga_getnextreg(int *status, unsigned int *addr, unsigned int *val)
{
	if ((*status) < 0 || (*status) >= (int)(sizeof(S_registers)/sizeof(struct DECODER)))
		return NULL;

	if (val != NULL) {
		fpga_data_t data = fpga_peek(S_registers[*status].reg);
		if (data < 0)
			return NULL;
		*val = data;
	}

	if (addr != NULL)
		*addr = S_registers[*status].reg;
	return S_registers[(*status)++].label;
}

