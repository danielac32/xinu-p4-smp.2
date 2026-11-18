/**
 * @file     clkinit.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <conf.h>

#if RTCLOCK

#include <kernel.h>
#include <stddef.h>
#include <platform.h>
#include <interrupt.h>
#include <clock.h>
#include <queue.h>
#include <riscv.h>

/** @ingroup timer
 *
 * Number of timer interrupts that have occurred since ::clktime was
 * incremented.  When ::clkticks reaches ::CLKTICKS_PER_SEC, ::clktime is
 * incremented again and ::clkticks is reset to 0.
 */
volatile ulong clkticks;

/** @ingroup timer
 * Number of seconds that have elapsed since the system booted.  */
volatile ulong clktime;

/** @ingroup timer
 * Número total de ticks de 1 ms desde el arranque.  No da la vuelta en
 * sincronía con clktime; se usa para la contabilidad de CPU por tarea. */
volatile ulong clktotal;

/** Queue of sleeping processes.  */
qid_typ sleepq;

/**
 * @ingroup timer
 *
 * Initialize the clock and sleep queue.  This function is called at startup.
 */
void clkinit(void)
{
    sleepq = queinit();         /* initialize sleep queue       */

    clkticks = 0;
    clktotal = 0;

#ifdef DETAIL
    kprintf("Time base %dHz, Clock ticks at %dHz\r\n",
            platform.clkfreq, CLKTICKS_PER_SEC);
#endif

    /* registrar e iniciar tick en core0 */
    interruptVector[IRQ_TIMER] = clkhandler;
    enable_irq(IRQ_TIMER);
    clkupdate(platform.clkfreq / CLKTICKS_PER_SEC);

    /* IPI de replanificación en core0 (HP_SYSTEM FROM_CPU). */
    {
        extern interrupt ipihandler(void);
        extern void route_irq(int source, int line);
        extern void enable_irq_line(int line);
        route_irq(100, 28); /* ETS_FROM_CPU_INTR0 -> linea CLIC 28 */
        interruptVector[28] = ipihandler;
        enable_irq_line(28);
    }
}

#endif                          /* RTCLOCK */
