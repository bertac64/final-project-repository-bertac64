/**
 * util.h - Header of utility functions
 *
 * (C) bertac64
 *
 */
#ifndef UTIL_H
#define UTIL_H

void	stato_init(void);
int		stato_add(int fd, int options);
t_stato *stato_getbyfd(int fd);
t_stato *stato_iter(int *stato);
void	stato_close(t_stato *p_ch);

void	task_cancel(t_task *t);
void	task_create(t_task *t, int cnt, t_cmd *pCmd, t_stato *);

int		ch_conf(short portnum);
int		bipopen(const char *command, pid_t *ppid);
size_t cook(char *buf, const char *cmd, size_t buflen);
ssize_t abcr(t_stato *p, const char *chunk, size_t chlen,
			 char *outbuf, size_t oblen);
ssize_t Read(int fd, void *buffer, size_t nbuffer);
ssize_t Writen(int fd, const char *vptr, size_t n);
ssize_t my_readc(int fd, char *ptr);
ssize_t readline(int fd, void *vptr, size_t maxlen);
void	init_timer(void);
t_mclock mtimes(void);
void	freeTok(char **psp, int32_t nArgs);
int32_t argTok(const char *cmdStr, char **psp, int32_t maxArgs);
int		load_calib(const char *filename);
int		parse_wps(const char *wps, int *wl, char *parity, int *sb);
void	msleep(int ms);
char *	savestr(const char *str);
int		since_now(const char *date);
void	timestamp(char * buffer, size_t z);

#endif /*UTIL_H */
