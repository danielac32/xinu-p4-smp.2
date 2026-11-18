/**
 * @file kill.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <thread.h>
#include <queue.h>
#include <memory.h>

extern void xdone(void);
extern void send_ipi(int cpu);

/**
 * @ingroup threads
 *
 * Kill a thread and remove it from the system
 * @param tid target thread
 * @return OK on success, SYSERR otherwise
 */
syscall kill(tid_typ tid)
{
    register struct thrent *thrptr;     /* thread control block */
    irqmask im;
    int cid;

    im = disable();
    if (isbadtid(tid) || (NULLTHREAD == tid))
    {
        restore(im);
        return SYSERR;
    }
    /* no matar idle1 ni hilo actual remoto de forma insegura */
    if (tid == 1)
    {
        restore(im);
        return SYSERR;
    }

    thrptr = &thrtab[tid];
    cid = cpu_id();

    /* Si la hebra es THRCURR en otro núcleo, diferir al núcleo dueño.
     * El punto seguro esta en resched.c (stk_pad[0]); hay que despertar al
     * core destino con una IPI para que entre en resched y lo procese.  Sin
     * IPI la victima podria estar en wfi/cpu-bound y el kill no se completaria
     * nunca.  El flag se publica con fence antes de la IPI. */
    if (THRCURR == thrptr->state && thrptr->cpu_last != (uint8_t)cid)
    {
        thrptr->stk_pad[0] = 1; /* kill_pending */
        __asm__ volatile("fence rw, rw" ::: "memory");
        send_ipi((int)thrptr->cpu_last);
        restore(im);
        return OK;
    }

    if (--thrcount <= 1)
    {
        xdone();
    }

    send(thrptr->parent, tid);

    stkfree(thrptr->stkbase, thrptr->stklen);

    /* Liberar la imagen ELF cargada en PSRAM si esta tarea la uso */
    if (thrptr->elf_task && thrptr->elf_img != NULL)
    {
        psram_freemem(thrptr->elf_img, thrptr->elf_size);
        thrptr->elf_img = NULL;
        thrptr->elf_size = 0;
        thrptr->elf_task = FALSE;
    }

    switch (thrptr->state)
    {
    case THRSLEEP:
    case THRTMOUT:
        unsleep(tid);
        thrptr->state = THRFREE;
        break;
    case THRCURR:
        thrptr->state = THRFREE;        /* suicide */
        resched();
        break;

    case THRWAIT:
        semtab[thrptr->sem].count++;
        /* fall through to remove from queue */

    case THRREADY:
        getitem(tid);           /* removes from queue */

    default:
        thrptr->state = THRFREE;
    }

    restore(im);
    return OK;
}
