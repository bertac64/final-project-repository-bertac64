/**
 * epxcore.h -	main header file of the server; defines costants, types and
 * 				general purpose functions.
 *
 * (C) 2024 Igea SpA
 *
 */

#ifndef EPXCORE_H_
#define EPXCORE_H_

#include "driver.h"

#define PGM_VERS		"1.1.0"
#define PRGNAME			"epxcore"
#define	SRV_PORT		9696
#define D64BIT

/**********************************************************************/
/* generic defines */
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
/* tasks definitions */
/**********************************************************************/

typedef struct S_TASK {
	pthread_mutex_t mutex;	/* Concurrent structure access */
	int32_t valid;				/* The POSIX threads accept as valid every th */
	pthread_t th;			/* Thread Id */
	int32_t cnt;				/* Counter progressive (rif.) of the command */
	struct S_CMD *pCmd;		/* Command generating the task */
} t_task;

/**********************************************************************/
/* Protocol definitions */
/**********************************************************************/

#define MAXPRO_CLI		10		/* N. max of clients accepted */
#define MAXPRO_LCMD		1024	/* Max length of a message */
#define	MAXPRO_TOK		100		/* N. max of token/arguments */

#define CLIENTHEADER	6		/* message header length */

/* Timer extended and anti-wrap in milliseconds (cfr. mtimes()) */
typedef uint64_t t_mclock;

typedef struct S_STATO_CONN {
    int32_t     fd;					/* Connection File descriptor */
    int32_t		options;			/* possible options (cfr. O_*) */
    t_mclock last_t;			/* Absolute duration of the last in/out operation */
    size_t	nb;					/* Number of data riceived on the channel */
    char 	b[MAXPRO_LCMD*2];	/* Receiving Buffer */
    t_task	t;					/* Associated task data */

    /* Per i comandi multipli */
    struct S_CMD *currCmd;		/* Running Command */

    /* Write */
    uint32_t count;						/* write data counter  */
    uint32_t base;						/* write base */
    uint32_t data[DATA_BUFFER_SIZE];	/* write Buffer */
} t_stato;

/* Connection management (status of clients) */
#define O_MUST_CLOSE	0x01	/* Bit for later closure */
#define O_INTERNAL		0x02	/* internal channel Bit */

typedef struct S_ACTTOUT {
    const char * name;			/* routine name*/
    t_mclock tout;				/* Timeout (absolute) for the action */
    void	(*f)(void);			/* Action to be done */
    int32_t		delay;				/* Polling Interval (in ms) */
} t_actions;

/* Stati server */
enum e_state {
		e_init,		/* Init Status */
#define E_INIT		"init"
		e_blank,	/* Intermediate Status */
#define E_BLANK		"blank"
		e_ready,	/* Ready Status */
#define E_READY		"ready"
		e_idle		/* Idle Status: device freezed. */
#define E_IDLE		"idle"
};

/* Answers Format */
#define ANSOK	"+OK"
#define ANSKO	"-KO"
#define ANSERR	"-ERR"

/* Livels of warning */
#define W_INFO	0
#define W_LOW	1
#define W_MID	2
#define W_HIGH	3

/* Messages Format */
#define PREFIXCMDSYNC	'+'
#define PREFIXCMDASYNC	'*'
#define PREFIXMSGASYNC	'!'


/*********************************************************************/

typedef struct RAM{
	ushort *g_pucTot;
	ushort *g_pucConf;
	uint offset;
	uint length;
	int fd_mem;						/* File for memory access */

	int fd_dev;
} Memory;

/*********************************************************************/

/* global variables */
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
/* prototypes */

void write2mainAll(const char *answer, size_t nb);
ssize_t write2main(const char *answer, size_t nb, size_t fd);
ssize_t write2client(int32_t fd, const char *answer, size_t nb);
enum e_state get_state(void);
enum e_state set_state(enum e_state);
const char * state_str(enum e_state s);
void * eval_state(void *);

#endif /*EPXCORE_H_*/
