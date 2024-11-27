/**
 * util.c - utility functions
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
#include <stdint.h>		/* Integrate the elementary types from sys/types.h */
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
/* Connected customer status management module								  */
/******************************************************************************/

static t_stato S_stato[MAXPRO_CLI];	/* Table with client status */

/**
 * Initialize the customer table.
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
 * Adds (creates) a customer in the managed customers table
 * Returns the index of the created customer, -1 for error.
 */
int stato_add(int fd, int options)
{
	int j;

	/* Search a free session descriptor */
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
 * Iterate over the list of managed customers.
 * Returns a valid pointer to the "next" client or NULL at the end of the list.
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
 * DESCRIPTION
 * Find a customer in the connected customers table. The search key
 * is the fd of the connection.
 * RETURN VALUES
 * Returns a pointer to the descriptor or NULL on error (not found).
 */
t_stato *stato_getbyfd(int fd)
{
	int j;

	/* Search a free session descriptor */
	for (j=0; j<MAXPRO_CLI; j++) {
		if (S_stato[j].fd == fd)
			return &S_stato[j];
	}
	log_warning("stato_getbyfd(): fd <%d> non trovato", fd);
	return NULL;
}

/**
 * Closes the communication channel with the customer and frees the slot
 * matching.
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
 * Deletes a running task and clears the corresponding structure.
 */
void task_cancel(t_task *t			/** task descriptor */)
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
 * Initializes the structure of a task.
 */
void task_create(t_task *t,			/** task descriptor */
				 int cnt, 			/** Progressive ref. to the command */
				 t_cmd *pCmd, 		/** Command Descriptor */
				 t_stato *p_stato	/** Channel Descriptor/status */)
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
/* Channel and protocol management module									  */
/******************************************************************************/

/**
 *	DESCRIPTION
 * Configure the passive service channel.
 * RETURN VALUES
 * A passive socket. Mistakes are fatal.
 */
int ch_conf(short portnum	/** Number of the well-known port */)
{
	int s, on = 1;
	unsigned int j;
	struct sockaddr_in epc_sin;
	int bind_retval = -1;

	/* open main realignment socket */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		log_fatal("socket: %s", strerror(errno));

	log_debug("created socket %d", s);

	/* set the address to "reusable", to avoid TIME_WAIT;
		N.B.: the fact that a socket is reusable does not
		it means that two instances can start on the same one
		address/port pair. If another server is running,
		bind() will fail with EADDRINUSE. */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
		log_fatal("setsockopt(SO_REUSEADDR): %s", strerror(errno));
	log_debug("(socket reusable)");

	/* make bind with its "well known port number" */
	bzero((char *)&epc_sin, sizeof(epc_sin));
	epc_sin.sin_family = AF_INET;
	epc_sin.sin_port = htons(portnum);	/* attention: htons */

	// retry bind if fails
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
		log_debug("Bind not executed after 20 tentatives");
		return (-1);
	}else
		log_debug("done bind(): porta %d", portnum);

	/* accept connections from all hosts; waiting queue: 5 */
	if (listen(s, 5) == -1) {
		log_fatal("listen: %s", strerror(errno));
		exit(1);
	}
	log_debug("done listen()");
	log_debug("service channel configured");

	/* Return */
	return s;
}

/**
 * "Cook" a buffer to turn it into a valid message to send.
 * Returns the length of the buffer.
 */
size_t cook(char *buffer, 		/* Exit buffer */
			 const char *cmd,	/* Input buffer */
			 size_t	buflen		/* buffer length */)
{
	size_t len = buflen;
	if (buflen > 2) {
		/* It's a zero-terminated string, so length doesn't matter */
		len = snprintf(buffer, MAXPRO_LCMD, "%s\r\n", cmd);

		/* debug */
		if (isdebug())
			log_dump((char *) buffer, len);
	}else
		log_error("Wrong buffer length: %d", len);

	/* returns the dimentions */
	return len;
}

/**
 * Assemble a complete message on reception.
 * Can be called cyclically: if chunk=NULL just look for
 * return any next complete command present in the buffer.
 * Returns the length of the buffer; 0 if the buffer is not yet ready,
 * -1+errno in case of error (the channel must be closed).
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
		 	/* serious error; protocol violation? */
		 	errno = EBADMSG;
		 	return (ssize_t)-1;
		}

		memcpy(&(p->b[p->nb]), chunk, chlen);
		p->nb += chlen;
	}

	/* are strings: zero-termined for safety */
	p->b[p->nb] = '\0';
	if ((ps = strstr(p->b, "\r\n")) == NULL)
		return 0;

	/* packet found: detach it */
	oblen = ps - p->b;
	memcpy(outbuf, p->b, oblen);
	outbuf[oblen] = 0;

	/* buffer alignment */
	cl = oblen + 2;			/* length + terminator CRLF */
	memmove(p->b, &(p->b[cl]), p->nb - cl);
	p->nb -= cl;

	return (ssize_t)oblen;
}

/******************************************************************************/
/* Management of token separation and command execution						  */
/******************************************************************************/

/**
 *	Desallocates memory allocated for tokens (cfr. argTok).
 */
void freeTok(char **psp,	/* Array of pointers to token */
			 int32_t nArgs	/* Number of token */)
{
	int j;

	for(j = 0;  j < nArgs;  j++) {
		if (psp[j] != NULL)
			free(psp[j]);
	}
}

/******************************************************************************/

/**
 * Transforms a string of commands into tokens, storing them in an array that
 * signals the end of the list by NULL. To de-allocate tokens
 * you must use the freeTok() function.
 * Return:
 * >=0 number of tokens obtained from the string,
 * -1 if the command string is invalid,
 * -2 if there are too many arguments.
 */
int32_t argTok(const char *cmdStr, 	/** String to be tokenized */
			   char **psp,			/** Array of pointers to a token */
			   int32_t maxArgs		/** Max number of allowed token */)
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
 *	DESCRIPTION
 * Runs a coprocess like popen(), but returns a file descriptor
 * bidirectional.
 * RETURN VALUES
 * File descriptors; -1 for error (see errno).
 * Via *ppid, the process pid also returns.
 */
int bipopen(const char *command,	/** Command */
			pid_t *ppid	/** ptr to pid to be returned or NULL */)
{
	int fds[2];
	pid_t pid;
	const size_t MAXARGS=100;		/* Max allower arguments */

	/* new full duplex socket pipe */
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) < 0)
		return -1;

	log_debug("FORKING for %s", command);

	/* Lancio nuova task */
	if ((pid = fork()) == 0) {
		sigset_t smask;
		char *psp[MAXARGS];
		char taskName[1028];

		/* ------ initalizing child code ------ */

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
/*** Packet I/O resistent to SIGCHLD ************************************/
/******************************************************************************/
/**
 * DESCRIPTION
 * Runs a coprocess like popen(), but returns a file descriptor
 * bidirectional.
 * RETURN VALUES
 * File descriptors; -1 for error (see errno).
 * Via *ppid, the process pid also returns.
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
 *	DESCRIPTION
 * Non-interruptible write that writes exactly n characters.
 * Stevens UNP.1 p. 78.
 * RETURN VALUES
 * Those of write;
 */
ssize_t Writen(int fd,				/** file descriptor */
			   const char *vptr,	/** buffer da scrivere */
			   size_t n				/** N. caratteri da scrivere */)
{
	size_t nleft;
	char ptr[1024]="";
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
 *	DESCRIPTION
 * helper for readline (cf.).
 * Stevens UNP.1 p. 80.
 * RETURN VALUES
 * 1: a read character; 0: EOF; -1: error
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
 *	DESCRIPTION
 * non-interruptible readline. Like fgets, but works on fd and returns
 * the number of characters read or -1.
 * Stevens UNP.1 p. 80.
 * RETURN VALUES
 * Number of characters read (0 for hangup); -1 + errno by mistake.
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
 *	DESCRIPTION
 * Initialize the timing subsystem and set the system time T=.
 *
 * RETURN VALUES
 *		Nobody.
 *
 */
void init_timer(void)
{
    int n = sysconf(_SC_CLK_TCK);

    S_clk_tck = n;
    log_debug("sysconf(_SC_CLK_TCK)=%d", S_clk_tck);
}

/**
 *  DESCRIPTION
 * Handles up to three consecutive errors in times().
 * In case of repeated errors, it aborts.
 *
 * RETURN VALUES
 * Come times; furthermore, in case of repeated errors, it exits.
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
 *  DESCRIPTION
 * Returns an elapsed time in milliseconds from an instant
 * arbitrary after machine boot. The t_mclock type is defined
 * as (signed) long long and therefore is "large enough" for
 * Support uptime of centuries.
 *
 * RETURN VALUES
 * Time in milliseconds.
 *
 * NOTES
 * Calls wtimes and then, on a fatal error, aborts.
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
 ****** Utilities **************************************************************
 *****************************************************************************/

/**
 *	DESCRIPTION
 * Suspend the current thread for (at least) ms milliseconds.
 *
 * RETURN VALUES
 *		Nobody.
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
 *	DESCRIPTION
 * Dynamically allocate space to copy the passed string into.
 *
 * RETURN VALUES
 * Pointer to valid string, NULL if string is NULL or
 * in case of error.
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
 *	DESCRIPTION
 * Transforms a string of specified length into another string
 * where the character '\' and non-printing characters have been
 * converted to the sequences "\\" and "\x??" respectively. If the flag
 * hexOnly is set to 1 all characters are converted to hexadecimal.
 * RETURN VALUES
 * The length of the converted string.
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
 *	DESCRIPTION
 * Transform a string where the sequences "\\", "\x??" And "\???"
 * represent the characters '\', and le respectively
 * hexadecimal and octal representations of a byte.
 * RETURN VALUES
 * >=0 the length of the converted string,
 * <0 the position in the string to be converted where a
 * error of interpretation.
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
 * DESCRIPTION
 * Calculate the time interval in seconds from the past date (HHDDMMYY format)
 * to now.
 * RETURN VALUES
 * Number of seconds, -1 for invalid date.
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


