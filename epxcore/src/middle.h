/**
 * middle.h - Header of the interface functions to the middleware FPGA.
 *
 * (C) bertac64
 *
 */
#ifndef MIDDLE_H
#define MIDDLE_H
#include "fpga.h"
#define PARS_BULK_BASE	TDATA_OFFSET	// Base for the treatment parameters transfer

void reset_watchdog(void);
int reset_hardware(void);
int init_hardware(void);
int start_power(int32_t mode);
int32_t explain_error(const char *msg);

#endif /* MIDDLE_H */
