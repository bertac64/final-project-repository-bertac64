/**
 * main.c - main routine of epxcore.
 *
 * (C) 2024 bertac64
 *
 * 21/11/2024 - edit server status management
 */

/* POSIX.1 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>

/* SOCKET */
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

/* Local Libraries */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "epxcore.h"
#include "fpga.h"
#include "errors.h"
#include "main.h"
#include "command.h"
#include "util.h"
#include "sharedVar.h"
#include "middle.h"

#define ECG_TRIGGER_CHECK
#define SYS_TIDLE = 172800

/* ========================================================================= */
/* Global variables */

int G_flgVerbose = 0;		/* verbose level for log and debug */
int G_thPipe[2];			/* Communication pipe from task to main thread */
Memory SRAM;
volatile uint32_t *mm;
time_t valm_f = 0;
int va_th = 0;
int noFPGA = 0;
/* ========================================================================= */
/* Static globals */

static long S_poll_tout_us = 500000; /* timeout poll (usec) */
static short S_asport = SRV_PORT;	 /* well-known port del server */
static int S_conn_tout = 600000;	 /* timeout connessione (msec) */
static int S_maxfd;					 /* max N. of fd managed in select */
static int S_aserfd;				 /* fd del server (cfr. asport) */
static int S_pipefd;				 /* Estremita` in lettura della pipe */
static int S_tomainfd;				 /* Estremita` in scrittura della pipe */
static fd_set S_allset;				 /* fd gestiti da select */

/* ========================================================================= */
/* Internal Prototypes */

static void sig_trap(int sig);
static void sig_pipe(int sig);
static void sig_chld(int sig);
static void before_exit(void);
static void f_cleanup(void);

/* ========================================================================= */
/* Actions table to be done at timeout */

static t_actions S_actions[] = {
    /*   name	tout 	funzione	polling interval (in ms) */
	{ "CLN",	0,		f_cleanup,	86400000 },	/* timeout connection (1 day) */
	{ NULL,		0, 		NULL, 		0 }		/* Tappo */
};


/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

/**
 * Program usage print. Exit with error (exitcode=1).
 */
void usage(const char *prgname, const char *s)
{
	fprintf(stderr, "%s\n\n", s);
	fprintf(stderr, "usage : %s [-d level][-v][-p prop]\n", prgname);
	fprintf(stderr, "\twhere: -d level      debug level\n");
	fprintf(stderr, "\t       -h            usage\n");
	fprintf(stderr, "\t       -p prop       load properties file\n");
	fprintf(stderr, "\t       -v            print version, hardware platform and build date/time and then exit\n");
	fprintf(stderr, "\n");
	exit(1);
}

/* ========================================================================= */

/**
 * main of the program (POSIX.1)
 */
int main(int argc, char **argv)
{
	int c, j;
	char z[MAXSTR];
	pthread_t stato_th;
	fd_set rset;
	opterr = 0;

	while ((c = getopt(argc, argv, "d:hv:")) != -1) {
		switch (c) {
		case 'd':
			G_flgVerbose = atoi(optarg);
			break;

		case 'h':
			usage(PRGNAME, "");
			break;

		case 'v':
			printf("%s;%s;%s;%s;%s\n", argv[0], PGM_VERS, get_cputype(z), __DATE__, __TIME__);
			return 0;
			break;

		default:
			usage(PRGNAME, "Invalid parameters");
			break;
		}
	}

	if (argc-optind != 0)
		usage(PRGNAME, "Too many parateters");

	/* Initialization log */
	log_init(PRGNAME);
	log_delout(stderr); /* remove stderr enabled by default */

	if (G_flgVerbose > 0) {
		switch (G_flgVerbose) {
		case 1:
			log_addout(stderr, e_log_error);
			break;
		case 2:
			log_addout(stderr, e_log_warning);
			break;
		case 3:
			log_addout(stderr, e_log_info);
			break;
		default:
			log_addout(stderr, e_log_debug);
			break;
		}
	}

	/**** Before exit ****/
	atexit(before_exit);

	/**** Signal ****/
	signal(SIGINT, sig_trap);
	signal(SIGTERM, sig_trap);
	signal(SIGPIPE, sig_pipe);
	signal(SIGCHLD, sig_chld);

	/* Initializations */
	epsmdrv_open();
	stato_init();
	init_timer();

	/* Initialization of shared variables (status, ...) */
	initSharedVar();

	/* check fpga presence */
	if (start_power(FALSE) < 0){
		log_info("FPGA not connected!");
		noFPGA = 1;
	}

	/* Start the status update thread */
	if (pthread_create(&stato_th, NULL, eval_state, NULL) < 0) {
		log_perror("can't create thread for FPGA polling");
		return 1;
	}

	msleep(100);
	set_state(e_blank);

	log_debug("Cleaning channel descriptors");
	/* Clear channel descriptors */
	FD_ZERO(&rset);
	FD_ZERO(&S_allset);

	log_debug("Opening communication channel");
	/*** Server channels Configuration ***/
	/* Passive channel configuration */
	S_aserfd = ch_conf(S_asport);
	(void)stato_add(S_aserfd, O_INTERNAL);
	FD_SET(S_aserfd, &S_allset);
	if (S_maxfd < S_aserfd)
		S_maxfd = S_aserfd;
	log_debug("Starting on port [%d] accepting %d clients", S_asport,
			MAXPRO_CLI);

	/* creating the pipe for the channel from threads */
	if (pipe(G_thPipe) < 0) {
		log_perror("can't create pipe from thread");
		exit(1);
	}
	S_pipefd = G_thPipe[0];
	S_tomainfd = G_thPipe[1];
	(void)stato_add(S_pipefd, O_INTERNAL);
	FD_SET(S_pipefd, &S_allset);
	if (S_maxfd < S_pipefd)
		S_maxfd = S_pipefd;

	log_debug("Internal pipe created on fd [%d->%d]", S_pipefd, S_tomainfd);

	/* Main Loop (infinite) */
	log_debug("entering in select ----------------");

	while (1) {
		char chunk[MAXPRO_LCMD], cmd[MAXPRO_LCMD], stmp[MAXSTR];
		ssize_t nb;
		int s, nready;
		struct timeval tout;
		int clientFd, iter;
		t_stato *p_stato;

		tout.tv_sec = 0;
		tout.tv_usec = S_poll_tout_us;

		rset = S_allset;
		while ((nready = select(S_maxfd+1, &rset, NULL, NULL, &tout)) < 0) {
			if (errno == EINTR)
				continue;
			else
				log_fatal("select: %s", strerror(errno));
		}

		/* *** timeout management *** */

		/* check tabel of actions to be done during timeout */
		for (j=0; S_actions[j].f != NULL; j++) {
			t_mclock tnow = mtimes();

			if (tnow > S_actions[j].tout) {

				/* N.B: it is assumed that the action service time
				* is LESS than the rescheduling time. Otherwise
				* the action may starvation.
				*/
				S_actions[j].f();

				/* It may happen that as a result of the action, one or
				* more channels are closed. A check must be made
				* at the select() level.
				*/
				iter = 0;
				while ((p_stato = stato_iter(&iter)) != NULL) {
					if (btst(p_stato->options, O_MUST_CLOSE)) {
						int tmpfd = p_stato->fd;
						if (tmpfd >= 0) {
							stato_close(p_stato);
							FD_CLR(tmpfd, &S_allset);
							FD_CLR(tmpfd, &rset);
						}
						bclr(p_stato->options, O_MUST_CLOSE);
					}
				}

				/* calculates the next timeout expiration */
				S_actions[j].tout = tnow + (t_mclock)S_actions[j].delay;
			}
		}

		/* Exited due to timeout: re-enter poll */
		if (nready == 0)
			continue;

		/* Exited due regular event */
		log_debug( "wakeup from select ------------");

		/* checking all channels */

		/* passive channel of the server --------------------------------------- */
		if (FD_ISSET(S_aserfd, &rset)) {
			/* Connection request */

			struct sockaddr_in cliaddr;
			socklen_t clilen;

			clilen = sizeof(cliaddr);
			s = accept(S_aserfd, (struct sockaddr *)&cliaddr, &clilen);
			if (s < 0)
				log_fatal("accept: %s", strerror(errno));

			if (stato_add(s, 0) < 0) {
				/* No requests */
				log_debug("*** NEW connection rejected: "
												"too many (%d) clients", j);
				close(s);
			}
			else {
				/* Found request */
				if (s > S_maxfd)
					S_maxfd = s;
				log_debug("NEW connection accepted "
										"[%d/%d]: fd=%d", j, MAXPRO_CLI, s);

				FD_SET(s, &S_allset);
			}

			/* Send the banner */
			nb = makeBanner(cmd, s);
			if (nb > 0){
				write2client(s, cmd, nb);
			}else
				log_error("Banner not sent");
			if (--nready <=0)
				continue;
		}

		/* Clients --------------------------------------------------------- */
		iter = 0;
		while ((p_stato = stato_iter(&iter)) != NULL) {
			s = p_stato->fd;
			if (s < 0)
				continue;

			if (FD_ISSET(s, &rset)) {
				/* reading chunk */
				nb = Read(s, chunk, sizeof(chunk));
				if (nb <= 0) {
					/* Hangup from client */
					if (nb < 0) {
						log_debug( "read: %s", strerror(errno));
					}

					if (isdebug())
						printf("*** CLI connection hangup: fd=%d\n", s);
					stato_close(p_stato);

					FD_CLR(s, &S_allset);
				}
				else {
					char *p;

					log_debug( "reading %d bytes", nb);
					if (isdebug()) {
						printf("Received the following bytes:\n");
						log_dump(chunk, nb);
					}

					/* Assembly of a complete command (cmd) */
					p = chunk;
					while ((nb = abcr(p_stato, p, nb, cmd, sizeof(cmd))) > 0) {
						/* messagge coming on the main pipe ? */
						if (s == S_pipefd) {
							strncpy(stmp, cmd, CLIENTHEADER);
							stmp[CLIENTHEADER] = '\0';
							sscanf(stmp, "%x", (unsigned int *)&clientFd);

							/* in the meantime the customer could be dead */
							t_stato *p_cli = stato_getbyfd(clientFd);
							if (p_cli != NULL) {
								/* send the answer */
								if (write2client(clientFd, cmd+CLIENTHEADER,
														nb-CLIENTHEADER) < 0) {

									FD_CLR(clientFd, &S_allset);
									stato_close(p_cli);
								}
							}
							else {
								/* silently work */
								;
							}
						}
						else {
							/* It's an ordinary client: run the command */
							char buffer[MAXPRO_LCMD];
							nb = elaboraCmd(p_stato, buffer, cmd, nb);

							// If prompted, log the received command
							log_warning("[%s]:\"%s\"",
											state_str(get_state()), cmd);

							if (nb == NEED_TO_CLOSE) {
								(void)write2client(s, buffer, strlen(buffer));
								FD_CLR(p_stato->fd, &S_allset);
								stato_close(p_stato);
							}
							else {
								/* send the answer */
								if (nb > 0) {
									if (write2client(s, buffer, nb) < 0) {
										FD_CLR(p_stato->fd, &S_allset);
										stato_close(p_stato);
									}
								}
							}
						}

						p = NULL;
						nb = 0;
					}

					if (nb < 0) {
						/* Protocol error, closing the channel */
						stato_close(p_stato);
						log_warning("protocol error");
					}
					else if (nb == 0) {
						/* Data accepted, but the package is not complete */
						log_debug( "abcr returns 0");
					}
				}

				/* Update timeout time */
				if (p_stato->fd >= 0)
					p_stato->last_t = mtimes();
			}

			if (--nready <= 0 )
				continue;
		}

		log_debug( "re entering in poll ----------------------");
	}

	epsmdrv_close();
	close(SRAM.fd_mem);
	return 0;
}

/*****************************************************************************/

/**
* Send a message to the client identified by fd.
* Returns the number of characters written for success, -1+errno for failure.
*/
ssize_t write2client(int fd, const char *answer, size_t nb)
{
	ssize_t ret = 0;
	char buffer[MAXPRO_LCMD];
	size_t len;

	//log_info("ANS: %s", answer);	
	if ((nb > 0)&&(nb<=1024)){
		/* cook the answer */
		len = cook(buffer, answer, nb);

		/* send the answer */
		ret = Writen(fd, buffer, len);
		if (ret < 0)
			log_warning("write2client(): %s", strerror(errno));
		else
			log_debug("written %d bytes", ret);
	}else{
		ret = (-1);
		log_error("wrong message length: %d", nb);
	}

	return ret;
}

/*****************************************************************************/

/**
* DESCRIPTION
* Sends a message to the main thread (server). For use by command threads.
* The function is synchronized.
*
* RETURN VALUES
* Returns the number of characters written for success, -1+errno for failure.
*/
static pthread_mutex_t S_writeMutex = PTHREAD_MUTEX_INITIALIZER;

ssize_t write2main(const char *answer, size_t nb, size_t fd)
{
	ssize_t ret;
	char buffer[MAXPRO_LCMD+CLIENTHEADER];

	pthread_mutex_lock(&S_writeMutex);

	sprintf(buffer, "%0*zX", CLIENTHEADER, fd);
	nb = cook(buffer+CLIENTHEADER, answer, nb); 	/* cook the answer */

	/* send the answer */
	ret = Writen(S_tomainfd, buffer, nb+CLIENTHEADER);

	if (ret < 0)
		log_warning("write2main(): %s", strerror(errno));
	else
		log_debug("write2main %d bytes", nb);

	pthread_mutex_unlock(&S_writeMutex);

	return ret;
}

/**
* Sends a message to the main thread (server), intended for ALL clients.
* Returns the number of characters written for success, -1+errno for failure.
*/
static pthread_mutex_t S_writeAllMutex = PTHREAD_MUTEX_INITIALIZER;

void write2mainAll(const char *answer, size_t nb)
{
	t_stato *p_stato;
	int iter;

	pthread_mutex_lock(&S_writeAllMutex);
	iter = 0;
	while ((p_stato = stato_iter(&iter)) != NULL) {
		if (btst(p_stato->options, O_INTERNAL))
			continue;
		(void)write2main(answer, nb, p_stato->fd);
	}
	pthread_mutex_unlock(&S_writeAllMutex);
}

/*****************************************************************************/

/**
* DESCRIPTION
* Read the current state.
*
* RETURN VALUES
* The current state
*
* NOTES
* The function is synchronized.
*/
enum e_state get_state(void)
{
	t_sharedVar *p;
	enum e_state s;

	p = acquireSharedVar();
	s = p->state;
	releaseSharedVar(&p);
	return s;
}

/**
* DESCRIPTION
* Sets the current state.
*
* RETURN VALUES
* The previous state.
*
* NOTES
* The function is synchronized.
*/
enum e_state set_state(enum e_state new_s)
{
	t_sharedVar *p;
	enum e_state s;

	p = acquireSharedVar();
	s = p->state;
	p->state = new_s;
	releaseSharedVar(&p);
	return s;
}

/**
 * Decoding server status.
 */
const char * state_str(enum e_state s)
{
	switch (s) {
	case e_init:		/* Init Status */
		return E_INIT;
		break;
	case e_ready:		/* Ready Stato */
		return E_READY;
		break;
	case e_blank:		/* Intermediate Status: should not be detected */
		return E_BLANK;
		break;
	case e_idle:
	default:		/* Device in Idle */
		return E_IDLE;
		break;
	}
	return "????";
}



/**
 * DESCRIPTION
 * 	helper to manage the communication error
 */
void comm_error(const char *msg)
{
	char buffer[MAXSTR];

	log_error(msg);
	int nb = warningMsg(W_HIGH, FALSE, e_errFPGACommunications_idle,
							buffer, "HW communications error");
	write2mainAll(buffer, nb);
	(void)set_state(e_idle);
}

/**
* DESCRIPTION
* Thread that evaluates state.
*
* RETURN VALUES
* None: never returns.

*/
void * eval_state(void *par)
{
	char buffer[MAXPRO_LCMD];
	size_t nb = 0;
	int sampling_period_ms = 100;	// status/alarms sampling period
	uint16_t fpga_old_state = 0;
	uint16_t fpga_state = 0x1800;
	fpga_data_t state;
	fpga_data_t alrm;
	uint16_t fpga_alarm = 0;
	uint16_t fpga_old_alarm;
	enum e_state old_s = get_state();
	fpga_state = fpga_old_state = 0x1800;
	fpga_alarm = fpga_old_alarm = 0;

	int imp_count = 0;
	uint16_t fpga_old_state2 = 0;
	uint16_t fpga_state2 = 0;

	(void)par;

	while (1) {
		enum e_state s = get_state();

		if (s != e_idle) {
			if (!noFPGA){
				do{
					state = fpga_peek(r_State);				//read fpga state register
					if (state == -1){
						comm_error("Can't get FPGA State");
						continue;
					}else{
						fpga_state = (state & 0x0000FFFF);
						fpga_state2 = (state>>16);
					}
					alrm = fpga_peek(r_Alarm);				//read fpga alarm register
					if (alrm == -1) {
						comm_error("Can't get FPGA Alarm");
						continue;
					}else
						fpga_alarm = alrm;

					if (imp_count > 3){
						imp_count = 0;
						msleep(1000);
						(void)set_state(e_ready);
						break;
					}
					else
					{
						if (fpga_state == fpga_alarm && fpga_state == fpga_state2){
							comm_error("FPGA clock freezed. Trying to recover it.");
							imp_count ++;
							msleep(100);
						}
					}
				}while(fpga_state == fpga_alarm && fpga_state == fpga_state2);

				if ((fpga_old_state != fpga_state ||
					 fpga_old_state2 != fpga_state2 ||
					 fpga_old_alarm != fpga_alarm)) {
					nb = sprintf(buffer, "State=%04X State2=%04X Alarm=%04X", fpga_state, fpga_state2, fpga_alarm);
					write2mainAll(buffer, nb);
				}
			}else{
				(void)set_state(e_ready);
			}
			
			/* status calculation **************************************** */
			if (s == e_blank) {
				if ((fpga_state & MASK_READY)!= PATT_READY){
					// Wrong Condition
					log_warning("HW not in ready");
					nb = warningMsg(W_HIGH, FALSE,
									e_errAsyncHWInUnexpectedState, buffer,
									"HW not in ready ($1=%04X;))",
									(0x00FFFF)&fpga_state);
					write2mainAll(buffer, nb);
				}else{
					s = e_ready;
				}
			}
			else {
				if ((fpga_state & MASK_READY) == PATT_READY)
				{
					s = e_ready;
				}
				else{
					log_warning("HW not in ready)");
					nb = warningMsg(W_HIGH, FALSE,
									e_errAsyncHWInUnexpectedState, buffer,
									"HW not in ready ($1=%04X;))",
									(0x00FFFF)&fpga_state);
					write2mainAll(buffer, nb);
					s = e_blank;
				}
			}
			sampling_period_ms = 2;
			// set status
			(void)set_state(s);
		}
		else{ /* s == e_idle */
			sampling_period_ms = 100;
		}

		// Report changes to all connected customers
		if (s != old_s) {
			nb = stateVar(buffer, old_s, s);
			write2mainAll(buffer, nb);

			if (s == e_idle)
				log_error("Server is idle");

			old_s = s;
		}

		(void)set_state(s);

		// wait for the sampling time
		msleep(sampling_period_ms);
	}

	/* Returns a value */
	return NULL;
}


/******************************************************************************/

/**
 *	General signal manager.
 */
static void sig_trap(int sig /** Come da ANSI */)
{
	log_debug("signal %d caught: exiting...", sig);
	exit(1);
}

/******************************************************************************/

/**
 *	SIGPIPE Manager.
 */
static void sig_pipe(int sig /** Come da ANSI */)
{
	/* probabilmente e` inutile */
	log_debug("SIGPIPE (%d) caught: ignored", sig);
}

/******************************************************************************/

/**
 *	SIGCHLD Manager
 */
static void sig_chld(int sig /** Come da ANSI */)
{
	pid_t pid;
	int stato;

	while ((pid = waitpid(-1, &stato, WNOHANG)) > 0)
		log_debug("SIGCHLD (%d): child pid=%d terminated", sig, pid);
}

/******************************************************************************/

/**
 *	Exit handler.
 */
static void before_exit(void)
{
	log_info("*** program terminated");
}

/******************************************************************************/

/**
* Checks for inactivity disconnect timeout on the channel.
*/
static void f_cleanup(void)
{
	int state=0;
	t_stato *p_stato;

	while ((p_stato = stato_iter(&state)) != NULL) {
		if (p_stato->fd >= 0 && !btst(p_stato->options, O_INTERNAL)) {
			/* Check if there is no activity for more than so many seconds */
			if (p_stato->last_t + S_conn_tout < mtimes()) {
				/* Timeout, must close */
				bset(p_stato->options, O_MUST_CLOSE);
				/* The caller will take care of setting the
				* bits of the select descriptors.
				*/
				log_warning("Connection timeout (fd=%d)", p_stato->fd);
			}
		}
	}
}


