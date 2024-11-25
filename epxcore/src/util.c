/**
 * util.c - Funzioni di utilita`
 *
 * (C) 2024 bertac64
 *
 */

/********************************************************************/

/* POSIX.1 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>		/* Integra i tipi elementari di sys/types.h */
#include <assert.h>
#include <pthread.h>

/* SOCKET */
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

/* Librerie locali */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "fpga.h"
#include "errors.h"
#include "epxcore.h"
#include "command.h"
#include "util.h"

const unsigned int BIND_RETRY_COUNT = 20;

/******************************************************************************/
/* Modulo di gestione dello stato clienti connessi							  */
/******************************************************************************/

static t_stato S_stato[MAXPRO_CLI];	/* Tabella con lo stato dei client */

/**
 * Inizializza la tabella dei clienti.
 */
void stato_init(void)
{
	int j;

	/* Inizializza i descrittori di sessione */
	for (j=0; j<MAXPRO_CLI; j++) {
		memset(&(S_stato[j]), 0, sizeof(t_stato));
		S_stato[j].fd = -1;
		S_stato[j].t.valid = FALSE;	/* Qui non serve il mutex */
		pthread_mutex_init(&(S_stato[j].t.mutex), NULL);
	}
}

/**
 * Aggiunge (crea) un cliente nella tabella dei clienti gestiti
 * Ritorna l'indice del cliente creato, -1 per errore.
 */
int stato_add(int fd, int options)
{
	int j;

	/* Cerca descrittore di sessione libero */
	for (j=0; j<MAXPRO_CLI; j++) {
		if (S_stato[j].fd == -1) {
			S_stato[j].fd = fd;
			S_stato[j].options = options;
			S_stato[j].last_t = mtimes();
			S_stato[j].nb = 0;

			S_stato[j].t.valid = FALSE;	/* Qui non serve il mutex */
			pthread_mutex_init(&(S_stato[j].t.mutex), NULL);
			break;
		}
	}
	if (j >= MAXPRO_CLI)
		return -1;

	return fd;
}

/**
 * Itera sulla lista dei clienti gestiti.
 * Ritorna un puntatore valido al "prossimo" cliente o NULL a fine lista.
 */
t_stato * stato_iter(int *iter)
{
	for (; (*iter) < MAXPRO_CLI; (*iter)++) {
		if (S_stato[*iter].fd != -1)
			return &(S_stato[(*iter)++]);
	}

	return NULL;
}

/***************************************************************************/

/**
 * DESCRIZIONE
 *	Trova un cliente nella tabella dei clienti collegati. La chiave di ricerca
 *	e` il fd della connessione.
 * VALORI DI RITORNO
 *	Torna un puntatore al descrittore o NULL in caso di errore (not found).
 */
t_stato *stato_getbyfd(int fd)
{
	int j;

	/* Cerca descrittore di sessione libero */
	for (j=0; j<MAXPRO_CLI; j++) {
		if (S_stato[j].fd == fd)
			return &S_stato[j];
	}
	log_warning("stato_getbyfd(): fd <%d> non trovato", fd);
	return NULL;
}

/**
 *		Chiude il canale di comunicazione con il cliente e libera lo slot
 *		corrispondente.
 */
void stato_close(t_stato *p_chn)
{
	/* Azione sui task */
	task_cancel(&(p_chn->t));

	/* Azione sul canale */
	close(p_chn->fd);
	p_chn->fd = -1;
	p_chn->options = 0;
	p_chn->last_t = 0;
	p_chn->nb = 0;
	p_chn->currCmd = NULL;
    p_chn->count = 0;
    p_chn->base = 0;
	p_chn->nsample = 0;
}

/**
 * 	Cancella un task in esecuzione e azzera la struttura corrispondente.
 */
void task_cancel(t_task *t			/** descrittore di task */)
{
	pthread_mutex_lock(&(t->mutex));
	if (t->valid && !pthread_equal(pthread_self(), t->th))
		(void)pthread_cancel(t->th);

	t->th = 0;
	t->pCmd = NULL;
	t->cnt = 0;
	t->valid = FALSE;
	pthread_mutex_unlock(&(t->mutex));
}

/**
 * 	Inizializza la struttura di un task.
 */
void task_create(t_task *t,			/** descrittore di task */
				 int cnt, 			/** Rif. progressivo al comando */
				 t_cmd *pCmd, 		/** Descrittore del comando */
				 t_stato *p_stato	/** Descrittore/stato di canale */)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&(t->mutex));

	// Imposta lo stato del thread a detached, per non dover fare la join
	if (pthread_attr_init(&attr) != 0) {
		log_perror("task_create: can't create attr for thread");
		exit(1);
	}
	if (pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED) != 0) {
		log_perror("task_create: can't set detached state for thread");
		exit(1);
	}

	// Da fare PRIMA che il thread cominci ad usarli

	t->pCmd = pCmd;
	t->cnt = cnt;
	t->valid = TRUE;

	// Crea il thread
	if (pthread_create(&(t->th), &attr, pCmd->threadStart, p_stato) != 0) {
		log_perror("task_create: can't create thread for command");
		exit(1);
	}

	// Rilascia gli attributi creati
	if (pthread_attr_destroy(&attr) != 0) {
		log_perror("task_create: can't destroy attr for thread");
		exit(1);
	}

	pthread_mutex_unlock(&(t->mutex));
}

/******************************************************************************/
/* Modulo gestione canali e protocolli										  */
/******************************************************************************/

/**
 *	DESCRIZIONE
 *		Configura il canale passivo di servizio.
 *	VALORI DI RITORNO
 *		Un socket passivo. Gli errori sono fatali.
 */
int ch_conf(short portnum	/** Numero della well-known port */)
{
	int s, on = 1;
	unsigned int j;
	struct sockaddr_in epc_sin;
	int bind_retval = -1;

	/* apri il socket principale di riallineamento */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		log_fatal("socket: %s", strerror(errno));

	log_debug("creato socket %d", s);

	/* metti a "riusabile" l'indirizzo, per evitare TIME_WAIT;
		N.B.: il fatto che un socket sia riutilizzabile non
		vuol dire che due istanze possano partire sullo stessa
		coppia indirizzo/porta. Se sta girando un altro server,
		la bind() fallira` con EADDRINUSE. */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
		log_fatal("setsockopt(SO_REUSEADDR): %s", strerror(errno));
	log_debug("(socket riutilizzabile)");

	/* fai il bind con il suo "well known port number" */
	bzero((char *)&epc_sin, sizeof(epc_sin));
	epc_sin.sin_family = AF_INET;
	epc_sin.sin_port = htons(portnum);	/* attenzione: htons */

	// ciclo per ritentare la bind se fallisce
	for (j=0; j<BIND_RETRY_COUNT; ++j)
	{
		bind_retval = bind(s, (struct sockaddr *)&epc_sin, sizeof(epc_sin));
		if (errno != EADDRINUSE || j+1==BIND_RETRY_COUNT)
			log_debug("bind: %s ", strerror(errno));
		if (bind_retval != -1){
			break;
		}
		sleep(1);
	}

	if (bind_retval == -1){
		log_debug("Bind non effettuata dopo 20 tentativi");
		return (-1);
	}else
		log_debug("effettuata bind(): porta %d", portnum);

	/* accetta le connessioni da tutti gli host; coda d'attesa: 5  */
	if (listen(s, 5) == -1) {
		log_fatal("listen: %s", strerror(errno));
		exit(1);
	}
	log_debug("effettuata listen()");
	log_debug("canale di servizio configurato");

	/* Ritorna */
	return s;
}

/**
 *	"Cucina" un buffer per trasformarlo in un messaggio valido da spedire.
 *	Ritorna la lunghezza del buffer.
 */
size_t cook(char *buffer, 		/* Buffer uscita */
			 const char *cmd,	/* buffer ingresso */
			 size_t	buflen		/* Lunghezza del suddetto */)
{
	size_t len = buflen;
	if (buflen > 2) {
		/* e` una stringa zero-terminated, quindi la lunghezza non interessa */
		len = snprintf(buffer, MAXPRO_LCMD, "%s\r\n", cmd);

		/* Stampa di debug */
		if (isdebug())
			log_dump((char *) buffer, len);
	}else
		log_error("Wrong buffer length: %d", len);

	/* Ritorna le dimensioni */
	return len;
}

/**
 *	Assembla un messaggio completo in ricezione.
 *	Puo` essere chiamato ciclicamente: se chunk=NULL cerca solo di
 *	ritornare l'eventuale prossimo comando completo presente nel buffer.
 *	Ritorna la lunghezza del buffer; 0 se il buffer non e' ancora pronto,
 *	-1+errno in caso di errore (il canale va chiuso).
 */
ssize_t abcr(t_stato *p,		/* Informazioni di canale */
			 const char *chunk, /* Chunk di dati in arrivo */
			 size_t chlen,		/* Dimensione del chunk */
			 char *outbuf, 		/* Buffer di dati in uscita */
			 size_t oblen		/* Dim. massima del buffer */)
{
	char *ps;
	int cl;

	if (chunk != NULL) {
		if (chlen + p->nb > sizeof(p->b)) {
		 	/* errore piuttosto serio; violazione del protocollo? */
		 	errno = EBADMSG;
		 	return (ssize_t)-1;
		}

		memcpy(&(p->b[p->nb]), chunk, chlen);
		p->nb += chlen;
	}

	/* Sono pur sempre stringhe: zero-termina per sicurezza */
	p->b[p->nb] = '\0';
	if ((ps = strstr(p->b, "\r\n")) == NULL)
		return 0;

	/* Hai un pacchetto: staccalo */
	oblen = ps - p->b;
	memcpy(outbuf, p->b, oblen);
	outbuf[oblen] = 0;

	/* Riallinea il buffer */
	cl = oblen + 2;			/* lunghezza + terminatore CRLF */
	memmove(p->b, &(p->b[cl]), p->nb - cl);
	p->nb -= cl;

	return (ssize_t)oblen;
}

/******************************************************************************/
/* Gestione separazione in token e esecuzione comandi 						  */
/******************************************************************************/

/**
 *	Disalloca la memoria allocata per i token (cfr. argTok).
 */
void freeTok(char **psp,	/* Array di puntatori a token */
			 int32_t nArgs	/* Numero di token */)
{
	int j;

	for(j = 0;  j < nArgs;  j++) {
		if (psp[j] != NULL)
			free(psp[j]);
	}
}

/******************************************************************************/

/**
 *	Trasforma una stringa di comandi in token, riponendoli in un array che
 *	segnala la fine della lista tramite NULL. Per disallocare i token
 *	bisogna utilizzare la funzione freeTok().
 *	Ritorna:
 *		>=0 numero di token ricavati dalla stringa,
 *		-1	se la stringa di comandi non e` valida,
 *		-2	se ci sono troppi argomenti.
 */
int32_t argTok(const char *cmdStr, 	/** Stringa da tokenizzare */
			   char **psp,			/** Array di puntatori a token */
			   int32_t maxArgs		/** Massimo numero di token ammessi */)
{
	char *cmdCpy, *pStart, *p;
	int parNum, qFlg, sFlg, anyChar, retval;

	cmdCpy = strmem(cmdStr);

	parNum = qFlg = sFlg = anyChar = 0;
	pStart = cmdCpy;
	retval = -2;
	for (p=cmdCpy; *p != '\0'; p++) {
		switch(*p) {
		case ' ':
		case '\t':
			if (!qFlg) {
				if (anyChar) {
					*p = '\0';
					if (parNum+1 >= maxArgs-1)
						goto tokError;
					psp[parNum++] = strmem(pStart);
				}
				pStart = p+1;
			}
			anyChar=0;
			break;

		case '\'':
			if (sFlg) {
				anyChar=1;
				sFlg = 0;
			}
			else {
				if (qFlg) {
					*p = '\0';
					if (parNum+1 >= maxArgs-1)
						goto tokError;
					psp[parNum++] = strmem(pStart);
					qFlg = 0;
					pStart = p+1;
					anyChar=0;
				}
				else {
					if (!anyChar)
						pStart = p+1;
					else {
						memmove(p, p+1, strlen(p));
						p--;
					}
					qFlg = 1;
					anyChar=1;
				}
			}
			break;

		case '\\':
			if (sFlg) {
				sFlg = 0;
				anyChar=1;
			}
			else {
				memmove(p, p+1, strlen(p));
				p--;
				sFlg = 1;
			}
			break;

		default:
			if (sFlg)
				sFlg = 0;
			anyChar=1;
			break;
		}
	}
	if (anyChar) {
		psp[parNum++] = strmem(pStart);
		if (parNum+1 >= maxArgs-1)
			goto tokError;
	}
	psp[parNum] = NULL;

	retval = -1;
	if (qFlg || sFlg) {
tokError:
		free(cmdCpy);
		freeTok(psp, parNum);
		return retval;
	}

	free(cmdCpy);
	return parNum;
}

/*****************************************************************************/

/**
 *	DESCRIZIONE
 *		Esegue un coprocesso come popen(), ma torna un file descriptor
 *		bidirezionale.
 *	VALORI DI RITORNO
 *		File descriptor; -1 per errore (cfr. errno).
 *		Via *ppid, torna anche il pid del processo.
 */


int bipopen(const char *command,	/** Comando */
			pid_t *ppid	/** ptr a pid da tornare o NULL */)
{
	int fds[2];
	pid_t pid;
	const size_t MAXARGS=100;		/* N. massimo di argomenti ammessi */

	/* Crea una nuova full duplex socket pipe */
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) < 0)
		return -1;

	log_debug("FORKING for %s", command);

	/* Lancio nuova task */
	if ((pid = fork()) == 0) {
		sigset_t smask;
		char *psp[MAXARGS];
		char taskName[1024];

		/* ------ Inizio codice figlio ------ */

		/* Preparo argomenti */
		log_debug("Launch task \"%s\"", command);
		argTok(command, psp, MAXARGS);
		strncpy(taskName, psp[0], sizeof(taskName));

		int j=0;
		for (j=0; 1; j++) {
			if (psp[j] == NULL)
				break;
			log_debug("--- %d %s", j, psp[j]);
		}

		/* Sblocco i segnali eventualmenti bloccati dai processi */
		sigfillset(&smask);
		if (sigprocmask(SIG_UNBLOCK, &smask, NULL) < 0) {
			log_error("sigprocmask() for %s: %s", taskName, strerror(errno));
			_exit(100);
		}

		/* Redirige STDIN e STDOUT al socket */
		if ((dup2(fds[1], STDIN_FILENO) < 0)   ||
			(dup2(fds[1], STDOUT_FILENO) < 0)) {
			log_error("dup2() for %s: %s", taskName, strerror(errno));
			_exit(100);
		}
		close(fds[1]);

		/* Chiude tutti gli altri possibili file descriptor */
		for (j = 3;  j < sysconf(_SC_OPEN_MAX);  j++)
			close(j);

		/* Lancio taskName */

		execvp(taskName, psp);

		/* Se sono qui, execvp() e' fallita */
		log_fatal("exec() for %s:", taskName);

		/* ------ Fine codice figlio ------ */
	}
	else if (pid < 0) {
		/* Errore di fork */
		return -1;
	}
	else {
		log_debug("coprocess pid=%d", (int)pid);
	}

	/* Chiudi l'estremita` non utilizzata */
	close(fds[1]);

	/* Se richiesto, torna il pid */
	if (ppid != NULL)
		*ppid = pid;

	/* Ritorna il file pointer */
	return fds[0];
}

/******************************************************************************/
/*** Pacchetto I/O resistente alle SIGCHLD ************************************/
/******************************************************************************/


/*
 *	DESCRIZIONE
 *		Read non interrompibile.
 *		Stevens vI.
 *	VALORI DI RITORNO
 *		Quelli di read;
 */
ssize_t Read(int fd,			/** file descriptor */
			 void *buffer,		/** buffer da scrivere */
			 size_t nbuffer		/** N. caratteri da scrivere */)
{
	ssize_t ret;

	while ((ret = read(fd, buffer, nbuffer)) < 0) {
		/* le SIGCHLD hanno questa brutta caratteristica */
		if (errno != EINTR)
			break;
	}
	return ret;
}

/******************************************************************************/

/*
 *	DESCRIZIONE
 *		Write non interrompibile che scrive esattamente n caratteri.
 *		Stevens UNP.1 p. 78.
 *	VALORI DI RITORNO
 *		Quelli di write;
 */
ssize_t Writen(int fd,				/** file descriptor */
			   const char *vptr,	/** buffer da scrivere */
			   size_t n				/** N. caratteri da scrivere */)
{
	size_t nleft;
	char ptr[1024];
	ssize_t nwritten=0;
	ssize_t retval = n;

	memset(ptr,0,strlen(ptr));
	if(n > 0){
		snprintf(ptr,n+1,"%s",(char*)vptr);
		nleft = strlen(ptr);		// era n;
	}else
		return (-1);

	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) <= 0) {
		/* le SIGCHLD hanno questa brutta caratteristica */
			//log_info("Writen answer: %d, error: %s", nwritten, strerror(errno));
			if (errno == EINTR)
				nwritten = 0;	/* e rientra in write */
			else
				return (-1);	/* errore */
		}else{
			nleft -= nwritten;
//			ptr += nwritten;
		}
	}
	return (retval);
}

/******************************************************************************/

/*
 *	DESCRIZIONE
 *		helper per readline (cfr.).
 *		Stevens UNP.1 p. 80.
 *	VALORI DI RITORNO
 *		1: un carattere letto; 0: EOF; -1: errore
 */
ssize_t my_readc(int fd,			/** File descriptor */
				 char *ptr			/** Carattere da ritornare */)
{
	static int read_cnt = 0;
	static char *read_ptr;
	static char read_buf[MAXPRO_LCMD];
	const int mspause = 100;
	const int tout = 3000;		// timeout su mancata ricezione espresso in ms
	int n_iter = tout/mspause;	// Numero di iterazioni per diagnosticare un timeout

	if (read_cnt <= 0) {
again:
		if ((read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0) {
			if (errno == EINTR)
				goto again;
			else if (errno == EAGAIN) {
				msleep(mspause);
				if (n_iter--)
					goto again;
				// 20130208: risolta incongruenza. Dopo N retries,
				// veniva tornato -1+EAGAIN anziche` 0
				errno = 0;
				return 0;
			}
			return -1;
		}
		else if (read_cnt == 0)
			return (0);
		read_ptr = read_buf;
	}
	read_cnt--;
	*ptr = *read_ptr++;
	return(1);
}

/******************************************************************************/

/*
 *	DESCRIZIONE
 *		readline non interrompibile. Come fgets, ma lavora su fd e ritorna
 *		il numero di caratteri letti o -1.
 *		Stevens UNP.1 p. 80.
 *	VALORI DI RITORNO
 *		Numero di caratteri letti (0 per hangup); -1 + errno per errore.
 */
ssize_t readline(int fd,			/** File descriptor */
				 void *vptr,		/** Buffer da riempire */
				 size_t maxlen		/** Max. dimensione buffer */)
{
	int n;
	char c, *ptr;

	ptr = vptr;
	for (n = 1; (size_t)n < maxlen; n++) {
		int rc=0;
		if ((rc = my_readc(fd, &c)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;
		}
		else if (rc == 0) {
			if (n == 1)
				return 0;			/* EOF, no data read */
			else
				break;				/* EOF, some data was read */
		}
		else
			return -1;
	}

	*ptr = 0;
	return (n);
}

/*****************************************************************************
 ****** Timer *****************************************************************
 ******************************************************************************/

static int S_clk_tck;

/**
 *	DESCRIZIONE
 *		Inizializza il sottosistema di timing e setta l'istante T= del sistema.
 *
 *	VALORI DI RITORNO
 *		Nessuno.
 *
 */
void init_timer(void)
{
    int n = sysconf(_SC_CLK_TCK);

    S_clk_tck = n;
    log_debug("sysconf(_SC_CLK_TCK)=%d", S_clk_tck);
}

/**
 *  DESCRIZIONE
 *		Gestisce fino ad un massimo di tre errori consecutivi in times().
 *		In caso di erori ripetuti, abortisce.
 *
 *	VALORI DI RITORNO
 *		Come times; in piu' in caso di errore ripetuto, esce.
 *
 */
static clock_t wtimes(void)
{
	int j;
	struct tms dummy;
	clock_t t;

	for (j = 0; j < 3; j++) {
		t = times(&dummy);
		if (t != (clock_t)-1)
			return t;
		usleep(100000);     /* usec */
	}

	/* Errore grave */
	log_fatal("times repeated errors in wtimes");

	/* Qui non si arriva mai */
	return t;
}

/**
 *  DESCRIZIONE
 *		Ritorna un tempo elapsed in millisecondi da un istante
 *		arbitrario dopo il boot della macchina. Il tipo t_mclock e` definito
 *		come (signed) long long e quindi e` "grande a sufficienza" per
 *		supportare uptime di secoli.
 *
 *	VALORI DI RITORNO
 *		Tempo in millisecondi.
 *
 *  NOTE
 *		Chiama wtimes e quindi, in caso di errore fatale, abortisce.
 */
t_mclock mtimes(void)
{
	static t_mclock s_base = 0;
	static clock_t s_was = 0;
	clock_t now;

	now = wtimes();

	/* Attenzione: clock_t si comporta come un unsigned int */
	if ((int)s_was >= 0 && (int)now < 0)
		s_base += 0xFFFFFFFF;

	s_was = now;

	return (((int)now) + s_base)*(1000/S_clk_tck);
}



/*****************************************************************************
 ****** Utilita`**************************************************************
 *****************************************************************************/

/**
 *	DESCRIZIONE
 *		Sospende il thread corrente per (almeno) ms millisecondi.
 *
 *	VALORI DI RITORNO
 *		Nessuno.
 *
 */
void msleep(int ms)
{
	struct timespec tim, tlast;
	int count = 10;

	tim.tv_sec = ms/1000;
	tim.tv_nsec = (ms % 1000)*1000000L;
	while (nanosleep(&tim, &tlast) < 0 && count-- > 0)
		tim = tlast;
}

/**
 *	DESCRIZIONE
 *		Alloca dinamicamente spazio per copiarci la stringa passata.
 *
 *	VALORI DI RITORNO
 *		Puntatore a stringa valida, NULL se la stringa e` NULL o
 *		in caso di errore.
 *
 */
char * savestr(const char *str)
{
	char *p;

	if (str == NULL)
		return NULL;

	p = (char *)malloc(strlen(str)+1);
	if (p == NULL)
		return NULL;
	strcpy(p, str);
	return p;
}


/******************************************************************************/

/**
 *	DESCRIZIONE
 *		Trasforma una stringa di lunghezza specificata in un'altra stringa
 *		in cui il carattere '\' e i caratteri non stampabili sono stati
 *		convertiti rispettivamente nelle sequenze "\\" e "\x??". Se il flag
 *		hexOnly e` posto a 1 tutti i caratteri sono convertiti in esadecimale.
 *	VALORI DI RITORNO
 *		La lunghezza della stringa convertita.
 */
size_t buf2str(char *p_out,			/** Buffer destinazione */
			   size_t maxOutStrLen,	/** Dimensione del buffer */
			   const uint8_t *p_in,	/** Stringa sorgente */
			   size_t inBufLen,		/** Lunghezza della stringa */
			   int hexOnly			/** Flag conversione esadecimale */)
{
	size_t j;
	char *p_ob;

	p_ob = p_out;
	for(j = inBufLen;  j > 0;  j--) {
		uint8_t c;
		c = *p_in++;
		if (!hexOnly  &&  isprint(c)  &&  c != '\\') {
			/* Caratteri ascii stampabili */
			if ((size_t)(p_ob-p_out+1) > maxOutStrLen-1)
				break;
			*p_ob++ = c;
		}
		else if (!hexOnly  &&  c == '\\') {
			/* Carattere backslash */
			if ((size_t)(p_ob-p_out+2) > maxOutStrLen-1)
				break;
			*p_ob++ = '\\';
			*p_ob++ = '\\';
		}
		else {
			if ((size_t)(p_ob-p_out+4) > maxOutStrLen-1)
				break;
			sprintf(p_ob, "\\x%02x", (uint8_t) (c & 0x000000ff));
			p_ob += 4;
		}
	}
	*p_ob = '\0';

	return strlen(p_out);
}

/******************************************************************************/

/**
 *	DESCRIZIONE
 *		Trasforma una stringa ove le sequenze "\\", "\x??" e "\???"
 *		rappresentano rispettivamente i caratteri '\', e le
 *		rappresentazioni esadecimale e ottale di un byte.
 *	VALORI DI RITORNO
 *		>=0	la lunghezza della stringa convertita,
 *		<0  la posizione nella stringa da convertire ove si e` verificato un
 *			errore d'interpretazione.
 */
ssize_t str2buf(uint8_t *p_out,			/** Buffer di output */
				size_t maxOutBufLen,	/** Lunghezza del buffer */
				const char *p_in		/** Stringa da convertire */)
{
	enum { ESC_OFF, ESC_OCTAL, ESC_HEX, ESC_END };

	int j, flg_esc, char_parse = 0, char_to_parse = 0, c_built = 0;
	uint8_t *p_outOld;
	uint8_t c;

	flg_esc = ESC_OFF;
	p_outOld = p_out;
	for(j = 0;  (c = *(p_in+j)) != '\0';  j++) {
		if (c == '\\'  &&  flg_esc == ESC_OFF) {
			flg_esc = ESC_OCTAL;
			char_to_parse = 3;
			char_parse = 0;
			c_built = 0;
		}
		else {
			if (flg_esc != ESC_OFF) {
				switch (c) {
				case 'x':
					if (char_parse > 0)
						return -1 * (j+1);
					flg_esc = ESC_HEX;
					char_to_parse = 2;
					break;

				case '\\':
					flg_esc = ESC_END;
					break;

				default:
					if (flg_esc == ESC_HEX) {
						c = (uint8_t) toupper(c);
						if ((c < '0' || c > '9') && (c < 'A' || c > 'F'))
							return -1 * (j+1);

						c_built = c_built * 16;
						c_built += (c <= '9') ? c-'0' : c-'A'+10;
					}
					else {
						if (c < '0' || c > '7')
							return -1 * (j+1);

						c_built = (c_built * 8) + c - '0';
					}
					char_parse++;

					if (char_to_parse == char_parse) {
						c = (uint8_t) c_built;
						flg_esc = ESC_END;
					}
					break;
				}
			}

			if (flg_esc == ESC_OFF  ||  flg_esc == ESC_END) {
				*p_out++ = c;
				if ((size_t)(p_out-p_outOld) >= maxOutBufLen)
					return p_out-p_outOld;
				flg_esc = ESC_OFF;
			}
		}
	}

	if (flg_esc != ESC_OFF)
		return -1 * j;

	return p_out-p_outOld;
}

/*
 * DESCRIZIONE
 * 	Calcola l'intervallo di tempo in secondi dalla data passata (formato HHDDMMYY)
 *	ad ora.
 * VALORI DI RITORNO
 * 	Numero di secondi, -1 per data non valida.
 */

int since_now(const char *date)
{
	struct tm tm;
	int hh, dd, mm, yy;

	if (sscanf(date, "%2d%2d%2d%2d", &hh, &dd, &mm, &yy) != 4)
		return -1	;

	memset(&tm, 0, sizeof(tm));
	tm.tm_hour = hh;
	tm.tm_mday = dd;
	tm.tm_mon = mm-1;		/* month */
	tm.tm_year = 100+yy;	/* year */
	tm.tm_isdst = -1;		/* Ora legale: non disponibile */

	time_t told = mktime(&tm);

	int n = (int)difftime(time(NULL), told);
	if (n < 0)
		return -1;

	return n;
}


