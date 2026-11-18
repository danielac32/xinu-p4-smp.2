/**
 * @file mailboxSend.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <stddef.h>
#include <interrupt.h>
#include <mailbox.h>

/**
 * @ingroup mailbox
 *
 * Envia un mensaje al buzon indicado.
 */
syscall mailboxSend(mailbox box, int mailmsg)
{
    struct mbox *mbxptr;
    irqmask im;

    if (!(0 <= box && box < NMAILBOX))
    {
        return SYSERR;
    }

    mbxptr = &mboxtab[box];

    /* reservar espacio (puede bloquear sin mantener el spinlock) */
    if (SYSERR == wait(mbxptr->sender))
    {
        return SYSERR;
    }

    im = disable();
    spin_acquire(&mbxptr->lock);
    if (MAILBOX_ALLOC != mbxptr->state)
    {
        spin_release(&mbxptr->lock);
        restore(im);
        signal(mbxptr->sender);
        return SYSERR;
    }

    /* escribir mensaje en la cola */
    mbxptr->msgs[((mbxptr->start + mbxptr->count) % mbxptr->max)] = mailmsg;
    mbxptr->count++;
    spin_release(&mbxptr->lock);
    restore(im);

    /* avisar que hay un mensaje disponible */
    signal(mbxptr->receiver);
    return OK;
}
