/**
 * @file     clkhandler.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <stddef.h>
#include <queue.h>
#include <clock.h>
#include <thread.h>
#include <platform.h>

#if RTCLOCK

void wakeup(void);
int resched(void);

/**
 * @ingroup timer
 *
 * Interrupt handler function for the timer interrupt.  This schedules a new
 * timer interrupt to occur at some point in the future, then updates ::clktime
 * and ::clkticks, then wakes sleeping threads if there are any, otherwise
 * reschedules the processor.
 */
uint32 isr_count;
volatile uint32 core1_ticks;

extern void clkupdate1(ulong cycles);

interrupt clkhandler(void)
{
    irqmask im;

    /* Ambas ramas bajo BKL: cpu_ticks en thrtab[] es compartida y la leen
     * taskmon/ps desde el otro core. */
    im = disable();

    /* Core1: el tick lo genera la IRQ del SYSTIMER target1, que llama aqui
     * desde core1_sdio_handler (cpu_id()==1).  Hacemos el bookkeeping local
     * de core1 y resched de sus tareas; NO tocamos clktotal/clktime (que son
     * del core0) ni el sleepq global.  El comparador ya lo rearmo el handler
     * del SYSTIMER antes de llamarnos. */
    if (cpu_id() != 0)
    {
        core1_ticks++;
        if (thrcurrent >= 0 && thrcurrent < NTHREAD)
        {
            thrtab[thrcurrent].cpu_ticks++;
        }
        resched();
        restore(im);
        return;
    }

    clkupdate(platform.clkfreq / CLKTICKS_PER_SEC);


    /* Another clock tick passes. */
    clkticks++;
    clktotal++;
    isr_count++;
    /* Contabilidad de CPU: el tick se carga a la tarea que estaba
     * ejecutándose (incluye el tiempo de IRQ). */
    if (thrcurrent >= 0 && thrcurrent < NTHREAD)
    {
        thrtab[thrcurrent].cpu_ticks++;
    }

    /* Update global second counter. */
    if (CLKTICKS_PER_SEC == clkticks)
    {
        clktime++;
        clkticks = 0;
    }

    /* If sleepq is not empty, decrement first key.   */
    /* If key reaches zero, call wakeup.              */
    if (nonempty(sleepq) && (--firstkey(sleepq) <= 0))
    {
        wakeup();
    }
    else
    {
        resched();
    }

    restore(im);
}

#endif /* RTCLOCK */
