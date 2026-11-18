/**
 * @file resched.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <thread.h>
#include <clock.h>
#include <queue.h>
#include <memory.h>
#include <stack.h>
#include <spinlock.h>
#include <platform.h>

extern void ctxsw(void *, void *, uchar);
extern uint64_t mtime64_public(void);

/* CPU% real: timestamp de cuando entro cada core en la tarea actual (ciclos
 * mtime).  Al hacer ctxsw acumulamos los CICLOS transcurridos en un array
 * separado por tid.  Se usa resolucion de ciclo (no ms) porque las tareas
 * I/O-bound trabajan menos de 1 ms por cambio de contexto y redondearian a 0.
 * cpu_ticks (en ms) se deja intacto: lo usa taskmon y la app tareas.elf. */
static uint64_t cpu_entered[2];
volatile uint64_t cpu_cycles[NTHREAD];

/**
 * @ingroup threads
 *
 * Reschedule processor to highest priority ready thread (afinidad por core).
 * Libera/readquiere el BKL a través de ctxsw cuando está activo.
 */
int resched(void)
{
    uchar asid;
    struct thrent *throld;
    struct thrent *thrnew;
    volatile uint32_t stkmark;
    struct percpu *c = thiscpu();

    stack_note(thrcurrent, (uint32_t)&stkmark);

    if (c->resdef > 0)
    {
        c->resdef++;
        return (OK);
    }

    throld = &thrtab[thrcurrent];

    throld->intmask = disable();

    /* Terminación diferida solicitada desde otro núcleo (punto seguro). */
    if (throld->stk_pad[0])
    {
        /* completar salida como suicidio: ya bajo BKL/disable */
        extern void xdone(void);
        extern syscall send(tid_typ, message);
        if (--thrcount <= 1) xdone();
        send(throld->parent, thrcurrent);
        stkfree(throld->stkbase, throld->stklen);
        if (throld->elf_task && throld->elf_img != NULL)
        {
            psram_freemem(throld->elf_img, throld->elf_size);
            throld->elf_img = NULL;
            throld->elf_size = 0;
            throld->elf_task = FALSE;
        }
        throld->stk_pad[0] = 0;
        throld->state = THRFREE;
    }

    if (THRCURR == throld->state)
    {
        if (nonempty(c->readyq) && (throld->prio > firstkey(c->readyq)))
        {
            restore(throld->intmask);
            return OK;
        }
        throld->state = THRREADY;
        insert(thrcurrent, c->readyq, throld->prio);
    }

    thrcurrent = dequeue(c->readyq);
    if (thrcurrent < 0 || thrcurrent >= NTHREAD)
    {
        /* Fallback defensivo: no dejar el core con un tid invalido. */
        thrcurrent = (cpu_id() == 0) ? 0 : 1;
    }
    thrnew = &thrtab[thrcurrent];
    thrnew->state = THRCURR;

    thrnew->cpu_last = (uint8_t)cpu_id();

    asid = thrcurrent & 0xff;

    /* Medir ciclos de CPU del hilo saliente con resolucion de mtime. */
    {
        uint32_t core = (uint32_t)cpu_id();
        uint64_t now = mtime64_public();
        if (cpu_entered[core] != 0ULL && now >= cpu_entered[core])
        {
            cpu_cycles[thrcurrent] += (now - cpu_entered[core]);
        }
    }

    /* Handoff del BKL a través del cambio de contexto. */
    throld->klock_depth = (uint16_t)c->nest;
    c->nest = 0;
    if (throld->klock_depth > 0)
    {
        spin_release(&kernel_lock);
    }

    ctxsw(&throld->stkptr, &thrnew->stkptr, asid);

    /* al reanudarse este hilo */
    c = thiscpu();
    cpu_entered[(uint32_t)cpu_id()] = mtime64_public();
    c->nest = throld->klock_depth;
    if (c->nest > 0)
    {
        spin_acquire(&kernel_lock);
    }

    restore(throld->intmask);
    return OK;
}
