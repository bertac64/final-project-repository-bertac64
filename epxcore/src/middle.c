/**
 * middle.c - funzioni "middleware" tra i comandi di command.c/bite.c e
 * 				il basso livello di fpga.cpp.
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
 *	DESCRIZIONE
 *		Resetta il watchdog; va chiamato il piu' frequentemente possibile.
 */
void reset_watchdog(void)
{
#ifndef SIMULAZIONE
	if (fpga_poke((fpga_addr_t)r_Command, FC_WATCHDOG) != 0)
		comm_error("Can't reset watchdog");
#endif
}

/**
 *	DESCRIZIONE
 *		Resetta l'hardware
 *	VALORI DI RITORNO
 *		0 per successo, -1 + log per errore.
 */
int reset_hardware(void)
{
	if (fpga_poke((fpga_addr_t)r_Command, FC_HW_RESET) != 0) {
		comm_error("Can't reset hardware (1st)");
		return -1;
	}
	msleep(10);
	if (fpga_poke((fpga_addr_t)r_Command, FC_HW_RESET) != 0) {
		comm_error("Can't reset hardware (2nd)");
		return -1;
	}
	msleep(10);
	if (fpga_poke((fpga_addr_t)r_Command, FC_AMP_OFF) != 0) {
		comm_error("Can't turn amp off");
		return -1;
	}
	log_warning("Hardware System Reset: done!");

	return 0;
}

/**
 *	DESCRIZIONE
 *		Inizializza l'hardware e lo predispone al funzionamento di regime.
 *	VALORI DI RITORNO
 *		0 per successo, -1 + log per errore.
 */
int init_hardware(void)
{

	/* Accensione amplificatori */
	if (fpga_poke((fpga_addr_t)r_Command, FC_AMP_ON) != 0) {
		comm_error("can't turn on amplifiers");
		return -1;
	}

	log_warning("Hardware System Init: done!");
	return 0;
}


/*
 * DESCRIZIONE
 * 	Inizializza la parte di potenza.
 *
 * VALORI DI RITORNO
 * 	0 per successo, -1 per fallimento.
 */
int start_power(int32_t mode	/* Modo di invocazione (TRUE = in bite) */)
{
	char tmps[MAXSTR];
	char * fname_calib = NULL;

	(void)mode;

	reset_watchdog();

	/* Verifica il registro di stato della FPGA */
	int state, try;

	if ((state = fpga_peek(r_State)) == -1) {
		comm_error("Can't get FPGA state");
		return -1;
	}

	try = 0;
	while (state == 0x0000) {

		if (try++ >= 3){
			log_error("Wrong FPGA state (0000).");
			return -1;
		}

		log_warning("Problems in FPGA init: trying again");


		msleep(1000);

		if ((state = fpga_peek(r_State)) == -1) {
			comm_error("Can't get FPGA state");
			return -1;
		}

		msleep(500);
	}

	reset_watchdog();

	return 0;
}


/**
 * DESCRIZIONE
 * 	Fai un log completo con lo stato.
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

