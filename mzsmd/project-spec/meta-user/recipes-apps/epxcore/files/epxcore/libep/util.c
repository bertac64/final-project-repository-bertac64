// $Id: util.c bertac64 $

// Undefine to use syscall uname
// #define UNAME2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
//#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef UNAME2
#include <sys/utsname.h>
#endif

#include "libep.h"

#define PROT_EOL		"\r\n"	/* EOL for the control protocol */

char *strmem(const char *str)
{
	if( str == NULL )
		return NULL;

	char *p = malloc(strlen(str) + 1);
	strcpy(p, str);
	return p;
}

// Check if a file exist
int file_exists(const char *filename)
{
	FILE *fp;

    if ((fp = fopen(filename, "r")))
    {
        fclose(fp);
        return 1;
    }
    return 0;
}

// Get a file size
int file_size(const char *filename, off_t *size)
{
	struct stat buf;
	int ret = stat(filename, &buf);
	if (ret < 0)
		return -1;

	if (size != NULL)
		*size = buf.st_size;
    return 0;
}

/******************************************************************************/
/**
 * DESCRIPTION
* Ascii protocol get: retrieves a complete protocol line from the
* internal buffer.
* Works with proto_add (cf.)
*
* RETURN VALUES
* Number of characters read; 0 if there is no complete buffer;
* -1 for error.
 */
ssize_t proto_get(t_proto *p, char *buffer, size_t nbuffer)
{
	unsigned char *t;
	size_t nb;

	t = (unsigned char *)strstr((char *)p->b, PROT_EOL);
	if (t == NULL)
		return 0;

	nb = (t - p->b) + strlen(PROT_EOL);
	if (nb > nbuffer)
		return -1;
	if (nb > p->nb)
		return -1;

	memcpy(buffer, p->b, nb);
	buffer[nb] = 0;		/* E` ASCII */
	p->nb -= nb;

	memmove(p->b, &(p->b[nb]), p->nb);
	p->b[p->nb] = 0;	/* E` ASCII */

	return (ssize_t)nb;
}

/**
* DESCRIPTION
* Ascii protocol add: adds a chunk to the internal protocol buffer.
* * Works with proto_get (cf.)
*
* RETURN VALUES
* Number of characters added; -1 for error.
*/
ssize_t proto_add(t_proto *p, const void *chunk, size_t nchunk)
{
	if (p->nb + nchunk >=  sizeof(p->b))
		return -1;

	memcpy(&(p->b[p->nb]), chunk, nchunk);
	p->nb += nchunk;
	p->b[p->nb] = 0;	/* E` ASCII */

	return nchunk;
}

/******************************************************************************/

/*
 * MAP
 */
#define MAXMAP	1024

/******************************************************************************/

/* Inizializza la mappa */
t_map * map_init(void)
{
	const size_t size = sizeof(t_map) * MAXMAP;
	t_map *p = malloc(size);
	if (p != NULL)
		memset(p, 0, size);
	return p;
}

// elimina la hash
void map_dispose(t_map *pm)
{
	t_map *q;
	if (pm != NULL) {
		for (q = pm; q->key != NULL; q++) {
			free(q->key);
			free(q->val);
		}

		free(pm);
	}
}

// Add an item to the map (internal usage)
static int map_iadd(t_map *pm, const char *key, const char *val)
{
	int j;

	for (j=0; j < MAXMAP && pm[j].key != NULL; j++);
	if (j >= MAXMAP)
		return -1;

	pm[j].key = strmem(key);
	pm[j].val = strmem(val);
	return 0;
}

// find a node into the hash (internal use)
static t_map * map_iget(t_map *pm, const char *key)
{
	while (pm->key != NULL) {
		if (strcasecmp(pm->key, key) == 0)
			return pm;
		pm++;
	}
	return NULL;
}

// Iterating on the hash
t_map * map_next(t_map *pm, int *status)
{
	if (pm[(*status)].key == NULL)
		return NULL;
	else
		return &(pm[(*status)++]);
}

// Find a node in the hash; return boolean (0=not there, 1=there)
// if key is there and val != NULL, return the value.
int map_get(t_map *pm, const char *key, char *val, size_t maxval)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return 0;

	if (val != NULL && maxval > 0) {
		strncpy(val, p->val, maxval);
		val[maxval-1] = 0;
	}
	return 1;
}

// Utility function: returns a pointer to the specified value or default.
// Useful to use on the fly, because it never returns NULL.
// WARNING: the pointer is part of the t_map structure: if
// the map is dynamically modified, the pointer is lost.
const char *map_getS(t_map *pm, const char *key, const char *dflt)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return dflt;

	return p->val;
}

// Utility function: returns the numeric value of the specified key or
// the default.
// Useful to use on the fly, because it never returns an error.
int map_getN(t_map *pm, const char *key, int dflt)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return dflt;

	return atoi(p->val);
}

// update/add a node to the hash; returns 0 for success, !0 otherwise.
int map_set(t_map *pm, const char *key, const char *val)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return map_iadd(pm, key, val);
	else {
		free(p->val);
		p->val = strmem(val);
	}
	return 0;
}

/*****************************************************************************/
/*
* Module for converting and expanding messages from server;
*/

/*
* DESCRIPTION
* Given a string with tokens like $n=xxxx; puts them in the passed table
* if n > 0 and n <= maxlist;
*
* RETURN VALUES
* The maximum size of the table; to be passed to free_tokens() (cfr).
*/
static size_t get_tokens(const char *p,
					  char **list,
					  size_t maxlist)
{
	size_t j, n;

	/* clear the list */
	for (j = 0; j < maxlist; j++)
		list[j] = NULL;

	/* filling slots */
	n = 0;
	while ((p = strchr(p, '$')) != NULL)  {
		p++;
		j = atoi(p++);
		if (j > 0 && j <= maxlist) {
			char tmps[4096];
			if (sscanf(p, "=%[^;];", tmps) == 1)
				list[j-1] = strmem(tmps);
			n = (j > n)? j: n;
		}
		p = strchr(p, ';');
		if (p == NULL)
			break;
		p++;
	}

	return n;
}

/*
 * DESCRIPTION
 * Deallocate the greata table from get_tokens(); see for the parameters.
 */
static void free_tokens(char **list, size_t n)
{
	size_t j;

	/* free the list */
	for (j = 0; j < n; j++) {
		free(list[j]);
		list[j] = NULL;
	}
}

/*
* DESCRIPTION
* Given a string with placeholders of type "$n" and a table created with get_tokens()
* (cf.), replaces occurrences of valid tokens with those found in the table.
* If a token is not found, the placeholder is left intact.
*/

void substitute_tokens(char *dest_buffer,
					   size_t dest_size,
					   const char * buffer,
					   char **list,
					   size_t n)
{
	const char *p, *q = buffer;

	(void)dest_size; // TODO

	dest_buffer[0] = 0;
	while ((p = strchr(q, '$')) != NULL) {
		char head[4096];
		memcpy(head, q, p-q);
		head[p-q] = 0;
		strcat(dest_buffer, head);

		p++;
		size_t j = (*p-'0');
		p++;

		if ((j > 0 && j <= n) && list[j-1] != NULL)
			strcat(dest_buffer, list[j-1]);
		else {
			char tmps[16];
			sprintf(tmps, "$%zd", j);
			strcat(dest_buffer, tmps);
		}

		q = p;
	}
	strcat(dest_buffer, q);
}

static int get_translation(t_map *p_map,
						   int errcod,
						   char *buffer,
						   size_t buffer_size)
{
	char key[16];
	sprintf(key, "%03d", errcod);
	if (!map_get(p_map, key, buffer, buffer_size)) {
		/* not found: find the default */
		strcpy(key, "default");
		if (!map_get(p_map, key, buffer, buffer_size)) {
			/* Not found: end */
			return 0;
		}

		/* found it: compose */
		char tmps[16];
		snprintf(tmps, sizeof(tmps), " (%03d)", errcod);
		strcat(buffer, tmps);
	}
	return 1;
}

#ifdef TEST_AND_DEBUG

void test_tokens(char *dest,
				 const char *src,
				 const char *dict)
{

	char *list[8];
	size_t j, n = get_tokens(dict, list, 8);
	for (j = 0; j < n; j++)
		printf("* list[%d] = \"%s\"\n", j, list[j]);

	substitute_tokens(dest, 256, src, list, n);
	printf("dict=\"%s\"\n", dict);
	printf("src =\"%s\"\n", src);
	printf("dest=\"%s\"\n", dest);

	free_tokens(list, n);
}

#endif /* TEST_AND_DEBUG */

/*
* DESCRIPTION
* The string starting with !Wn, -KO or *KO is passed;
* the error code is searched in the passed map; if it is found,
* the string is taken and all the tokens are replaced; if it is not found, the
* content of the key 999 (internal error (nnn)) is returned.
* The warning level is also returned (if it is a -KO string, it sets it to -1), a boolean
* to indicate whether the string should be overwritten and the error code.
* If this data is not of interest, NULL can be passed.
* RETURN VALUES
* Pointer to the filled buffer or NULL in case of error.
*/
char * translateMessage(t_map *p_map,			/* Map with translations */
						const char *from_server,/* Server Messagge */
						char *dest_buffer,		/* Destination Buffer */
						size_t dest_size,		/* Buffer Dimension */
						int *warning_level,		/* Warning Level !W (o NULL) */
						int *overwrite,			/* if it has to be overwritten (o NULL)*/
						int *errorcode			/* Error Code */)
{
	/* Sanity checks */
	if (p_map == NULL)
		return NULL;
	if (from_server == NULL || from_server[0] == 0)
		return NULL;
	if (dest_buffer == NULL || dest_size == 0)
		return NULL;

	/* in any case (!Wn,-KO, *KO) skip the first 4 */
	const char *p = &from_server[3];

	/* Test to "overwrite" */
	if (*p == '+') {
		if (overwrite != NULL)
			*overwrite = 1;
		p++;
	}
	else {
		if (overwrite != NULL)
			*overwrite = 0;
	}

	/* recover the warning level */
	if (warning_level != NULL) {
		if (sscanf(from_server, "!W%d", warning_level) != 1)
			*warning_level = -1;
	}

	/* Recover the code ot the messagge/error */
	int errcod;
	if (sscanf(p, "%d", &errcod) != 1)
		return NULL;

	if (errorcode != NULL)
		*errorcode = errcod;

	/* Translation recovery */
	char buffer[4096];
	if (!get_translation(p_map, errcod, buffer, sizeof(buffer)))
		return NULL;

	/* found it: recover the first 8 token from server messagge */
	char *list[8];
	size_t n = get_tokens(p, list, 8);

	/* Replace */
	substitute_tokens(dest_buffer, dest_size, buffer, list, n);

	/* finished */
	free_tokens(list, n);
	return dest_buffer;
}

/*
* DESCRIPTION
* An error code and a string with values ​​("$1=x; ...") are passed
* the error code is searched in the passed map; if it is found,
* the string is taken and all tokens are replaced; if it is not found,
* the content of the "default" key is returned (internal error (nnn)).
* RETURN VALUES
* Pointer to the filled buffer or NULL in case of error.
*/
char * translateInternalMsg(t_map *p_map,		/* Map with translations */
							int errcod,			/* Error Code to be found */
							const char *fields,	/* field list (can be "" or NULL) */
							char *dest_buffer,	/* Destination Buffer */
							size_t dest_size	/* Buffer Dimension */)
{
	/* Sanity checks */
	if (p_map == NULL)
		return NULL;
	if (dest_buffer == NULL || dest_size == 0)
		return NULL;

	/* Translation recovery */
	char buffer[4096];
	if (!get_translation(p_map, errcod, buffer, sizeof(buffer)))
		return NULL;

	/* found it */
	if (fields != NULL) {
		/* recover the first 8 token from the list */

		char *list[8];
		size_t n = get_tokens(fields, list, 8);

		/* replace */
		substitute_tokens(dest_buffer, dest_size, buffer, list, n);
		free_tokens(list, n);
	}
	else {
		strncpy(dest_buffer, buffer, dest_size);
		dest_buffer[dest_size-1] = 0;
	}

	/* finished */
	return dest_buffer;
}


/****************************************************************************/
/** stopwatch */

struct timeval S_tv;

/**
 * DESCRIPTION
* Starts the stopwatch.
* NOTE
* Not thread-safe
 */
void chrono_start(void)
{
	gettimeofday(&S_tv, NULL);
}

/**
 * DESCRIPTION
* stops the timer and returns the elapsed in ms.
* NOTE
* It is not thread-safe
 */
int chrono_stop(void)
{
	struct timeval tv;
	int ms;

	gettimeofday(&tv, NULL);
	ms = tv.tv_usec - S_tv.tv_usec;
	ms /= 1000;
	ms += (tv.tv_sec - S_tv.tv_sec)*1000;
	return ms;
}


/****************************************************************************/
/** Platform Identification */

/**
 *	DESCRIPTION
 * 		Checks the cpu type.
 *
 */
const char * get_cputype(char *buffer)
{
#ifdef UNAME2
	struct utsname uts;
	
	if (uname(&uts) != 0)
		return NULL;

	strcpy(buffer, uts.machine);
#else
	printf("get_cputype\n");
	FILE *fp = popen("/bin/uname -m", "r");
	if (fp == NULL) {
		printf("get_cputype can't load %s\n", strerror(errno));
		return NULL;
	}

	char z[1024];
	z[0] = 0;
	(void)fgets(z, sizeof(z), fp);
	(void)pclose(fp);

	int j;
	for (j=0; z[j] && z[j] != '\n'; j++)
		buffer[j] = z[j];
	buffer[j] = 0;
#endif
	return buffer;
}
