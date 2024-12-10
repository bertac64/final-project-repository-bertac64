/**
 * fpga.h -	FPGA access functions.
 *
 *
 */

#ifndef FPGA_H_
#define FPGA_H_

#include "driver.h"

/* data types for the fpga */
typedef int64_t fpga_data_t;	// Data (we use int64_t to return (-1)
typedef uint64_t fpga_addr_t;	// Addresses

#define TDATA_OFFSET		(uint32_t)(SRAM_TR_BASEADDR - SRAM_IP_BASEADDR)	// Offset parametri trattamento
#define TDATA_SIZE			(uint32_t)(((SRAM_IP_HIGHADDR + 1) - SRAM_TR_BASEADDR)/4)	// Spazio a 32 bit parametri trattamento
#define ECG_TRIGGER_CHECK

/* registers address*/
#define r_Command		0x00000000	//IO
#define r_Gpr1			0x00000001	//IO
#define r_State			0x00000008	//O
#define r_Alarm			0x00000009	//O
#define r_State2		0x00000026	//O
#define r_Version		0x0000007F	//O



// Pattern e masks for the status calculation
#define MASK_ALL	0xFFFF
#define MASK_READY	0xFFFF
#define PATT_READY	0x0000


/*****************************************************************************/
/* Comandi */

#define FC_HW_RESET		0x5248	// Hardware Reset
#define FC_WATCHDOG		0x5744	// Watchdog Reset
#define CMD_MT 			0x4D54	// Memory test command

/*****************************************************************************/
/* Prototipi */
#ifdef __cplusplus
extern "C" {
#endif

fpga_data_t fpga_peek(uint32_t addr);
int fpga_poke(uint32_t addr, uint32_t data);
ssize_t fpga_read(uint32_t addr, uint32_t *data, uint32_t count);
ssize_t fpga_write(uint32_t addr, uint32_t *data, uint32_t count);
const char * fpga_getnextreg(int *status, uint32_t *addr, uint32_t *val);

#ifdef __cplusplus
}
#endif
#endif /* FPGA_H_ */
