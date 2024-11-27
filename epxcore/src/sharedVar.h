/**
 * sharedVar.h - Header for the threasd shared variables.
 *
 * (C) bertac64
 *
 */
#ifndef SHAREDVAR_H_
#define SHAREDVAR_H_

/* Struttura che contiene tutte le variabili condivise del server */
typedef struct {
	/* stato del server */
	enum e_state state;
} t_sharedVar;

/* Prototype */
void initSharedVar();
t_sharedVar *acquireSharedVar();
void releaseSharedVar(t_sharedVar **p_sharedVar);

#endif /*SHAREDVAR_H_*/
