/**
 * @file mailboxFree.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <mailbox.h>
#include <memory.h>
#include <interrupt.h>

/**
 * @ingroup mailbox
 *
 * Libera el buzon indicado.
 */
syscall mailboxFree(mailbox box)
{
    struct mbox *mbxptr;
    irqmask im;
    int retval;

    if (!(0 <= box && box < NMAILBOX))
    {
        return SYSERR;
    }

    mbxptr = &mboxtab[box];

    /* esperar acceso exclusivo a la tabla de buzones */
    wait(mboxtabsem);

    im = disable();
    spin_acquire(&mbxptr->lock);
    if (MAILBOX_ALLOC == mbxptr->state)
    {
        /* marcar buzon como no asignado */
        mbxptr->state = MAILBOX_FREE;
        spin_release(&mbxptr->lock);
        restore(im);

        /* liberar semaforos y memoria del buzon */
        semfree(mbxptr->sender);
        semfree(mbxptr->receiver);
        memfree(mbxptr->msgs, sizeof(int) * (mbxptr->max));
        retval = OK;
    }
    else
    {
        spin_release(&mbxptr->lock);
        restore(im);
        retval = SYSERR;
    }

    /* liberar acceso a la tabla de buzones */
    signal(mboxtabsem);

    return retval;
}
