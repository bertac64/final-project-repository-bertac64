// $Id: libep.h bertac64 $

#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

#define MAXBUF		4096		/* max dimension of tx/rx buffer */

#define EQUAL_F(a,b)	(fabs((a)-(b)) < 1e-6)

typedef struct PROTO_CONN {
	unsigned int status;		/* Status (cfr. S_*) */
	size_t nb;					/* number of data reveived on the channel */
	unsigned char b[MAXBUF];	/* Receiving Buffer */
} t_proto;


// configuration map
typedef struct PM_MAP {
	char *key;
	char *val;
} t_map;

/* prototypes for util.c */
char *strmem(const char *str);
int file_exists(const char * filename);
int file_size(const char *filename, off_t *size);
ssize_t proto_get(t_proto *p, char *buffer, size_t nbuffer);
ssize_t proto_add(t_proto *p, const void *chunk, size_t nchunk);

t_map *	map_init(void);
void	map_dispose(t_map *p);
int		map_get(t_map *pc, const char *key, char *val, size_t maxval);
int		map_set(t_map *pc, const char *key, const char *val);
t_map *	map_next(t_map *pc, int *status);
const char *map_getS(t_map *pm, const char *key, const char *dflt);
int map_getN(t_map *pm, const char *key, int dflt);

void test_tokens(char *dest, const char *src, const char *dict);
char * translateMessage( t_map *p_map, const char *from_server,
						char *dest_buffer, size_t dest_size,
						int *warning_level,	int *overwrite, int *errorcode);
char * translateInternalMsg(t_map *p_map, int errcod,
							const char *fields,
							char *dest_buffer, size_t dest_size	);
const char * get_cputype(char *buf);

#endif /*UTIL_H_*/
