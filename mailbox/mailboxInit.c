/**
 * @file mailboxInit.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <mailbox.h>

struct mbox mboxtab[NMAILBOX];
semaphore mboxtabsem;

/**
 * @ingroup mailbox
 *
 * Inicializa las estructuras de buzones.
 */
syscall mailboxInit(void)
{
    uint i;

    /* marcar todos los buzones como libres */
    for (i = 0; i < NMAILBOX; i++)
    {
        mboxtab[i].state = MAILBOX_FREE;
        mboxtab[i].lock.lock = 0;
        mboxtab[i].msgs = NULL;
        mboxtab[i].count = 0;
        mboxtab[i].start = 0;
        mboxtab[i].max = 0;
        mboxtab[i].sender = 0;
        mboxtab[i].receiver = 0;
    }

    mboxtabsem = semcreate(1);
    if (SYSERR == mboxtabsem)
    {
        return SYSERR;
    }

    return OK;
}
