/**
 * @file mailbox.h
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#ifndef _MAILBOX_H_
#define _MAILBOX_H_

#include <semaphore.h>
#include <stddef.h>
#include <conf.h>
#include <spinlock.h>

#define MAILBOX_FREE     0
#define MAILBOX_ALLOC    1

/**
 * Define la entrada de la tabla de buzones.
 */
struct mbox
{
    semaphore sender;           /**< espacios libres en el buzon        */
    semaphore receiver;         /**< mensajes listos para recibir       */
    uint max;                   /**< maximo de mensajes                 */
    uint count;                 /**< mensajes actuales en el buzon      */
    uint start;                 /**< indice del primer mensaje          */
    uchar state;                /**< estado del buzon                   */
    int *msgs;                  /**< cola de mensajes                   */
    spinlock_t lock;            /**< exclusion entre nucleos            */
};

typedef uint mailbox;

extern semaphore mboxtabsem;

extern struct mbox mboxtab[];

/* Mailbox function prototypes */
syscall mailboxAlloc(uint);
syscall mailboxCount(mailbox);
syscall mailboxFree(mailbox);
syscall mailboxInit(void);
syscall mailboxReceive(mailbox);
syscall mailboxSend(mailbox, int);

#endif                          /* _MAILBOX_H_ */
