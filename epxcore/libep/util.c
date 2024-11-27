// $Id: util.c 131 2010-01-19 09:03:24Z borgia $

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

#define PROT_EOL		"\r\n"	/* EOL per il protocollo di controllo */

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
 * DESCRIZIONE
 *		Ascii protocol get: recupera una linea di protocollo completa dal
 *		buffer interno.
 *		Lavora con proto_add (cfr.)
 *
 * VALORI DI RITORNO
 *		Numero di caratteri letti; 0 se non c'e' un buffer completo;
 *		-1 per errore.
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
 * DESCRIZIONE
 *		Ascii protocol add: aggiunge un chunk al buffer interno di protocollo
 *		interno.
 *		Lavora con proto_get (cfr.)
 *
 * VALORI DI RITORNO
 *		Numero di caratteri aggiunti; -1 per errore.
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
 * MAPPA
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

// Aggiunge un item alla mappa (uso interno)
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

// Trova un nodo nella hash (uso interno)
static t_map * map_iget(t_map *pm, const char *key)
{
	while (pm->key != NULL) {
		if (strcasecmp(pm->key, key) == 0)
			return pm;
		pm++;
	}
	return NULL;
}

// Itera sulla hash
t_map * map_next(t_map *pm, int *status)
{
	if (pm[(*status)].key == NULL)
		return NULL;
	else
		return &(pm[(*status)++]);
}

// Trova un nodo nella hash; torna booleano (0=non c'e`, 1=c'e`)
// se key c'e` e val != NULL, torna il valore.
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

// Funzione di utilita`: torna un puntatore al valore o al default specificato.
// Utile da usare al volo, perche' non ritorna mai NULL.
// ATTENZIONE: il puntatore e` parte della struttura di t_map: se
// la mappa viene modificata dinamicamente, il puntatore viene perso.
const char *map_getS(t_map *pm, const char *key, const char *dflt)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return dflt;

	return p->val;
}

// Funzione di utilita`: torna il valore numerico della chiave specificata o
// il default.
// Utile da usare al volo, perche' non ritorna mai errore.
int map_getN(t_map *pm, const char *key, int dflt)
{
	t_map *p = map_iget(pm, key);
	if (p == NULL)
		return dflt;

	return atoi(p->val);
}

// aggiorna/aggiunge un nodo alla hash; torna 0 per successo, !0 altrimenti.
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
 * Modulo per la conversione ed espansione dei messaggi da server;
 */

/*
 * DESCRIZIONE
 * 	Data una stringa con token del tipo $n=xxxx; li mette nella tabella passata
 * 	se n > 0 e n <= maxlist;
 *
 * VALORI DI RITORNO
 * 	La dimensione massima della tabella; da passare a free_tokens() (cfr).
 */
static size_t get_tokens(const char *p,
					  char **list,
					  size_t maxlist)
{
	size_t j, n;

	/* Azzera la lista */
	for (j = 0; j < maxlist; j++)
		list[j] = NULL;

	/* Riempi gli slot */
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
 * DESCRIZIONE
 * 	Dealloca la tabella greata da get_tokens(); cfr. per i parametri.
 */
static void free_tokens(char **list, size_t n)
{
	size_t j;

	/* rilascia la lista */
	for (j = 0; j < n; j++) {
		free(list[j]);
		list[j] = NULL;
	}
}

/*
 * DESCRIZIONE
 * 	Data una stringa con placeholders del tipo "$n" e una tabella creata con get_tokens()
 * 	(cfr.), sostituisce le occorrenze dei token validi con quelli trovati nella tabella.
 * 	Se un token non viene trovato, il placeholder viene lasciato intatto.
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
		/* Non hai trovato: cerca il default */
		strcpy(key, "default");
		if (!map_get(p_map, key, buffer, buffer_size)) {
			/* Non hai trovato: fine */
			return 0;
		}

		/* Hai trovato: componi */
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
 * DESCRIZIONE
 *	Viene passata la stringa iniziante per !Wn, -KO o *KO;
 *	viene cercato il codice di errore nella mappa passata; se viene trovato
 *	si prende la stringa e si sostituiscono tutti i token; se non viene trovato, viene
 *	tornato il contenuto della chiave 999 (internal error (nnn)).
 *	Torna anche il warning level (se e` una stringa -KO, ci mette -1), un booleano
 *	per segnalare se la stringa va sovrascritta e il codice di errore.
 *	Se questi dati non interessano, si puo` passare NULL.
 * VALORI DI RITORNO
 * 	Puntatore al buffer riempito o NULL in caso di errore.
 */
char * translateMessage(t_map *p_map,			/* Mappa con le traduzioni */
						const char *from_server,/* Messaggio da server */
						char *dest_buffer,		/* Buffer destinazione */
						size_t dest_size,		/* Dimensioni del medesimo */
						int *warning_level,		/* Livello di warning del !W (o NULL) */
						int *overwrite,			/* Se deve essere sovrascritto (o NULL)*/
						int *errorcode			/* Codice di errore */)
{
	/* Sanity checks */
	if (p_map == NULL)
		return NULL;
	if (from_server == NULL || from_server[0] == 0)
		return NULL;
	if (dest_buffer == NULL || dest_size == 0)
		return NULL;

	/* In ogni caso caso (!Wn,-KO, *KO) salta i primi 4 */
	const char *p = &from_server[3];

	/* Testa per "overwrite" */
	if (*p == '+') {
		if (overwrite != NULL)
			*overwrite = 1;
		p++;
	}
	else {
		if (overwrite != NULL)
			*overwrite = 0;
	}

	/* recupera il warning level */
	if (warning_level != NULL) {
		if (sscanf(from_server, "!W%d", warning_level) != 1)
			*warning_level = -1;
	}

	/* Recupera il codice di messaggio/errore */
	int errcod;
	if (sscanf(p, "%d", &errcod) != 1)
		return NULL;

	if (errorcode != NULL)
		*errorcode = errcod;

	/* Recupera la traduzione */
	char buffer[4096];
	if (!get_translation(p_map, errcod, buffer, sizeof(buffer)))
		return NULL;

	/* Hai trovato: recupera i primi 8 token dal messaggio del server */
	char *list[8];
	size_t n = get_tokens(p, list, 8);

	/* Sostituisci */
	substitute_tokens(dest_buffer, dest_size, buffer, list, n);

	/* finito */
	free_tokens(list, n);
	return dest_buffer;
}

/*
 * DESCRIZIONE
 *	Viene passato un codice di errore e una stringa con i valori ("$1=x; ...")
 *	viene cercato il codice di errore nella mappa passata; se viene trovato
 *	si prende la stringa e si sostituiscono tutti i token; se non viene trovato,
 *	viene tornato il contenuto della chiave "default" (internal error (nnn)).
 * VALORI DI RITORNO
 * 	Puntatore al buffer riempito o NULL in caso di errore.
 */
char * translateInternalMsg(t_map *p_map,		/* Mappa con le traduzioni */
							int errcod,			/* Codice di errore da cercare */
							const char *fields,	/* lista dei campi (can be "" or NULL) */
							char *dest_buffer,	/* Buffer destinazione */
							size_t dest_size	/* Dimensioni del medesimo */)
{
	/* Sanity checks */
	if (p_map == NULL)
		return NULL;
	if (dest_buffer == NULL || dest_size == 0)
		return NULL;

	/* recupera la traduzione */
	char buffer[4096];
	if (!get_translation(p_map, errcod, buffer, sizeof(buffer)))
		return NULL;

	/* Hai trovato */
	if (fields != NULL) {
		/* recupera i primi 8 token dalla lista */

		char *list[8];
		size_t n = get_tokens(fields, list, 8);

		/* Sostituisci */
		substitute_tokens(dest_buffer, dest_size, buffer, list, n);
		free_tokens(list, n);
	}
	else {
		strncpy(dest_buffer, buffer, dest_size);
		dest_buffer[dest_size-1] = 0;
	}

	/* finito */
	return dest_buffer;
}


/****************************************************************************/
/** Cronometro */

struct timeval S_tv;

/**
 *	DESCRIZIONE
 * 		Fa partire il cronometro.
 *	NOTE
 *		Non e` thread-safe
 */
void chrono_start(void)
{
	gettimeofday(&S_tv, NULL);
}

/**
 *	DESCRIZIONE
 * 		ferma il cronometro e torna l'elapsed in ms.
 *	NOTE
 *		Non e` thread-safe
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
/** Identificazione piattaforma */

/**
 *	DESCRIZIONE
 * 		Fa partire il cronometro.
 *	NOTE
 *		Non e` thread-safe
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
