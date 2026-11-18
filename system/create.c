/**
 * @file create.c
 */
/* Embedded Xinu, Copyright (C) 2007, 2013.  All rights reserved. */

#include <platform.h>
#include <string.h>
#include <thread.h>
#include <clock.h>

static int thrnew(void);

/**
 * @ingroup threads
 *
 * Create a thread to start running a procedure.
 *
 * @param procaddr
 *      procedure address
 * @param ssize
 *      stack size in bytes
 * @param priority
 *      thread priority (0 is lowest priority)
 * @param name
 *      name of the thread, used for debugging
 * @param nargs
 *      number of arguments that follow
 * @param ...
 *      arguments to pass to thread procedure
 * @return
 *      the new thread's thread id, or ::SYSERR if a new thread could not be
 *      created (not enough memory or thread entries).
 */
tid_typ create(void *procaddr, uint ssize, int priority,
               const char *name, int nargs, ...)
{
    irqmask im;                 /* saved interrupt state               */
    ulong *saddr;               /* stack address                       */
    tid_typ tid;                /* new thread ID                       */
    struct thrent *thrptr;      /* pointer to new thread control block */
    va_list ap;                 /* list of thread arguments            */

    im = disable();

    if (ssize < MINSTK)
    {
        ssize = MINSTK;
    }

    /* Allocate new stack.  */
    saddr = stkget(ssize);
    if (SYSERR == (intptr_t)saddr)
    {
        restore(im);
        return SYSERR;
    }

    /* Allocate new thread ID.  */
    tid = thrnew();
    if (SYSERR == (int)tid)
    {
        stkfree(saddr, ssize);
        restore(im);
        return SYSERR;
    }

    /* Set up thread table entry for new thread.  */
    thrcount++;
    thrptr = &thrtab[tid];

    thrptr->state = THRSUSP;
    thrptr->prio = priority;
    thrptr->stkbase = saddr;
    thrptr->stklen = ssize;
    strlcpy(thrptr->name, name, TNMLEN);
    thrptr->parent = gettid();
    thrptr->hasmsg = FALSE;
    thrptr->memlist.next = NULL;
    thrptr->memlist.length = 0;

    /* Set up default file descriptors.  */
    thrptr->fdesc[0] = CONSOLE; /* stdin  is console */
    thrptr->fdesc[1] = CONSOLE; /* stdout is console */
    thrptr->fdesc[2] = CONSOLE; /* stderr is console */

    thrptr->cpu_ticks = 0;
    thrptr->born_ticks = clktotal;
    thrptr->stk_peak = 0;
    thrptr->stk_overflow = FALSE;
    thrptr->gen++;              /* distingue este uso del slot del anterior */

    /* SMP: limpiar estado heredado de un uso anterior del slot.  Sin esto,
     * un kill remoto pendiente (stk_pad[0]) mataria a la nueva hebra en su
     * primer resched, y los campos elf_* heredados pueden liberar un bloque
     * PSRAM que pertenece a otra app. */
    thrptr->stk_pad[0] = 0;     /* kill_pending */
    thrptr->stk_pad[1] = 0;
    thrptr->stk_pad[2] = 0;
    thrptr->elf_task = FALSE;
    thrptr->elf_img = NULL;
    thrptr->elf_size = 0;

    /* SMP: afinidad (core0 por defecto) */
    thrptr->cpu_home = 0;
    thrptr->cpu_last = 0;
    thrptr->klock_depth = 0;

    /* Set up new thread's stack with context record and arguments.
     * Architecture-specific.  */
    va_start(ap, nargs);
    thrptr->stkptr = setupStack(saddr, procaddr, INITRET, nargs, ap);
    va_end(ap);

    /* Restore interrupts and return new thread TID.  */
    restore(im);
    return tid;
}

/*
 * Obtain a new (free) thread ID.  Returns a free thread ID, or SYSERR if all
 * thread IDs are already in use.  This assumes IRQs have been disabled so that
 * the contents of the threads table are stable.
 */
static int thrnew(void)
{
    int tid;
    static int nexttid = 0;

    /* check all NTHREAD slots    */
    for (tid = 0; tid < NTHREAD; tid++)
    {
        nexttid = (nexttid + 1) % NTHREAD;
        if (THRFREE == thrtab[nexttid].state)
        {
            return nexttid;
        }
    }
    return SYSERR;
}

/**
 * @ingroup threads
 *
 * Crear una hebra afinada a un núcleo específico (0 o 1).
 * Igual que create(), pero fija cpu_home/cpu_last al núcleo indicado.
 */
tid_typ create_on(uint8 core, void *procaddr, uint ssize, int priority,
                  const char *name, int nargs, ...)
{
    irqmask im;
    ulong *saddr;
    tid_typ tid;
    struct thrent *thrptr;
    va_list ap;

    if (core > 1) core = 0;

    im = disable();

    if (ssize < MINSTK)
    {
        ssize = MINSTK;
    }

    saddr = stkget(ssize);
    if (SYSERR == (intptr_t)saddr)
    {
        restore(im);
        return SYSERR;
    }

    tid = thrnew();
    if (SYSERR == (int)tid)
    {
        stkfree(saddr, ssize);
        restore(im);
        return SYSERR;
    }

    thrcount++;
    thrptr = &thrtab[tid];

    thrptr->state = THRSUSP;
    thrptr->prio = priority;
    thrptr->stkbase = saddr;
    thrptr->stklen = ssize;
    strlcpy(thrptr->name, name, TNMLEN);
    thrptr->parent = gettid();
    thrptr->hasmsg = FALSE;
    thrptr->memlist.next = NULL;
    thrptr->memlist.length = 0;

    thrptr->fdesc[0] = CONSOLE;
    thrptr->fdesc[1] = CONSOLE;
    thrptr->fdesc[2] = CONSOLE;

    thrptr->cpu_ticks = 0;
    thrptr->born_ticks = clktotal;
    thrptr->stk_peak = 0;
    thrptr->stk_overflow = FALSE;
    thrptr->gen++;

    /* SMP: limpiar estado heredado del slot (ver create()). */
    thrptr->stk_pad[0] = 0;
    thrptr->stk_pad[1] = 0;
    thrptr->stk_pad[2] = 0;
    thrptr->elf_task = FALSE;
    thrptr->elf_img = NULL;
    thrptr->elf_size = 0;

    /* SMP: afinidad explícita */
    thrptr->cpu_home = core;
    thrptr->cpu_last = core;
    thrptr->klock_depth = 0;

    va_start(ap, nargs);
    thrptr->stkptr = setupStack(saddr, procaddr, INITRET, nargs, ap);
    va_end(ap);

    restore(im);
    return tid;
}
