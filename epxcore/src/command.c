/**
 * command.c - Server commands.
 *
 * (C) bertac64
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <malloc.h>

/* Local Libraries */
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

// Comment to have values ​​in logical levels
#define REALVALUES

/* ========================================================================= */
/* Internal Prototypes */

/* ========================================================================= */
/* static global */
static ssize_t fCmd_nop (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_quit (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_info (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_poke (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_peek (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_read (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_write (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_fill (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_abort (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_idle (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);
static ssize_t fCmd_hwreset (t_stato *p_stato, t_cmd *pCmd, \
	char *buffer, const char **tok, int32_t ntok, int32_t cnt);



/* main command table */
static t_cmd S_command[] = {
	/* cmd	  npar async   idly	Cmd_func	Start_thread */
	{ "nop",	0, FALSE,	0,	fCmd_nop,	NULL },
	{ "quit",	0, FALSE,	0,	fCmd_quit,	NULL },
	{ "info",	0, FALSE,	0,	fCmd_info,	NULL },

	// Comandi ad uso debug
	{ "poke",	2, FALSE,	1,	fCmd_poke,	NULL },
	{ "peek",	1, FALSE,	0,	fCmd_peek,	NULL },
	{ "read",	2, FALSE,	1,	fCmd_read,	NULL },
	{ "write",	-1,FALSE,	1,	fCmd_write,	NULL },		// N. variabile di pars
	{ "fill",	2, FALSE,	1,	fCmd_fill,	NULL },
	
	{ "idle",	0, FALSE,	1,	fCmd_idle,	NULL },
	{ "abort",	0, FALSE,	1,	fCmd_abort,	NULL },
	
	{ "hwreset",	0, FALSE,	0,	fCmd_hwreset,	NULL },

	{ NULL,		0, FALSE,	0,	NULL,		NULL }		/* tappo */
};

/******************************************************************************/

/**
* Processes a command; handles syntax parsing and executes immediate or threaded
* commands as needed.
* Returns 0 for success or -1 for failure; diagnostics via log_.
*/
ssize_t elaboraCmd(t_stato *p_stato, char *buffer, const char *cmd,
		__attribute__ ((unused)) size_t nb)
{
	char *tok[MAXPRO_TOK];
	char *tok2func[MAXPRO_TOK];
	int32_t cnt;
	int32_t x;
	int32_t ntok, nparwaited, ret;
	bool found = FALSE;
	t_cmd *pCmd;

	/* Separate command parameters */
	log_info("Cmd: %s",cmd);
	ntok = argTok(cmd, tok, MAXPRO_TOK);
	if (ntok < 0) {
		log_warning("errore cmd=%s argTok()=%d", cmd, ntok);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL, "tokenizer failure",
				-1, e_errSyntax);
		goto exitProc;
	}

	ret = 0;

	/* If there are no parameters exit silently */
	if (ntok < 1)
		goto exitProc;

	cmd = tok[0];

	found=FALSE;
	for (pCmd = S_command; pCmd->cmd != NULL; pCmd++) {
		if (strcmp(cmd, pCmd->cmd)!=0)
			continue;

		/* Commands marked idly are stuck in e_idle state */
		if (pCmd->idly && get_state() == e_idle) {
			log_warning("cmd=\"%s\" in idle", pCmd->cmd);

			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
					"idle", -1, e_errState);
			goto exitProc;
		}

		nparwaited = 1 + (pCmd->async? 1: 0);
		if (pCmd->npar == -1) {
			/* Number of parameters not specified */
			if (strcmp(tok[ntok-1], "\\")==0)
				p_stato->currCmd = pCmd;

			/* The task of handling (ALL) parameters is up to the
			* callback: execute the function */
			log_info("command to process: '%s' with %d parameters, used %d",
					cmd, ntok, ntok-nparwaited);
			ret = pCmd->func(p_stato, pCmd, buffer,
				(const char **) tok, ntok, 0);
			found = TRUE;
			break;
		}
		else if (ntok != pCmd->npar + nparwaited) {
			log_warning("cmd=\"%s\": invalid number of parameters", pCmd->cmd);

			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
					"invalid number of parameters", -1, e_errSyntax);
			goto exitProc;
		}

		/* Number of parameters specified */
		for  (x=0; x < ntok-nparwaited; x++)
			tok2func[x] = tok[x+nparwaited];

		cnt = -1;
		if (pCmd->async) {
			/* Further manipulation: extract the counter */
			if (tok[1][0] != '#') {
				log_warning("cmd=\"%s\": missing \"#\" in counter", pCmd->cmd);

				ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
						"missing # in counter", -1, e_errSyntax);
				goto exitProc;
			}
			cnt=atoi(&tok[1][1]);
			if (cnt <= 0) {
				log_warning("cmd=\"%s\": invalid \"#\" counter", pCmd->cmd);

				ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
						"invalid counter", -1, e_errSyntax);
				goto exitProc;
			}
		}

		/* run the function */
		log_info("processed command '%s' with %d parameters, used %d",
				cmd, ntok, ntok-nparwaited);
		ret = pCmd->func(p_stato, pCmd, buffer,
				(const char **) tok2func, ntok-nparwaited, cnt);

		found = TRUE;
		break;
	}
	if (!found) {
		log_warning("cmd=\"%s\": not found", cmd);

		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
				"command not found", -1, e_errSyntax);
		goto exitProc;
	}

	/****** Exit procedures ***************/
exitProc:
	freeTok(tok, ntok);		/* free token memory */
	log_info("Exiting from command");

	return ret;				/* return result */
}

/******************************************************************************/

/**server
* Compose a banner for a new connection.
*/
ssize_t makeBanner(char *buffer, 		/* Answer Buffer */
				   int32_t fd 				/* connection fd */)
{
	char tmps[MAXSTR];
	enum e_state s = get_state();

	sprintf(tmps, "%s fd=%d rel. %s;%s;%s", state_str(s), fd, PGM_VERS, __DATE__, __TIME__);
	return makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, tmps, -1, 0);
}

/******************************************************************************/

/**
* Compose a state transition
*/
ssize_t stateVar(char *buffer, 		/* Answer Buffer */
				   enum e_state old, 	/* previous State */
				   enum e_state new 	/* actual State */)
{
	return sprintf(buffer, "!CS %s %s", state_str(old), state_str(new));
}

/**
 * Compose a warning message
 */
ssize_t warningMsg(int32_t level,				/* Warning level */
				   int32_t overwrite,			/* flag boolean "overwrite" */
				   enum e_errCod errcode,	/* Error Code unique */
				   char *buffer, 			/* Answer Buffer */
				   const char *format,		/* Stringa di formato */
				   ...)
{

	va_list args;
	ssize_t ret;

	va_start(args, format);
	if (format != NULL) {
		char tmps[1024];
		vsnprintf(tmps, sizeof(tmps), format, args);
		ret = sprintf(buffer, "!W%d%c %d %s", level%10, (overwrite)? '+':' ',
														(int32_t)errcode, tmps);
	}
	else
		ret = sprintf(buffer, "!W%d%c %d", level%10, (overwrite)? '+': ' ',
				errcode);
	va_end(args);

	return ret;
}

/******************************************************************************/

/**
* Formats a response of the requested type.
* Returns the length of the response string.
*/
size_t makeAnswer(char *buffer, 			/** Buffer per la risposta */
				  enum e_modeType modeType, /** Modo /syn/asyn) */
				  enum e_cmdType cmdType,	/** Tipo (ok/ko...) */
				  const char *cmd,			/** Comando a cui si risponde */
				  const char *msg,			/** Messaggio */
				  int32_t cnt,					/** Contatore cmd asincrono */
				  enum e_errCod err			/** Codice di errore */)
{
	char header[MAXPRO_LCMD], tail[MAXPRO_LCMD], scnt[MAXSTR];

	/* The tail can always be present: it is a comment */
	tail[0] = '\0';
	if (msg != NULL)
		strcpy(tail, msg);

	/* The type of commands does not depend on the mode; if anything, the first character will be
	* changed.
	*/
	switch (cmdType) {
		case e_cmdOk:
			strcpy(header, ANSOK);
			break;

		case e_cmdKo:
            if (msg != NULL)
                strcpy(tail, msg);
            sprintf(header, "%s %d", ANSKO, (int32_t)err);
 			break;

		case e_cmdCmd:
			if(cmd != 0)
				sprintf(header, "%c%s", PREFIXCMDSYNC, cmd);
			else
				sprintf(header, "%c", PREFIXCMDSYNC);
			break;
	}

	/* The mode conditions the first character and the indication
	* of the counter; in synchronous, the latter is not present.
	*/
	switch (modeType) {
		case e_cmdAsyncAck:
			sprintf(scnt, " #%d", cnt);
			break;

		case e_cmdAsync:
			sprintf(scnt, " #%d", cnt);
			header[0] = PREFIXCMDASYNC;
			break;

		case e_msgAsync:
			if (cnt >= 0)
				sprintf(scnt, " #%d", cnt);
			else
				sprintf(scnt, " ##");
			header[0] = PREFIXMSGASYNC;
			break;

		default:
			scnt[0] = '\0';
			break;
	}

	/* Buffer overflow is something that should never
	* happen at runtime.
	*/
	assert(strlen(header) + strlen(scnt) + 1 + strlen(tail) < MAXPRO_LCMD);

	sprintf(buffer, "%s%s %s", header, scnt, tail);
	return strlen(buffer);
}

/******************************************************************************/
/* Server Commands															  */
/******************************************************************************/

/**
* Prints the current state.
* Returns the size of the response.
*/
static ssize_t fCmd_nop(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	char tmps[MAXSTR];
	ssize_t ret;
	enum e_state s;
	t_sharedVar *p;

	assert(ntok == 0);

	(void)p_stato;
	(void)pCmd;
	(void)tok;
	(void)ntok;
	(void)cnt;

	p = acquireSharedVar();
	s = p->state;
	releaseSharedVar(&p);

	sprintf(tmps, "%s", state_str(s));
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, tmps, -1, 0);
	return ret;
}

/**
* Exits the current session
* Returns a code that terminates the process.
* N.B: the response is still generated in the buffer. The caller can
* know the length with the strlen.
*/
static ssize_t fCmd_quit(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok,	int32_t cnt)
{
	assert(ntok == 0);

	/* all current client activity needs to be stopped; this is done by the caller */
	(void)p_stato;
	(void)pCmd;
	(void)tok;
	(void)ntok;
	(void)cnt;

	/* This is not elegant: the response is created but the size is not returned */
	(void)makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Quitting", -1, 0);
	return NEED_TO_CLOSE;
}



/**
* Show server information.
* Returns the response size.
*
*/
static ssize_t fCmd_info(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						 const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	int32_t j, iter;
	t_stato *p;

	assert(ntok == 0);

	/* Not used */
	(void)tok;
	(void)ntok;
	(void)cnt;
	(void)pCmd;

	j = iter = 0;
	while ((p = stato_iter(&iter)) != NULL) {
		char task[MAXSTR];

		/* Collect task data avoiding race conditions */
		pthread_mutex_lock(&(p->t.mutex));
		if (p->t.valid)
			sprintf(task, "[%d]\"%s\"", p->t.cnt, p->t.pCmd->cmd);
		else
			sprintf(task, "[-](none)");
		pthread_mutex_unlock(&(p->t.mutex));

		/* Channel status */
		if (!btst(p->options, O_INTERNAL)) {
			int32_t delta = (int32_t)(mtimes()- p->last_t);
			size_t nb;
			nb = sprintf(buffer, "+ %c%d fd=%d tout=%d.%03d cmd=%s curr=%s",
						(p == p_stato)? '*': ' ',
						j,
						p->fd,
						delta/1000, delta%1000,
						task,
						p->currCmd? p->currCmd->cmd: "(none)");
			if (write2client(p_stato->fd, buffer, nb) < 0)
				return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
						"Internal error", -1, e_errInt);
		}

		j++;
	}

	/* Command end */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Done", -1, 0);

	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Writes the hardware reset command to the PGA. Requires two parameters: address and data.
* Returns the size of the response.
*/
static ssize_t fCmd_hwreset(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int ntok, int cnt)
{
	ssize_t ret = 0;
	assert(ntok == 0);

	(void) tok;
	(void) p_stato;
	(void) pCmd;
	(void) ntok;
	(void) cnt;

	if (reset_hardware() != 0)
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL, "reset failed", -1, e_errFPGA);
	if (init_hardware() != 0)
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL, "init failed", -1, e_errFPGA);

	return ret;
}


/**
* Writes a data to the PGA. Requires two parameters: address and data.
* Returns the size of the response.
*/
static ssize_t fCmd_poke(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	uint32_t addr;
	uint32_t data;

	assert(ntok == 2);

	(void) p_stato;
	(void) pCmd;
	(void) ntok;
	(void) cnt;

	sscanf(tok[0], "%X", &addr);
	if (addr >= 0x80) {
		log_warning("poke: bad address (%08X)", addr);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"bad address", -1, e_errParam);
	}

	sscanf(tok[1], "%X", &data);
	data &= 0x0000FFFF;

	/* write with the wrapper fpga.cpp */
	if (fpga_poke((fpga_addr_t)addr, data) != 0) {
		log_error("poke %08X %08X failed", addr, data);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"poke failed", -1, e_errFPGA);
	}
	else
		ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, NULL, -1, 0);
	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Reads data from PGA. Requires one parameter: address.
* Returns the size of the response.
*/
static ssize_t fCmd_peek(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	char ans[MAXSTR];
	uint32_t addr = 0;

	assert(ntok == 1);

	(void)p_stato;
	(void)pCmd;
	(void) ntok;
	(void) cnt;

	/* If the parameter is "*", print all registers */
	if (strcmp(tok[0], "*") == 0) {
		int32_t state = 0;
		const char *p;
		uint32_t val;

		while ((p = fpga_getnextreg(&state, &addr, &val)) != NULL) {
			sprintf(ans, "+ %08X %-12.12s = %08X", 0x0000FFFF & addr, p, val);
			if (write2client(p_stato->fd, ans, strlen(ans)) < 0) {
				log_error("peek: cant't write to client");
				return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"Write error", -1, e_errInt);
			}
		}
		ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "done", -1, 0);
	}
	else {
		sscanf(tok[0], "%X", &addr);
		if (addr >= 0x80) {
			log_warning("peek: bad address (%08X)", addr);
			return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"bad address", -1, e_errParam);
		}

		/* read with the wrapper fpga.cpp */
		fpga_data_t data;
		if ((data = fpga_peek((fpga_addr_t)addr)) < 0) {
			log_error("peek %08X: failed", addr);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"peek failed", -1, e_errFPGA);
		}
		else  {
			sprintf(ans, "%08X", (unsigned int)data);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, ans, -1, 0);
		}
	}
	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Reads a buffer from the PGA. Requires two parameters: base address and
* #of words.
* Returns the size of the response.
*/
static ssize_t fCmd_read(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
//	uint32_t buf[FPGA_MEMORY_SIZE];
	uint32_t *buf;
	size_t base, count;

	assert(ntok == 2);

	(void)p_stato;
	(void)pCmd;
	(void) ntok;
	(void) cnt;

	base = 0;
	if (sscanf(tok[0], "%zX", &base) != 1) {
		log_warning("read: bad base address format (%s)", tok[0]);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"bad address format", -1, e_errParam);
	}

	if (base >= SRAM_IP_BASEADDR) {
		log_warning("read: base address too high (%08X)", base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"base too high", -1, e_errParam);
	}

	count = 0;
	if (sscanf(tok[1], "%zX", &count) != 1) {
		log_warning("read: bad count format (%s)", tok[1]);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"bad count format", -1, e_errParam);
	}

	if ((count*sizeof(uint32_t)) + base >= SRAM_IP_BASEADDR) {
		log_warning("read: end address too high (%08X)", (count*sizeof(uint32_t)) + base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"end address too high", -1, e_errParam);
	}

	buf = (uint32_t *) malloc(sizeof(uint32_t) * (count));

	/* read with the wrapper fpga.cpp */
	if (fpga_read(base/4, buf, count) < 0) {
		log_error("read: read(%08X,%08X) failed", base, (count+base)*4);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"read failed", -1, e_errFPGA);
	}
	else  {
		int j;
		char ans[MAXSTR];
		sprintf(ans, "%zd", count);
		int cont = atoi(ans);
		log_debug("read: read(%08X,%08X)", base, cont);
		strcpy(ans ,"");
		for (j=0; j<cont; j+=8) {
			size_t k;

			sprintf(ans, "+ %08zX", j*4+(base));

			for (k=0; k < 8; k++) {
				char tmps[10];
				sprintf(tmps, " %08X", buf[j+k]);
				strcat(ans, tmps);
			}
			if (write2client(p_stato->fd, ans, strlen(ans)) < 0) {
				log_error("read: cant't write to client");
				return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"Write error", -1, e_errInt);
			}
		}
		ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "EOF", -1, 0);
	}
	free(buf);
	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Fills a memory area of ​​the PGA. Requires two parameters:
* base address and pattern to write.
* The address is of the form:
* base[:end] (e.g. 10000:10FFF).
* The pattern is of the form:
* word[*count] (e.g. DEADBEEF*100)
* All numbers are in hexadecimal. If both
* base:end and count are specified, end is not exceeded.
* Returns the size of the response.
*/
static ssize_t fCmd_fill(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
//	uint32_t buf[FPGA_MEMORY_SIZE];
	uint *buf;
	uint32_t j, base, end, pattern, count;
	int32_t n;

	assert(ntok == 2);

	(void)p_stato;
	(void)pCmd;
	(void)ntok;
	(void)cnt;

	base = end = 0;
	n = sscanf(tok[0], "%x:%x", &base, &end);
	if (n != 1 && n != 2) {
		log_warning("fill: bad address format (%s)", tok[0]);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"bad address format", -1, e_errParam);
	}
	if (n == 2 && end < base) {
		log_warning("fill: end < base (%08X < 08X)", end, base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"end < base", -1, e_errParam);
	}
	if (base >= SRAM_IP_BASEADDR) {
		log_warning("fill: base too high (%08X)", base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"base too high", -1, e_errParam);
	}
	if (end == 0)
		end = base;

	pattern = count = 0;
	sscanf(tok[1], "%x*%x", &pattern, &count);
	if (n != 1 && n != 2) {
		log_warning("fill: bad pattern format (%s)", tok[1]);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"bad pattern format", -1, e_errParam);
	}
	if (count == 0 || (end > base && count > end - base + 1))
		count = end - base + 1;


	if (((count*sizeof(uint32_t)) + base) > SRAM_IP_BASEADDR) {
		log_warning("fill: end address toot high (%08X)", (count*4)+base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"end address too high", -1, e_errParam);
	}

	buf = (uint32_t *) malloc(sizeof(uint32_t) * count);

	for (j=0; j<count; j++)
		buf[j] = pattern;

	/* write with the wrapper fpga.cpp */
	if (fpga_write(base/4, buf, count) < 0) {
		log_error("fill: write(%08X,%08X) failed", base, (count*4)+base);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"write failed", -1, e_errFPGA);
	}
	else {
		char stmp[MAXSTR];
		sprintf(stmp, "%d 32bit patterns written", count);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, stmp, -1, 0);
	}
	free(buf);
	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Writes a buffer to the PGA. Requires an address and
* a series of hexadecimal values.
* If the series cannot be completed in a single message
* because there are too many values, then the first message
* ends with '\' and subsequent messages start with '+'.
* If the number of bytes to write is not a multiple
* of 512, the number is rounded up to the next multiple of 512
* and the buffer is padded with 0.
* Returns the size of the response.
*/
static ssize_t fCmd_write(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	uint32_t val, base;
	int32_t n;

	/* This command has a variable number of parameters. */
	if (ntok <= 0) {
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
									"not enough parameters", -1, e_errSyntax);
		goto end_reset;
	}

	(void)pCmd;
	(void)cnt;

	if (strcmp(tok[0], "write") == 0) {
		/* first messagge */
		int32_t j;

		n = sscanf(tok[1], "%x", &base);
		if (n != 1) {
			log_warning("write: bad address format (%s)", tok[0]);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"bad address format", -1, e_errParam);
			goto end_reset;
		}
		if (base >= (SRAM_TR_BASEADDR - SRAM_IP_BASEADDR)) {
			log_warning("write: base address too high (%08X)", base);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"base too high", -1, e_errParam);
			goto end_reset;
		}

		p_stato->base = base;
		p_stato->count = 0;

		for (j = 2; j < ntok; j++) {
			if (j == ntok -1 && equal(tok[j], "\\"))
				break;
			n = sscanf(tok[j], "%x", &val);
			if (n != 1) {
				log_warning("write: bad data format (tok[%d]=%s)", j, tok[j]);
				ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"bad data format", -1, e_errParam);
				goto end_reset;
			}

			p_stato->data[p_stato->count++] = 0xFFFFFFFF & val;
		}
	}
	else if (strcmp(tok[0], "+") == 0) {
		/* Next Messagges */
		int32_t j;

		for (j = 1; j < ntok; j++) {
			if (j == ntok -1 && equal(tok[j], "\\"))
				break;
			n = sscanf(tok[j], "%x", &val);
			if (n != 1) {
				log_warning("write: bad data format (tok[%d]=%s)", j, tok[j]);
				ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"bad data format", -1, e_errParam);
				goto end_reset;
			}

			p_stato->data[p_stato->count++] = 0xFFFFFFFF & val;
		}
	}
	else {
		/* error */
		log_warning("write: invalid message (%s)", tok[0]);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"invalid message", -1, e_errSyntax);
		goto end_reset;
	}

	if (equal(tok[ntok-1], "\\")) {
		/* Continue without passing from error */
		ret = 0;
		return ret;
	}
	else {
		/* Last messagge: write */
		uint32_t buf[DATA_BUFFER_SIZE];
		uint32_t j, count;

		/* collecting info */
		base = p_stato->base;
		count = p_stato->count;

		if (base + (count*4) >= (SRAM_TR_BASEADDR - SRAM_IP_BASEADDR)) {
			log_warning("write: end address too high (%08X)", base+count);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"end address too high", -1, e_errParam);
			goto end_reset;
		}

		for (j=0; j<count; j++)
			buf[j] = 0xFFFFFFFF & p_stato->data[j];

		/* write with the wrapper fpga.cpp */
		if (fpga_write(base/4, buf, count) < 0) {
			log_error("write: write(%08X,%08X) failed", base, base+count);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"write failed", -1, e_errFPGA);
		}
		else {
			char stmp[MAXSTR];
			sprintf(stmp, "%d word written", count);
			ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, stmp, -1, 0);
		}
	}

end_reset:
	p_stato->currCmd = NULL;
	p_stato->base = 0;
	p_stato->count = 0;
	return ret;
}

/*----------------------------------------------------------------------------*/

/**
* Idles the server
* Returns the size of the response.
*/
static ssize_t fCmd_idle(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;

	assert(ntok == 0);

	(void) p_stato;
	(void) pCmd;
	(void) ntok;
	(void) tok;
	(void) cnt;

	/* always work! */
	set_state(e_idle);

	/* Ack of the command */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "going idle", -1, -1);
	return ret;
}

/**
* Abort command simulation function.
* Returns the size of the response.
*/
static ssize_t fCmd_abort(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;

	assert(ntok == 0);

	(void) p_stato;
	(void) pCmd;
	(void) ntok;
	(void) tok;
	(void) cnt;

	explain_error("Status just before abort: ");
	//abort init

	switch (get_state()) {
	case e_ready:
		log_info("ABORT in ready");	// XXX
		break;
	default:
		// In all other cases it behaves like a no-op */
		break;
	}

	/* Ack of the command */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Abort activated", -1, -1);
	return ret;
}
