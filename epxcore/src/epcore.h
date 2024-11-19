/**
 * epcore.h -	header principale del server; definisce costanti, tipi e
 * 				funzioni di uso generale.
 *
 * (C) 2008 GGH Srl per Igea SpA
 *
 */

#ifndef EPCORE_H_
#define EPCORE_H_

#include "driver.h"

#define GENEDRIVE
/* Definire per mettere il programma in modo simulazione*/
#if 0
#define SIMULAZIONE
#endif
/* Definire per mettere il programma in modalita` IGEA DEMO;
 N.B: va definita assieme a SIMULAZIONE*/
#if 0
#define DEMO_MODE
#endif
/* Definire per abilitare la gestione manuale del done->ready
 se non definit, funziona in modo tradizionale, con il
 passaggio automatico da done a ready alla conclusione
 del comando get*/
#define DONE_TO_READY

// Definire per aprire i safety relays solo in carica
#if 0
#define RELAYS_ALWAYS_CLOSED
#endif

#define PGM_VERS		"1.4.2"
#define PRGNAME			"z-epcore"
#define	CMD_MD5SUM		"/usr/bin/md5sum"
#define	SRV_PORT		6969

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

#ifdef GENEDRIVE
#define AMPMH	2.0		/*Amplificazione misura corrente per HV in GeneDrive*/
#define AMPML	8.0		/*Amplificazione misura corrente per LV in GeneDrive*/
#else
#define AMPMH	1.0		/*Amplificazione misura corrente per HV in GeneDrive*/
#define AMPML	4.0		/*Amplificazione misura corrente per LV in GeneDrive*/
#endif

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
    uint32_t data[DATA_BUFFER_SIZE];	/* Buffer write */
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
		e_test,		/* Self-test in corso */
#define E_TEST		"test"
		e_blank,	/* Stato calcolabile */
#define E_BLANK		"blank"
		e_chrgn,	/* In caricamento */
#define E_CHRGN		"chrgn"
//		e_rfid,		/* Discovery RFID */
//#define E_RFID		"rfid"
		e_disch,	/* In scarica */
#define	E_DISCH		"disch"
		e_ready,	/* Stato "pronto" */
#define E_READY		"ready"
		e_armed,	/* Messaggio asincrono */
#define E_ARMED		"armed"
		e_wtreat,	/* IN attesa di trattamento */
#define E_WTREAT	"wtreat"
		e_trtmt,	/* In trattamento */
#define E_TRTMT		"trtmt"
		e_done,		/* Trattamento completato */
#define E_DONE		"done"
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

/* Modi ECG */
#define ECG_READY	200
#define ECG_NOISY	201
#define ECG_LOST	202
#define ECG_BAD		203
#define ECG_NOISY2	204

/* Formattazione messaggi */
#define PREFIXCMDSYNC	'+'
#define PREFIXCMDASYNC	'*'
#define PREFIXMSGASYNC	'!'

/*********************************************************************/
/* Chiavi per il file di properties */

#define KEY_SYS_ERR			"sys.err"
#define KEY_SYS_IMQ			"sys.imq"
#define KEY_SYS_QUIET		"sys.quiet"
#define KEY_SYS_LOGCOMMANDS	"sys.logcommands"
#define KEY_SYS_FMON		"sys.fmon"
#define KEY_SYS_TIDLE		"sys.tidle"
#define KEY_SYS_DISCH_IDLE	"sys.disch_idle"
#define KEY_SYS_CH_IDLE		"sys.ch_idle"
#define KEY_SYS_COMP_LEN	"sys.comp_len"
#define KEY_SYS_LOG_FPGA	"sys.log_fpga"
#define KEY_FPGA_CALIB		"fpga.calib"
#define KEY_FPGA_VERSION	"fpga.version"
#define KEY_BITE_BANNED		"bite.banned"
#define KEY_CHARGE_TIMEOUT	"charge.timeout"
#define KEY_RFID_SIMULATE	"rfid.simulate"
#define KEY_RFID_SERIAL		"rfid.serial"
#define KEY_RFID_PARAMS		"rfid.params"
#define KEY_RFID_BITETAG	"rfid.bitetag"
#define KEY_RFID_REPOSITORY	"rfid.repository"
#define KEY_RFID_EVALIDITY	"rfid.evalidity"
#define KEY_RFID_FWVERSION	"rfid.fwversion"
#define KEY_RFID_MAXWAIT	"rfid.maxwait"
#define KEY_RFID_ALWAYSON	"rfid.alwayson"
#define KEY_RFID_DONTBURN	"rfid.dontburn"
#define KEY_RFID_MUSTLOCK	"rfid.mustlock"
#define KEY_RFID_BITE_FATAL	"rfid.bite_fatal"
#define KEY_EL_HEXGETLIST	"el.hexgetlist"
#define KEY_RFID_CUSTOM     "rfid.custom"
#define KEY_SYS_CUSTOM		"./custom.properties"
#define KEY_DEVICE_SERIAL   "device.serial"

/*********************************************************************/
/* Strutture di configurazione */

typedef struct EPCONF {

	/* Da file */
	float mFinal;	// mFinal
	float qFinal;	// qFinal
	float mHope;	// mHope
	float qHope;	// qHope
	float mVolt;	// mVolt
	float qVolt;	// qVolt
	float mAmp;		// mAmp
	float qAmp;		// qAmp
	float mLFinal;	// mLFinal
	float qLFinal;	// qLFinal
	float mLHope;	// mLHope
	float qLHope;	// qLHope
	float mLVolt;	// mLVolt
	float qLVolt;	// qLVolt
	float mLAmp;	// mLAmp
	float qLAmp;	// qLAmp
	float mVMeas;	// mVMeas
	float qVMeas;	// qVMeas
	float mCMeas;	// mCMeas
	float qCMeas;	// qCMeas

	/* Interna */
	int32_t iHasEGT;	// has EGT (LV) module
} t_epconf;

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
extern t_epconf G_epconf;
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
void charge_timer_start(int32_t offset);
void charge_timer_stop(void);
int32_t charge_timer_isstop(void);
int32_t charge_timer_elapsed(void);
void make_beeps(int32_t n, int32_t shorter);

#endif /*EPCORE_H_*/
