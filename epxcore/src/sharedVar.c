/**
 * sharedVar.c - Gestione sincronizzata variabili condivise tra thread.
 *
 * (C) 2008 GGH Srl per Igea SpA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Librerie locali */
#include "../libep/libep.h"
#include "../liblog/log.h"

#include "epxcore.h"
#include "sharedVar.h"

/* static global */
static t_sharedVar S_sharedVar; 	//*< shared container. contain all shared var
static pthread_mutex_t S_sharedMutex = PTHREAD_MUTEX_INITIALIZER;	//*< mutex for shared var
static int S_useCounter; 			//*< counter. inc every acquire of mutex - (valid range is 0-1)

/**
 * DESCRIZIONE
 * 	Inizializza le variabili condivise.
  */
void initSharedVar(void)
{
	pthread_mutex_lock(&S_sharedMutex);
    S_useCounter = 0;
	S_sharedVar.state = e_init;

	pthread_mutex_unlock(&S_sharedMutex);
}

/**
 * DESCRIZIONE
 *	Acquisisce un mutex sulle variabili condivise.
 *	Ritorna un puntatore alla struttura solo dopo che e` stato acquuisito
 *	il mutex. Se il mutex e` bloccato, la funzione sospende il
 *	thread corrente.
 *
 * VALORI DI RITORNO
 * 	Puntatore a struttura condivisa.
  */
t_sharedVar *acquireSharedVar()
{
    pthread_mutex_lock(&S_sharedMutex);
	assert(S_useCounter == 0);
    S_useCounter++;
    return &S_sharedVar;
}

/**
 * DESCRIZIONE
 * 	Rilascia il mutex delle variabili condivise. Va chiamato dopo
 *	acquireSharedVar (cfr.).
 *	Il puntatore passato viene posto a NULL, come ulteriore misura
 *	di sicurezza.
 *
 * VALORI DI RITORNO
 *	Nessuno.
 */
void releaseSharedVar(t_sharedVar **p_sharedVar)
{
	/* controllo e azzero */
	assert(S_useCounter > 0);
	assert(*p_sharedVar == &S_sharedVar);
	*p_sharedVar = NULL;
	S_useCounter--;
    pthread_mutex_unlock(&S_sharedMutex);
}
