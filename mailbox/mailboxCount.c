/**
 * @file mailboxCount.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <interrupt.h>
#include <mailbox.h>

/**
 * @ingroup mailbox
 *
 * Obtiene el numero de mensajes pendientes en el buzon.
 */
syscall mailboxCount(mailbox box)
{
    struct mbox *mbxptr;
    irqmask im;
    int retval;

    if (!(0 <= box && box < NMAILBOX))
    {
        return SYSERR;
    }

    mbxptr = &mboxtab[box];
    im = disable();
    spin_acquire(&mbxptr->lock);
    if (MAILBOX_ALLOC == mbxptr->state)
    {
        retval = mbxptr->count;
    }
    else
    {
        retval = SYSERR;
    }
    spin_release(&mbxptr->lock);
    restore(im);
    return retval;
}
