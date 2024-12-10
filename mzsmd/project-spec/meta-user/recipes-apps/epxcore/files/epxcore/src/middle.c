/**
 * middle.c - "middleware" functions between commands and the
 * 				low level of fpga.cpp.
 *
 * (C) 2024 bertac64
 *
 */

/* POSIX.1 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* Librerie locali */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "fpga.h"
#include "errors.h"
#include "epxcore.h"
#include "main.h"
#include "command.h"
#include "util.h"
#include "sharedVar.h"
#include "middle.h"


/**
 *	DESCRIPTION
 *		Hardware reset
 *	RETURN VALUES
 *		0 on success, -1 error.
 */
int reset_hardware(void)
{
	if (fpga_poke((fpga_addr_t)r_Command, FC_HW_RESET) != 0) {
		comm_error("Can't reset hardware (1st)");
		return -1;
	}
	msleep(10);
/*	if (fpga_poke((fpga_addr_t)r_Command, FC_HW_RESET) != 0) {
		comm_error("Can't reset hardware (2nd)");
		return -1;
	}
	msleep(10);
	if (fpga_poke((fpga_addr_t)r_Command, FC_AMP_OFF) != 0) {
		comm_error("Can't turn amp off");
		return -1;
	}*/
	log_warning("Hardware System Reset: done!");

	return 0;
}

/**
 *	DESCRIPTION
 *		Hardware intialization
 *	RETURN VALUES
 *		0 on success, -1 error.
 */
int init_hardware(void)
{
	log_warning("Hardware System Init: done!");
	return 0;
}


/*
 * DESCRIPTION
 * 	Power Unit initialòization.
 *
 *	RETURN VALUES
 *		0 on success, -1 error.
 */
int start_power(int32_t mode)
{

	(void)mode;

	//reset_watchdog();

	/* Verification of the FPGA status register*/
	int state;

	if ((state = fpga_peek(r_State)) == -1) {
		comm_error("Can't get FPGA state");
		return -1;
	}
	
	return 0;
}


/**
 * DESCRIPTION
 * 	Complete log with status.
 */

int32_t explain_error(const char *msg)
{
	fpga_data_t state = fpga_peek(r_State);
	if (state == -1)
		return -1;
	uint16_t fpga_state = state & 0x0000FFFF;
	uint16_t fpga_state2 = (state>>16);

	fpga_data_t alarm = fpga_peek(r_Alarm);
	if (alarm == -1)
		return -1;
	uint16_t fpga_alarm = (alarm  & 0x0000FFFF);

	log_error("%s S:%04X S2:%04X A:%04X",
									msg, fpga_state, fpga_state2, fpga_alarm);

	return 0;
}

