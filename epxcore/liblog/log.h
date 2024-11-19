// $Id: log.h 49 2009-03-03 17:13:27Z bobmar $

#ifndef LIBLOG_LOG_H
#define LIBLOG_LOG_H

#include <stdio.h>
#include <stdarg.h>

/*
 * priorities/facilities are encoded into a single 32-bit quantity, where the
 * bottom 3 bits are the priority (0-7) and the top 28 bits are the facility
 * (0-big number).  Both the priorities and the facilities map roughly
 * one-to-one to strings in the syslogd(8) source code.  This mapping is
 * included in this file.
 *
 * priorities (these are ordered)
 */
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

void log_init(const char * progname);
void log_term(void);

enum t_log_level
{
	e_log_nil = 0,
	e_log_error = 1,
	e_log_warning = 2,
	e_log_info = 3,
	e_log_debug = 4
};

// log a livello variabile
void log_it(enum t_log_level l, const char * s, ...);
void log_itv(enum t_log_level l, const char * s, va_list ap);

// log a livello fisso
void log_error(const char * s, ...);
void log_warning(const char * s, ...);
void log_info(const char * s, ...);
void log_debug(const char * s, ...);

// logga e muore
void log_fatal(const char * s, ...);

// log di perror a livello error
void log_perror(const char * prefix);

// abilita uso di syslog
void log_enablesys(int x);

// aggiunge un FILE* su cuil loggare fino a livello indicato
// ritorna 1 se aggiunto, 0 se modificato
int log_addout(FILE * f, enum t_log_level uptoinc);

// elimina un FILE* su cui loggare
// ritorna 1 se eliminato, 0 se non era presente
int log_delout(FILE * f);

void log_dump(const char *buffer, size_t buflen);


#endif
