/**
 * command.h - Header per elaborazione comandi del server.
 *
 * (C) 2008 GGH srl per Igea SpA
 *
 */
#ifndef COMMAND_H_
#define COMMAND_H_

#define MAXRUNNINGPROC	20		/* Numero massimo di processi contemporanei */
#define NEED_TO_CLOSE	(-99)	/* Codice ritornato dalle funzioni comando
									per indicare che il canale col cliente
									deve essere	chiuso */

/* Modi del protocollo */
enum e_modeType {
		e_cmdSync,			/* Comando sincrono (risposta immediata) */
		e_cmdAsync,			/* Comando ssincrono (risposta differita) */
		e_cmdAsyncAck,		/* Comando asincrono con acknowledge immediato */
		e_msgAsync			/* Messaggio asincrono */
};

/* Tipi di comandi */
enum e_cmdType {
		e_cmdOk,			/* Risposta positiva */
		e_cmdKo,			/* Risposta negativa */
		e_cmdCmd			/* Comando */
};

/* Tabella principale dei comandi del server */
struct S_CMD {
	const char *cmd;		/* Stringa del comando */
	int32_t npar;				/* N. parametri attesi */
	bool async;				/* Mando asincrono? */
	bool idly;				/* Disabilitato in idle */
    ssize_t (*func)(t_stato *p_stato, struct S_CMD *pCmd, char *retBuff,
    		const char **par, int32_t npar,	int32_t errCode);	/* esecuzione */
    void *(*threadStart)(void *param);					/* partenza thread */
};
typedef struct S_CMD t_cmd;

/*********************************************************************/
/* Valori e limiti */

#define GET_BULK_BASE	0x00000000	// Base per il trasferimento misure


/* prototipi */
ssize_t makeBanner(char *, int32_t);
ssize_t stateVar(char *, enum e_state, enum e_state);
ssize_t fpgaMsg(char *buffer);
ssize_t warningMsg(int32_t level, int32_t overwrite, enum e_errCod errcode, char *buffer, const char *format,	...);
ssize_t elaboraCmd(t_stato *, char *, const char *, size_t);
size_t makeAnswer(char *buffer, enum e_modeType modeType,
		enum e_cmdType cmdType, const char *cmd, const char *msg, int32_t cnt,
		enum e_errCod err);
#endif /*COMMAND_H_*/
