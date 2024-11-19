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

#include "errors.h"
#include "epxcore.h"
#include "main.h"
#include "command.h"
#include "util.h"
#include "sharedVar.h"

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

/* tabella principale dei comandi */
static t_cmd S_command[] = {
	/* cmd	  npar async   idly	Cmd_func	Start_thread */
	{ "nop",	0, FALSE,	0,	fCmd_nop,	NULL },
	{ "quit",	0, FALSE,	0,	fCmd_quit,	NULL },
	{ "info",	0, FALSE,	0,	fCmd_info,	NULL },

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
