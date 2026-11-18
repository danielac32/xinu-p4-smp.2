/**
 * @file mailboxReceive.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <interrupt.h>
#include <mailbox.h>

/**
 * @ingroup mailbox
 *
 * Recibe un mensaje del buzon indicado.
 */
syscall mailboxReceive(mailbox box)
{
    struct mbox *mbxptr;
    irqmask im;
    int retval;

    if (!(0 <= box && box < NMAILBOX))
    {
        return SYSERR;
    }

    mbxptr = &mboxtab[box];

    /* esperar hasta que haya un mensaje disponible */
    if (SYSERR == wait(mbxptr->receiver))
    {
        return SYSERR;
    }

    im = disable();
    spin_acquire(&mbxptr->lock);
    if (MAILBOX_ALLOC != mbxptr->state)
    {
        spin_release(&mbxptr->lock);
        restore(im);
        signal(mbxptr->receiver);
        return SYSERR;
    }

    /* extraer el primer mensaje de la cola */
    retval = mbxptr->msgs[mbxptr->start];
    mbxptr->start = (mbxptr->start + 1) % mbxptr->max;
    mbxptr->count--;
    spin_release(&mbxptr->lock);
    restore(im);

    /* avisar que hay un espacio libre */
    signal(mbxptr->sender);
    return retval;
}
