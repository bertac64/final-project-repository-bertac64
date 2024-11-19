/**
 * main.c - main del programma epxcore.
 *
 * (C) 2024 bertac64
 *
 */

/* POSIX.1 */
#include <stdio.h>
//#include "../bsp_0/ps7_cortexa9_0/include/xparameters.h"
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

/* Librerie locali */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "epxcore.h"
#include "errors.h"
#include "main.h"
#include "command.h"
#include "util.h"
#include "sharedVar.h"

#define ECG_TRIGGER_CHECK

/* ========================================================================= */
/* Globali al progetto */

int G_flgVerbose = 0;		/* livello di verbosita` per log e debug */
int G_thPipe[2];			/* Comunicazione da task a main thread */
t_map * G_map = NULL;		/* Mappa delle properties */
char *G_fname_map = NULL;	/* Nome del file di properties */
t_device G_device; 			/* struttura delle info sul device*/
Memory SRAM;
volatile uint32_t *mm;
time_t valm_f = 0;
int va_th = 0;

/* ========================================================================= */
/* Static globals */

static long S_poll_tout_us = 500000; /* timeout poll (usec) */
static short S_asport = SRV_PORT;	 /* well-known port del server */
static int S_conn_tout = 600000;	 /* timeout connessione (msec) */
static int S_maxfd;					 /* N. max di fd gestiti in select */
static int S_aserfd;				 /* fd del server (cfr. asport) */
static int S_pipefd;				 /* Estremita` in lettura della pipe */
static int S_tomainfd;				 /* Estremita` in scrittura della pipe */
static fd_set S_allset;				 /* fd gestiti da select */

/* ========================================================================= */
/* Prototipi interni */

static void sig_trap(int sig);
static void sig_pipe(int sig);
static void sig_chld(int sig);
static void before_exit(void);
static void f_cleanup(void);

/* ========================================================================= */
/* Tabella delle azioni da fare su timeout */

static t_actions S_actions[] = {
    /*   name	tout 	funzione	Intervallo di poll (in ms) */
	{ "CLN",	0,		f_cleanup,	86400000 },	/* Calcolo timeout connessione (1giorno) */
	{ NULL,		0, 		NULL, 		0 }		/* Tappo */
};


/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

/**
 * Stampa l'usage del programma e esce con errore (exitcode=1).
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
 * main del programma (POSIX.1)
 */
int main(int argc, char **argv)
{
	int c, j;
	pthread_t stato_th;
	fd_set rset;
	opterr = 0;

	while ((c = getopt(argc, argv, "d:hvp:")) != -1) {
		switch (c) {
		case 'p':
			G_fname_map = optarg;
			break;

		case 'd':
			G_flgVerbose = atoi(optarg);
			break;

		case 'h':
			usage(PRGNAME, "");
			break;

		case 'v':
			printf("%s;%s;%s;%s\n", argv[0], PGM_VERS, __DATE__, __TIME__);
			return 0;
			break;

		default:
			usage(PRGNAME, "Invalid parameters");
			break;
		}
	}

	if (argc-optind != 0)
		usage(PRGNAME, "Too many parateters");

	/* Inizializzazione log */
	log_init(PRGNAME);
	log_delout(stderr); /* rimuovi stderr abilitato di default */

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

	/* Inizializzazioni varie */
	stato_init();
	init_timer();

	/* Inizializza le variabili condivise (stato, ...) */
	initSharedVar();

	/* Fai partire il thread di aggiornamento dello stato */
	if (pthread_create(&stato_th, NULL, eval_state, NULL) < 0) {
		log_perror("can't create thread for FPGA polling");
		return 1;
	}

	msleep(100);
	set_state(e_blank);

	log_debug("Azzero i descrittori di canale");
	/* Azzera i descrittori di canale */
	FD_ZERO(&rset);
	FD_ZERO(&S_allset);

	log_debug("Apro il canale di comunicazione");
	/*** Configurazione canali server ***/
	/* Configura il canale passivo */
	S_aserfd = ch_conf(S_asport);
	(void)stato_add(S_aserfd, O_INTERNAL);
	FD_SET(S_aserfd, &S_allset);
	if (S_maxfd < S_aserfd)
		S_maxfd = S_aserfd;
	log_debug("Starting on port [%d] accepting %d clients", S_asport,
			MAXPRO_CLI);

	/* creiamo la pipe per il canale dai thread */
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

	/* Loop principale (infinito) */
	log_debug("entro in select ----------------");

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

		/* *** Gestione dei timeout *** */

		/* Scandisci la tabella delle azioni da fare in timeout */
		for (j=0; S_actions[j].f != NULL; j++) {
			t_mclock tnow = mtimes();

			if (tnow > S_actions[j].tout) {

				/* N.B: si presuppone che il tempo di servizio dell'azione
				 * sia INFERIORE al tempo di rischedulazione. Altrimenti
				 * l'azione puo' andare in starvation.
				 */
				S_actions[j].f();

				/* Puo' capitare che come effetto dell'azione, uno o
				 * piu' canali vengano chiusi. Occorre fare una verifica
				 * a livello di select().
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

				/* calcola la prossima scadenza di timeout */
				S_actions[j].tout = tnow + (t_mclock)S_actions[j].delay;
			}
		}

		/* Uscito per timeout: rientra in poll */
		if (nready == 0)
			continue;

		/* uscito per evento regolare */
		log_debug( "risveglio da select ------------");

		/* controlla tutti i canali */

		/* canale passivo del server --------------------------------------- */
		if (FD_ISSET(S_aserfd, &rset)) {
			/* Richiesta di connessione */

			struct sockaddr_in cliaddr;
			socklen_t clilen;

			clilen = sizeof(cliaddr);
			s = accept(S_aserfd, (struct sockaddr *)&cliaddr, &clilen);
			if (s < 0)
				log_fatal("accept: %s", strerror(errno));

			if (stato_add(s, 0) < 0) {
				/* Non ce ne sono */
				log_debug("*** NEW connection rejected: "
												"too many (%d) clients", j);
				close(s);
			}
			else {
				/* Trovato */
				if (s > S_maxfd)
					S_maxfd = s;
				log_debug("NEW connection accepted "
										"[%d/%d]: fd=%d", j, MAXPRO_CLI, s);

				FD_SET(s, &S_allset);
			}

			/* Manda il banner */
			nb = makeBanner(cmd, s);
			if (nb > 0){
				write2client(s, cmd, nb);
			}else
				log_error("Banner non inviato");
			if (--nready <=0)
				continue;
		}

		/* Clienti --------------------------------------------------------- */
		iter = 0;
		while ((p_stato = stato_iter(&iter)) != NULL) {
			s = p_stato->fd;
			if (s < 0)
				continue;

			if (FD_ISSET(s, &rset)) {
				/* Leggi il chunk */
				nb = Read(s, chunk, sizeof(chunk));
				if (nb <= 0) {
					/* Hangup da cliente */
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

					log_debug( "letti %d bytes", nb);
					if (isdebug()) {
						printf("Ricevuti i segg. bytes:\n");
						log_dump(chunk, nb);
					}

					/* Assembla un comando (cmd) completo */
					p = chunk;
					while ((nb = abcr(p_stato, p, nb, cmd, sizeof(cmd))) > 0) {
						/* e' arrivato un mesaggio sulla main pipe ? */
						if (s == S_pipefd) {
							strncpy(stmp, cmd, CLIENTHEADER);
							stmp[CLIENTHEADER] = '\0';
							sscanf(stmp, "%x", (unsigned int *)&clientFd);

							/* nel frattempo il cliente potrebbe essere morto */
							t_stato *p_cli = stato_getbyfd(clientFd);
							if (p_cli != NULL) {
								/* Manda la risposta */
								if (write2client(clientFd, cmd+CLIENTHEADER,
														nb-CLIENTHEADER) < 0) {

									FD_CLR(clientFd, &S_allset);
									stato_close(p_cli);
								}
							}
							else {
								/* Consuma silenziosamente */
								;
							}
						}
						else {
							/* E` un cliente ordinario: esegui il comando */
							char buffer[MAXPRO_LCMD];
							nb = elaboraCmd(p_stato, buffer, cmd, nb);

							// Se richiesto, logga il comando ricevuto
							log_warning("[%s]:\"%s\"",
											state_str(get_state()), cmd);

							if (nb == NEED_TO_CLOSE) {
								(void)write2client(s, buffer, strlen(buffer));
								FD_CLR(p_stato->fd, &S_allset);
								stato_close(p_stato);
							}
							else {
								/* Manda la risposta */
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
						/* Errore di protocollo, si chiude il canale */
						stato_close(p_stato);
						log_warning("protocol error");
					}
					else if (nb == 0) {
						/* Dati accettati, ma il pacchetto non e' completo */
						log_debug( "abcr ritorna 0");
					}
				}

				/* Aggiorna il tempo di timeout */
				if (p_stato->fd >= 0)
					p_stato->last_t = mtimes();
			}

			if (--nready <= 0 )
				continue;
		}

		log_debug( "rientro in poll ----------------------");
	}
	return 0;
}

/*****************************************************************************/

/**
 * Invia un messaggio al cliente identificato da fd.
 * Ritorna il numero di caratteri scritti per successo, -1+errno per fallimento.
 */
ssize_t write2client(int fd, const char *answer, size_t nb)
{
	ssize_t ret = 0;
	char buffer[MAXPRO_LCMD];
	size_t len;

	//log_info("ANS: %s", answer);	
	if ((nb > 0)&&(nb<=1024)){
		/* Imbusta la risposta */
		len = cook(buffer, answer, nb);

		/* Manda la risposta */
		ret = Writen(fd, buffer, len);
		if (ret < 0)
			log_warning("write2client(): %s", strerror(errno));
		else
			log_debug("scritti %d bytes", ret);
	}else{
		ret = (-1);
		log_error("wrong message length: %d", nb);
	}

	return ret;
}

/*****************************************************************************/

/**
 * DESCRIZIONE
 * 	Invia un messaggio al main thread (server). Ad uso dei thread comandi.
 * 	La funzione e` sincronizzata.
 *
 * VALORI DI RITORNO
 * 	Ritorna il numero di caratteri scritti per successo, -1+errno per fallimento.
 */
static pthread_mutex_t S_writeMutex = PTHREAD_MUTEX_INITIALIZER;

ssize_t write2main(const char *answer, size_t nb, size_t fd)
{
	ssize_t ret;
	char buffer[MAXPRO_LCMD+CLIENTHEADER];

	pthread_mutex_lock(&S_writeMutex);

	sprintf(buffer, "%0*lX", CLIENTHEADER, fd);
	nb = cook(buffer+CLIENTHEADER, answer, nb); 	/* Imbusta la risposta */

	/* Manda la risposta */
	ret = Writen(S_tomainfd, buffer, nb+CLIENTHEADER);

	if (ret < 0)
		log_warning("write2main(): %s", strerror(errno));
	else
		log_debug("write2main %d bytes", nb);

	pthread_mutex_unlock(&S_writeMutex);

	return ret;
}

/**
 * Invia un messaggio al main thread (server), destinato a TUTTI i clienti.
 * Ritorna il numero di caratteri scritti per successo, -1+errno per fallimento.
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
 * DESCRIZIONE
 * 	Lettura dello stato corrente.
 *
 * VALORI DI RITORNO
 * 	Lo stato corrente
 *
 * NOTE
 * 	La funzione e` sincronizzata.
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
 * DESCRIZIONE
 * 	Impostazione dello stato corrente.
 *
 * VALORI DI RITORNO
 * 	Lo stato precedente.
 *
 * NOTE
 * 	La funzione e` sincronizzata.
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
 * Decodifica stati server.
 */
const char * state_str(enum e_state s)
{
	switch (s) {
	case e_init:		/* Stato iniziale */
		return E_INIT;
		break;
	case e_ready:		/* Stato "pronto" */
		return E_READY;
		break;
	case e_blank:		/* Stato calcolabile: non dovrebbe comparire mai */
		return E_BLANK;
		break;
	case e_idle:
	default:		/* Macchina bloccata */
		return E_IDLE;
		break;
	}
	return "????";
}



/**
 * DESCRIZIONE
 * 	helper per gestire il communication error
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
 * DESCRIZIONE
 * 	Thread che valuta lo stato.
 *
 * VALORI DI RITORNO
 * 	Nessuno: non ritorna mai.

 */
void * eval_state(void *par)
{
	char buffer[MAXPRO_LCMD];
	size_t nb;
	int sampling_period_ms = 100;	// periodo di campionamento stato/allarmi
	enum e_state old_s = get_state();

	(void)par;

	while (1) {
		enum e_state s = get_state();

		if (s != e_idle) {
			/* Calcolo degli stati **************************************** */
			if (s == e_blank) {

			}
			else {
				s = e_ready;
				sampling_period_ms = 2;
				// Imposta lo stato calcolato
				(void)set_state(s);
			}

		}
		else /* s == e_idle */
			sampling_period_ms = 100;

		// Segnala le variazioni a tutti i clienti collegati
		if (s != old_s) {
			nb = stateVar(buffer, old_s, s);
			write2mainAll(buffer, nb);

			if (s == e_idle)
				log_error("Server is idle");

			old_s = s;
		}

		(void)set_state(s);

		// attendi per il tempo impostato
		msleep(sampling_period_ms);
	}

	/* Ritorna comunque un valore */
	return NULL;
}


/******************************************************************************/

/**
 *	Gestore generale delle signal.
 */
static void sig_trap(int sig /** Come da ANSI */)
{
	log_debug("signal %d caught: exiting...", sig);
	exit(1);
}

/******************************************************************************/

/**
 *	Gestore della SIGPIPE.
 */
static void sig_pipe(int sig /** Come da ANSI */)
{
	/* probabilmente e` inutile */
	log_debug("SIGPIPE (%d) caught: ignored", sig);
}

/******************************************************************************/

/**
 *	Gestore della SIGCHLD
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
	/* Non resettare la PGA; c'e` un conflitto con distruttore del C++ */
	log_info("*** program terminated");
}

/******************************************************************************/

/**
 *	Verifica lo scadere del timeout di disconnessione per inattivita'
 *	sul canale.
 */
static void f_cleanup(void)
{
	int state=0;
	t_stato *p_stato;

	while ((p_stato = stato_iter(&state)) != NULL) {
		if (p_stato->fd >= 0 && !btst(p_stato->options, O_INTERNAL)) {
			/* Verifica se non c'e' attivita' da piu di tot secondi */
			if (p_stato->last_t + S_conn_tout < mtimes()) {
				/* Timeout, devi sbaraccare */
				bset(p_stato->options, O_MUST_CLOSE);
				/* Il chiamante si preoccupera' di mettere a posto i
				 * bit dei descrittori della select.
				 */
				log_warning("Connection timeout (fd=%d)", p_stato->fd);
			}
		}
	}
}


