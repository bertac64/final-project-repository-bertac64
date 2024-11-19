/**
 * epxcore.h -	header principale del server; definisce costanti, tipi e
 * 				funzioni di uso generale.
 *
 * (C) 2024 Igea SpA
 *
 */

#ifndef EPXCORE_H_
#define EPXCORE_H_

#define PGM_VERS		"1.0.0"
#define PRGNAME			"epxcore"
#define	CMD_MD5SUM		"/usr/bin/md5sum"
#define	SRV_PORT		9696
#define D64BIT

/**********************************************************************/
/* definizioni generiche */
/**********************************************************************/
#define MAXSTR	256			/* dimensione generica di stringa */

/* Gestione maschere di bit */
#define bset(a, m)	((a) |= (m))
#define bclr(a, m)	((a) &= ~(m))
#define btst(a, m)	((a) & (m))

#define isdebug()	(G_flgVerbose > 3)

#define TRUE	1
#define FALSE	0

typedef unsigned char bool;
#define equal(a, b) (!strcmp(a, b))
#define min(a,b)	(((a) < (b))?(a):(b))
#define max(a,b)	(((a) > (b))?(a):(b))

/**********************************************************************/
/* definizioni per i task */
/**********************************************************************/

typedef struct S_TASK {
	pthread_mutex_t mutex;	/* Accesso concorrente alla struttura */
	int32_t valid;				/* I POSIX thread considerano valido ogni th */
	pthread_t th;			/* Id del thread */
	int32_t cnt;				/* Contatore progressivo (rif.) del comando */
	struct S_CMD *pCmd;		/* Comando che ha generato il task */
} t_task;

/**********************************************************************/
/* definizioni per il protocollo */
/**********************************************************************/

#define MAXPRO_CLI		10		/* N. max di clienti accettati */
#define MAXPRO_LCMD		1024	/* Lunghezza max di un messaggio */
#define	MAXPRO_TOK		100		/* N. max di token/argomenti */

#define ELECTRODE_VALIDITY	(6*3600)	/* Default: 6 ore */

#define CLIENTHEADER	6		/* Lunghezza della header di un messaggio */

/* Timer esteso e anti-wrap in millisecondi (cfr. mtimes()) */
typedef uint64_t t_mclock;

typedef struct S_STATO_CONN {
    int32_t     fd;					/* File descriptor della connessione */
    int32_t		options;			/* Varie opzioni (cfr. O_*) */
    t_mclock last_t;			/* Tempo assoluto ultima operazione in/out */
    size_t	nb;					/* Numero dati ricevuti sul canale */
    char 	b[MAXPRO_LCMD*2];	/* Buffer di ricezione */
    t_task	t;					/* Dati dell'eventuale task associato */

    /* Per i comandi multipli */
    struct S_CMD *currCmd;		/* Comando in esecuzione */

    /* Write */
    uint32_t count;						/* contatore dati write */
    uint32_t base;						/* base write */
	size_t nsample;					/* n. totale impulsi HV+LV */
	uint32_t p_interpulse;			/* pausa tra gli impulsi bipolari*/
} t_stato;

/* Gestione connessione (stato clienti) */
#define O_MUST_CLOSE	0x01	/* Bit per chiusura differita */
#define O_INTERNAL		0x02	/* Bit che indica che il canale e' interno */

typedef struct S_ACTTOUT {
    const char * name;			/* Nome della routine */
    t_mclock tout;				/* Timeout (assoluto) per l'azione */
    void	(*f)(void);			/* Azione da eseguire */
    int32_t		delay;				/* Intervallo di poll (in ms) */
} t_actions;

/* Stati server */
enum e_state {
		e_init,		/* Stato iniziale */
#define E_INIT		"init"
		e_blank,	/* Stato calcolabile */
#define E_BLANK		"blank"
		e_ready,	/* Stato "pronto" */
#define E_READY		"ready"
		e_idle		/* Macchina bloccata */
#define E_IDLE		"idle"
};

/* Formattazione risposte */
#define ANSOK	"+OK"
#define ANSKO	"-KO"
#define ANSERR	"-ERR"

/* Livelli di warning */
#define W_INFO	0
#define W_LOW	1
#define W_MID	2
#define W_HIGH	3

/* Formattazione messaggi */
#define PREFIXCMDSYNC	'+'
#define PREFIXCMDASYNC	'*'
#define PREFIXMSGASYNC	'!'

/*********************************************************************/
/* Chiavi per il file di properties */

#define KEY_SYS_ERR			"sys.err"
#define KEY_SYS_QUIET		"sys.quiet"
#define KEY_SYS_LOGCOMMANDS	"sys.logcommands"
#define KEY_SYS_FMON		"sys.fmon"
#define KEY_SYS_TIDLE		"sys.tidle"

/*********************************************************************/
typedef struct S_DEVICE {
	/* device */
	char serial[9];
	char code[9];
	bool is_custom;
} t_device;

typedef struct RAM{
	ushort *g_pucTot;
	ushort *g_pucConf;
	uint offset;
	uint length;
	int fd_mem;						/* File per accedere alla memoria */

	int fd_dev;
} Memory;

/*********************************************************************/

/* variabili globali */
extern int32_t G_flgVerbose;
extern int32_t G_thPipe[2];
extern int32_t G_nobite;
extern int32_t G_calibration;
extern t_map * G_map;
extern char * G_fname_map;
extern int32_t  G_usb_id;
extern int32_t  G_reset_watchdog;
extern int32_t  G_poctype;
extern bool G_bulkComm;
extern t_map * G_custom;
extern t_device G_device;
extern Memory SRAM;

/*********************************************************************/
/* prototipi */

void write2mainAll(const char *answer, size_t nb);
ssize_t write2main(const char *answer, size_t nb, size_t fd);
ssize_t write2client(int32_t fd, const char *answer, size_t nb);
enum e_state get_state(void);
enum e_state set_state(enum e_state);
const char * state_str(enum e_state s);
void * eval_state(void *);

#endif /*EPXCORE_H_*/
