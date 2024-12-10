// $Id: log.c bertac64 $

#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

#include <sys/time.h>

#include "log.h"

static int s_enablesys=0;
static char * s_progname;

struct t_log_out
{
	FILE * file;
	enum t_log_level level;
};

// management of outputs map
static struct t_log_out * s_out;
static size_t s_out_size=0;
static size_t s_out_afterlast=0;
static size_t s_out_firstnull=0;


static int s_min(int x, int y)
{
	return x<y ? x : y;
}

static int s_max(int x, int y)
{
	return x>y ? x : y;
}

void s_timestamp(char * buffer, size_t z)
{

	struct timeval tv;
	gettimeofday(&tv,0);
	struct tm stm;
	time_t tvsec = tv.tv_sec;
	localtime_r(&tvsec,&stm);

	char str1[64] = "\0";
	strftime(str1,64,"%FT%T",&stm);

	char str2[8] = "\0";
	strftime(str2,8,"%z",&stm);

	char x[16] ="\0";
	sprintf(x, "%03d", (int)(tv.tv_usec/1000));

	snprintf(buffer, z, "%s.%s%s", str1, x, str2);

}

static const char * s_stringlev(enum t_log_level w)
{
	switch(w)
	{
		case e_log_nil: return "NIL";
		case e_log_error: return "ERR";
		case e_log_warning: return "WRN";
		case e_log_info: return "INF";
		case e_log_debug: return "DBG";

		default: return "???";
	}
}

static void s_printout(enum t_log_level w, const char * message, va_list ap)
{
	const char * eee = s_stringlev(w);

	const size_t ts_size = 256;
	char ts[ts_size];
	s_timestamp(ts, ts_size);

	const size_t buf_size = strlen(message) + strlen(ts) + 256;
	char buf[buf_size];

	void * th = (void*)pthread_self();
	if (th != NULL)
		snprintf(buf, buf_size, "%s @%p %s %s \n", ts, (void*)pthread_self(), eee, message);
	else
		snprintf(buf, buf_size, "%s %s %s \n", ts, eee, message);

	const size_t z = 1024; //TODO max dimension of the log message
	char str[z];
	vsnprintf(str, z, buf, ap);

	for (size_t j=0; j < s_out_afterlast; ++j)
	{
		if ((s_out[j].file != NULL) && (w <= s_out[j].level))
		{
			fputs(str, s_out[j].file);
			fflush(s_out[j].file);
		}
	}
}



void log_init(const char * progname)
{
	assert(progname != NULL);
	assert(s_progname == NULL); //check if not initialized

	s_enablesys = 0;

	s_out_size = 16;
	s_out = calloc(s_out_size, sizeof(struct t_log_out));
	s_out_afterlast = 0;
	s_out_firstnull = 0;

	s_progname = strdup(progname);

	log_enablesys(0);

#ifdef NDEBUG
	log_addout(stderr, e_log_info);
#else
	log_addout(stderr, e_log_debug);
#endif
}


void log_term()
{
	assert(s_progname != NULL);
	log_enablesys(0);
	free((void*)s_progname);
	s_progname = NULL;
	free(s_out);
	s_out = NULL;
}


void log_it(enum t_log_level level, const char * str, ...)
{
	va_list ap;
	va_start(ap, str);
	log_itv(level, str, ap);
	va_end(ap);
}


void log_itv(enum t_log_level level, const char * str, va_list ap)
{
	assert(s_progname != NULL);

	va_list ap2;
	va_copy(ap2, ap);
	s_printout(level, str, ap2);
	va_end(ap2);

	#ifdef NDEBUG
	if (level >= e_log_debug) return;
	#endif

	if(s_enablesys)
	{
		const char * eee = s_stringlev(level);
		const size_t buf_size = strlen(str) + 256;
		char buf[buf_size];

		void * th = (void*)pthread_self();
		if (th != NULL)
			snprintf(buf, buf_size, "@%p %s %s \n", (void*)pthread_self(), eee, str);
		else
			snprintf(buf, buf_size, "%s %s \n", eee, str);

		int w = -1;
		switch(level)
		{
			case e_log_nil: break;
			case e_log_error: w = LOG_ERR; break;
			case e_log_warning: w = LOG_WARNING; break;
			case e_log_info: w = LOG_INFO; break;
			case e_log_debug: w = LOG_DEBUG; break;
			default: assert(0);
		}

		if (w >= 0)
			vsyslog(w, buf, ap);
	}
}



void log_error(const char * str, ...)
{
	va_list ap;
	assert(s_progname != NULL);
	va_start(ap, str);
	log_itv(e_log_error, str, ap);
	va_end(ap);
}


void log_warning(const char * str, ...)
{
	va_list ap;
	assert(s_progname != NULL);
	va_start(ap, str);
	log_itv(e_log_warning, str, ap);
	va_end(ap);
}


void log_info(const char * str, ...)
{
	va_list ap;
	assert(s_progname != NULL);
	va_start(ap, str);
	log_itv(e_log_info, str, ap);
	va_end(ap);
}


void log_debug(const char * str, ...)
{
	va_list ap;
	assert(s_progname != NULL);
	va_start(ap, str);
	log_itv(e_log_debug, str, ap);
	va_end(ap);
}


void log_fatal(const char * str, ...)
{
	va_list ap;
	assert(s_progname != NULL);
	va_start(ap, str);
	const size_t buf_size = strlen(str) + 256;
	char buf[buf_size];
	snprintf(buf, buf_size, "FATAL: %s", str);
	vfprintf(stderr, buf, ap);
	va_end(ap);
	abort(); // deve morire
}


void log_perror(const char * prefix)
{
	log_error("%s: %s", prefix, strerror(errno));
}


int log_addout(FILE * f, enum t_log_level w)
{
	assert(s_progname != NULL);
	assert(f != NULL);

	for (size_t j=0; j < s_out_afterlast; ++j)
	{
		if (s_out[j].file == f)
		{
			s_out[j].level = s_max(s_out[j].level, w);
			return 0;
		}
	}

	if (s_out_firstnull >= s_out_size)
	{
		const int k = 2;
		s_out = realloc(s_out, k * s_out_size * sizeof(struct t_log_out));

		for (size_t j = s_out_size; j < k*s_out_size; ++j)
		{
			s_out[j].file = NULL;
			s_out[j].level = e_log_nil;
		}
		s_out_size *= k;
	}

	s_out[s_out_firstnull].file = f;
	s_out[s_out_firstnull].level = w;

	while (s_out_firstnull < s_out_size)
	{
		if (s_out[s_out_firstnull].file == NULL) break;
		++s_out_firstnull;
	}

	s_out_afterlast = s_max(s_out_afterlast, s_out_firstnull);
	return 1;
}


int log_delout(FILE * f)
{
	assert(s_progname != NULL);
	assert(f != NULL);


	for (size_t j=0; j < s_out_afterlast; ++j)
	{
		if (s_out[j].file == f)
		{
			s_out[j].file = NULL;
			s_out[j].level = e_log_nil;
			s_out_firstnull = s_min(s_out_firstnull, j);
			if (j+1 == s_out_afterlast)
				--s_out_afterlast;
			return 1;
		}
	}

	return 0;
}

void log_enablesys(int x)
{
	if(s_enablesys)

		closelog();

	if(x)
		openlog(s_progname, LOG_PID|LOG_CONS, LOG_USER);

	s_enablesys = (x!=0);
}

/********************************************************************************/

/**
* DESCRIPTION
* Dumps (ascii or hex) a buffer.
* RETURN VALUES
* None.
*/
void log_dump(const char *buf,    	/** Dump Buffer */
         	  size_t l    	    	/** Buffer length */)
{
    char s[256] = "\0", stmp[256] = "\0", *ps = 0, *pw = 0;
    unsigned int j=0, i=0;

    for(j = 0;  j < l;  j += 16) {
        memset(s, ' ', 80);
        sprintf(stmp, "%04x", (unsigned int) j);
        memcpy(s, stmp, strlen(stmp));
        ps = s + 8;
        pw = s + 63;
        for (i = 0;  i < 16  &&  j+i < l;  i++) {
            sprintf(stmp, "%02x", buf[j+i] & 0xff);
            memcpy(ps+i*3, stmp, 2);
            if (isprint((unsigned char)buf[j+i]))
                *(pw+i) = buf[j+i];
            else *(pw+i) = '.';
        }
        printf("%79.79s\n", s);
    }
}
