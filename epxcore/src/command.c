/**
 * command.c - Elaborazione comandi del server.
 *
 * (C) 2008 GGH srl per Igea SpA
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

// Commentare per avere i valori in livelli logici
#define REALVALUES

/* ========================================================================= */
/* Prototipi interni */

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



/* tabella principale dei comandi */
static t_cmd S_command[] = {
	/* cmd	  npar async   idly	Cmd_func	Start_thread */
	{ "nop",	0, FALSE,	0,	fCmd_nop,	NULL },
	{ "quit",	0, FALSE,	0,	fCmd_quit,	NULL },
	{ "info",	0, FALSE,	0,	fCmd_info,	NULL },

	// Comandi ad uso debug
	{ "poke",	2, FALSE,	1,	fCmd_poke,	NULL },
	{ "po",		2, FALSE,	1,	fCmd_poke,	NULL },		// Sinonimo di "poke"
	{ "peek",	1, FALSE,	0,	fCmd_peek,	NULL },
	{ "pe",		1, FALSE,	0,	fCmd_peek,	NULL },		// Sinonimo di "peek"
	{ "read",	2, FALSE,	1,	fCmd_read,	NULL },
	{ "write",	-1,FALSE,	1,	fCmd_write,	NULL },		// N. variabile di pars
	{ "fill",	2, FALSE,	1,	fCmd_fill,	NULL },
	
	{ "idle",	0, FALSE,	1,	fCmd_idle,	NULL },
	{ "i",		0, FALSE,	1,	fCmd_idle,	NULL },		// sinonimo di idle
	{ "abort",	0, FALSE,	1,	fCmd_abort,	NULL },
	{ "a",		0, FALSE,	1,	fCmd_abort,	NULL },		// sinonimo di "abort"

	{ "hwreset",	0, FALSE,	0,	fCmd_hwreset,	NULL },

	{ NULL,		0, FALSE,	0,	NULL,		NULL }		/* tappo */
};

#ifdef SIMULAZIONE
static int32_t S_HVa;
static int32_t S_LVa;
#endif

/******************************************************************************/

/**
 * Elabora un comando; gestisce il parsing della sintassi ed esegue comandi
 * immediati o thread secondo la necessita`.
 * Torna 0 per successo o -1 per fallimento; diagnostica via log_.
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

	/* Separa i parametri del comando */
	log_info("Cmd: %s",cmd);
	ntok = argTok(cmd, tok, MAXPRO_TOK);
	if (ntok < 0) {
		log_warning("errore cmd=%s argTok()=%d", cmd, ntok);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL, "tokenizer failure",
				-1, e_errSyntax);
		goto exitProc;
	}

	ret = 0;

	/* Se non ci sono parametri esci silenziosamente */
	if (ntok < 1)
		goto exitProc;

	cmd = tok[0];

	found=FALSE;
	for (pCmd = S_command; pCmd->cmd != NULL; pCmd++) {
		if (strcmp(cmd, pCmd->cmd)!=0)
			continue;

		/* I comandi marcati idly sono bloccati in stato e_idle */
		if (pCmd->idly && get_state() == e_idle) {
			log_warning("cmd=\"%s\" in idle", pCmd->cmd);

			ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
					"idle", -1, e_errState);
			goto exitProc;
		}

		nparwaited = 1 + (pCmd->async? 1: 0);
		if (pCmd->npar == -1) {
			/* Numero di parametri non specificato */
			if (strcmp(tok[ntok-1], "\\")==0)
				p_stato->currCmd = pCmd;

			/* Il compito di gestire (TUTTI) i parametri spetta alla
			 * callback: esegui la funzione */
			log_info("comando da processare: '%s' con %d parametri, usati %d",
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

		/* Numero di parametri specificato */
		for  (x=0; x < ntok-nparwaited; x++)
			tok2func[x] = tok[x+nparwaited];

		cnt = -1;
		if (pCmd->async) {
			/* Ulteriore manipolazione: estrai il contatore */
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

		/* esegui la funzione */
		log_info("processato comando '%s' con %d parametri, usati %d",
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

	/****** Procedure di uscita ***************/
exitProc:
	freeTok(tok, ntok);		/* libera memoria token */
	log_info("Exiting from command");

	return ret;				/* torna esito */
}

/******************************************************************************/

/**server
 * Componi un banner per una nuova connessione.
 */
ssize_t makeBanner(char *buffer, 		/* Buffer per la risposta */
				   int32_t fd 				/* fd della connesione */)
{
	char tmps[MAXSTR];
	enum e_state s = get_state();

	sprintf(tmps, "%s fd=%d rel. %s;%s;%s", state_str(s), fd, PGM_VERS, __DATE__, __TIME__);
	return makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, tmps, -1, 0);
}

/******************************************************************************/

/**
 * Componi una transizione di stato
 */
ssize_t stateVar(char *buffer, 		/* Buffer per la risposta */
				   enum e_state old, 	/* Stato precedente */
				   enum e_state new 	/* Stato attuale */)
{
	return sprintf(buffer, "!CS %s %s", state_str(old), state_str(new));
}

/**
 * Componi un warning message
 */
ssize_t warningMsg(int32_t level,				/* Livello del warning */
				   int32_t overwrite,			/* flag booleano "sovrascrivi" */
				   enum e_errCod errcode,	/* Codice univoco di errore */
				   char *buffer, 			/* Buffer per la risposta */
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
 * Formatta una risposta del tipo richiesto.
 * Torna la lunghezza della stringa di risposta.
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

	/* La coda puo` sempre esere presente: e` un commento */
	tail[0] = '\0';
	if (msg != NULL)
		strcpy(tail, msg);

	/* Il tipo di comandi non dipende dal modo; semmai verra`
	 * cambiato il primo carattere.
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

	/* Il modo condiziona il primo carattere e l'indicazione
	 * del contatore; nei sincroni, quest'ultimo non c'e`.
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

	/* L'uscita per superamento del buffer e` un fatto che non dovrebbe
	 * mai verificarsi a runtime.
	 */
	assert(strlen(header) + strlen(scnt) + 1 + strlen(tail) < MAXPRO_LCMD);

	sprintf(buffer, "%s%s %s", header, scnt, tail);
	return strlen(buffer);
}

/******************************************************************************/
/* Comandi del server														  */
/******************************************************************************/

/**
 * Stampa lo stato attuale.
 * Torna le dimensioni della risposta.
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
 * Esce dalla sessione corrente
 * Torna un codice che termina il processo.
 * N.B: la risposta viene comunque generata nel buffer. Il chiamante puo`
 * 		conoscere la lunghezza con la strlen.
 */
static ssize_t fCmd_quit(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok,	int32_t cnt)
{
	assert(ntok == 0);

	/* occorre fermare tutte le attivita` del cliente corrente; questo
	 * viene fatto dal chiamante */
	(void)p_stato;
	(void)pCmd;
	(void)tok;
	(void)ntok;
	(void)cnt;

	/* Non e` elegante: la risposta viene creata ma non viene ritornata la
	 * dimensione */
	(void)makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Quitting", -1, 0);
	return NEED_TO_CLOSE;
}



/**
 * Mostra informazioni sul server..
 * Torna le dimensioni della risposta.
 *
 */
static ssize_t fCmd_info(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						 const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	int32_t j, iter;
	t_stato *p;

	assert(ntok == 0);

	/* Non sono utilizzati */
	(void)tok;
	(void)ntok;
	(void)cnt;
	(void)pCmd;

	j = iter = 0;
	while ((p = stato_iter(&iter)) != NULL) {
		char task[MAXSTR];

		/* Raccogli i dati sui task evitando le race conditions */
		pthread_mutex_lock(&(p->t.mutex));
		if (p->t.valid)
			sprintf(task, "[%d]\"%s\"", p->t.cnt, p->t.pCmd->cmd);
		else
			sprintf(task, "[-](none)");
		pthread_mutex_unlock(&(p->t.mutex));

		/* Stato del canale */
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

	/* Conclusione del comando */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Done", -1, 0);

	return ret;
}

/*----------------------------------------------------------------------------*/

/**
 * Scrive nella PGA il comando di hardware reset. Richiede due parametri: indirizzo e dato.
 * Torna le dimensioni della risposta.
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
 * Scrive un dato nella PGA. Richiede due parametri: indirizzo e dato.
 * Torna le dimensioni della risposta.
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

	/* Scrivi con il wrapper fpga.cpp */
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
 * Legge un dato dalla PGA. Richiede un parametro: indirizzo.
 * Torna le dimensioni della risposta.
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

	/* Se il parametro e` "*", stampa tutti i registri */
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

		/* Leggi con il wrapper fpga.cpp */
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
 * Legge un buffer dalla PGA. Richiede due parametri: indirizzo base e
 * n.di parole.
 * Torna le dimensioni della risposta.
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

	/* Leggi con il wrapper fpga.cpp */
	if (fpga_read(base/4, buf, count) < 0) {
		log_error("read: read(%08X,%08X) failed", base, (count+base)*4);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
												"read failed", -1, e_errFPGA);
	}
	else  {
		int j;
		char ans[MAXSTR];
		sprintf(ans, "%ld", count);
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
 * Riempie un'area di memoria della PGA. Richiede due parametri:
 * indirizzo base e pattern da scrivere.
 * L'indirizzo e` della forma:
 * base[:end] (es: 10000:10FFF).
 * Il pattern e` della forma:
 * word[*count] (es. DEADBEEF*100)
 * Tutti i numeri sono in esadecimale. Nel caso si specifichino sia
 * base:end che count, non viene comunque superato end.
 * Torna le dimensioni della risposta.
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

//	if ((count % 0x200) != 0) {
//		log_warning("fill: invalid block size (%08X)", count);
//		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
//										"invalid block size", -1, e_errParam);
//	}
	if (((count*sizeof(uint32_t)) + base) > SRAM_IP_BASEADDR) {
		log_warning("fill: end address toot high (%08X)", (count*4)+base);
		return makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
										"end address too high", -1, e_errParam);
	}

	buf = (uint32_t *) malloc(sizeof(uint32_t) * count);

	for (j=0; j<count; j++)
		buf[j] = pattern;

	/* Scrivi con il wrapper fpga.cpp */
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
 * Scrive un buffer nella PGA. Richiede un indirizzo e
 * una serie di valori esadecimali.
 * Se la serie non puo` concludersi in un singolo mesaggio
 * perche' ci sono troppi valori, allora il primo mesaggio
 * termina con '\' e i successivi iniziano con '+'.
 * Se il numero di byte da scrivere non e` un multiplo
 * di 512, il numero viene arotondato al multiplo di 512
 * successivo e il buffer viene paddato con 0.
 * Torna le dimensioni della risposta.
 */
static ssize_t fCmd_write(t_stato *p_stato, t_cmd *pCmd, char *buffer,
						const char **tok, int32_t ntok, int32_t cnt)
{
	ssize_t ret;
	uint32_t val, base;
	int32_t n;

	/* Questo comando ha un numero variabile di parametri */
	if (ntok <= 0) {
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
									"not enough parameters", -1, e_errSyntax);
		goto end_reset;
	}

	(void)pCmd;
	(void)cnt;

	if (strcmp(tok[0], "write") == 0) {
		/* primo messaggio */
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
		/* Messaggi successivi */
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
		/* errore */
		log_warning("write: invalid message (%s)", tok[0]);
		ret = makeAnswer(buffer, e_cmdSync, e_cmdKo, NULL,
											"invalid message", -1, e_errSyntax);
		goto end_reset;
	}

	if (equal(tok[ntok-1], "\\")) {
		/* Prosegui senza passare da errore */
		ret = 0;
		return ret;
	}
	else {
		/* Ultimo messaggio: scrivi */
		uint32_t buf[DATA_BUFFER_SIZE];
		uint32_t j, count;

		/* Recupera le info */
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

		/* Comunque vada, resetta lo stato */

		/* Scrivi con il wrapper fpga.cpp */
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
 * Mette il server in idle
 * Torna le dimensioni della risposta.
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

	/* funziona sempre! */
	set_state(e_idle);

	/* Ack del comando */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "going idle", -1, -1);
	return ret;
}

/**
 * Funzione di simulazione del comando abort.
 * Torna le dimensioni della risposta.
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
	//inizio l'abort

	switch (get_state()) {
	case e_ready:
		log_info("ABORT in ready");	// XXX
		break;
	default:
		// In tutti gli altri casi si comporta come una no-op */
		break;
	}

	/* Ack del comando */
	ret = makeAnswer(buffer, e_cmdSync, e_cmdOk, NULL, "Abort activated", -1, -1);
	return ret;
}
