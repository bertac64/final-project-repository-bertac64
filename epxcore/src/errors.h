/*
 * errors.h
 *
 *  Created on: Sep 23, 2009
 *      Author: myuser
 */

#ifndef ERRORS_H_
#define ERRORS_H_

/* Errori del protocollo */
enum e_errCod {
	// Funge da tappo
	e_errOk					= 0,


	/* Codici da 100 a 499 sono riservati alla GUI */

	e_errAsync 				= 500,	/* Errori asincroni */
	e_errAsyncResetHW			= 511,	/* Cannot reset HW. */
	e_errAsyncResetHW_idle			= 512,	/* Cannot reset HW. Idle */
	e_errAsyncReinitHW			= 513,	/* Cannot reinit hardware. */
	e_errAsyncReinitHW_idle			= 514,	/* Cannot reinit hardware. Idle */
	e_errAsyncWatchdog_idle			= 517,	/* Watchdog timeout elapsed: shutting down */
	e_errAsyncPowerSupply_idle		= 519,	/* Power supply failure: shutting down */
	e_errAsyncHWInUnexpectedState	        = 526,	/* HW in unexpected state (state=$1)*/
	e_errDebug 				= 600,	/* Segnalazioni di debug (si usa il $1 per il messaggio) */
	
	e_errFPGA 				= 900,	/* FPGA error */
	e_errFPGACommunications			= 901,	/* HW communications error */
	e_errFPGACommunications_idle 	        = 902,	/* HW communications error. Idle */

	e_errSyntax				= 996,	/* Syntax error */
	e_errState				= 997,	/* Not in proper state */
	e_errParam				= 998,	/* Bad parameter */
	e_errInt				= 999	/* Errore interno */

};

#endif /* ERRORS_H_ */
