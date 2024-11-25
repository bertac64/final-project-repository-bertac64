/**
 * middle.h - Header delle funzioni di interfaccia al middleware FPGA.
 *
 * (C) 2008 GGH srl per Igea SpA
 *
 */
#ifndef MIDDLE_H
#define MIDDLE_H
#include "fpga.h"
#define PARS_BULK_BASE	TDATA_OFFSET	//0xFC000	// Base per il trasferimento parametri

void reset_watchdog(void);
int reset_hardware(void);
int init_hardware(void);
int start_power(int32_t mode);
int32_t explain_error(const char *msg);

#endif /* MIDDLE_H */
