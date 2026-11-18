/**
 * @file initialize.c
 * The system begins intializing after the C environment has been
 * established.  After intialization, the null thread remains always in
 * a ready (THRREADY) or running (THRCURR) state.
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <kernel.h>
#include <clock.h>
#include <device.h>
#include <memory.h>
#include <bufpool.h>
#include <thread.h>
#include <queue.h>
#include <semaphore.h>
#include <mailbox.h>
#include <monitor.h>
#include <network.h>
#include <sdmmc.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <platform.h>

/* Function prototypes */
extern thread main(void);       /* main is the first thread created    */
static int sysinit(void);       /* intializes system structures        */

/* Declarations of major kernel variables */
struct thrent thrtab[NTHREAD];  /* Thread table                   */
struct sement semtab[NSEM];     /* Semaphore table                */
struct monent montab[NMON];     /* Monitor table                  */
struct memblock memlist;        /* List of free memory blocks     */
struct bfpentry bfptab[NPOOL];  /* List of memory buffer pools    */

/* Active system status */
int thrcount;                   /* Number of live user threads         */

/* Params set by startup.S */
void *memheap;                  /* Bottom of heap (top of O/S stack)   */
ulong cpuid;                    /* Processor id                        */

struct platform platform;       /* Platform specific configuration     */

/**
 * Intializes the system and becomes the null thread.
 * This is where the system begins after the C environment has been 
 * established.  Interrupts are initially DISABLED, and must eventually 
 * be enabled explicitly.  This routine turns itself into the null thread 
 * after initialization.  Because the null thread must always remain ready 
 * to run, it cannot execute code that might cause it to be suspended, wait 
 * for a semaphore, or put to sleep, or exit.  In particular, it must not 
 * do I/O unless it uses kprintf for synchronous output.
 */
void nulluser(void)
{
    /* Platform-specific initialization  */
    platforminit();

    /* General initialization  */
    sysinit();

    /* Enable interrupts  */
    enable();

    /* Liberar core1 para que entre en su bucle SMP idle */
    {
        extern char _core1stk[];
        volatile uint32_t *go = (volatile uint32_t *)(_core1stk + 12);
        *go = 1;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }

    /* Spawn the main thread  */
    ready(create(main, INITSTK, INITPRIO, "MAIN", 0), RESCHED_YES);

    /* null thread has nothing else to do but cannot exit  */
    while (TRUE)
    {
        /* pause() es wfi: no toca el estado de la hebra, asi que el tid 0 sigue
         * en la readylist y dequeue() nunca la ve vacia.  Con sleep(100) el nulo
         * pasa a THRSLEEP y, si todas las demas hebras estan bloqueadas,
         * resched() saca EMPTY (-2) de la readylist, indexa thrtab[-2] y hace
         * ctxsw a una pila basura con las IRQ enmascaradas: ni el tick del reloj
         * vuelve a disparar y la placa se queda muerta para siempre. */
        pause();
    }
}

/**
 * Intializes all Xinu data structures and devices.
 * @return OK if everything is initialized successfully
 */
static int sysinit(void)
{
    int i;
    struct thrent *thrptr;      /* thread control block pointer  */
    struct memblock *pmblock;   /* memory block pointer          */

    /* Initialize system variables */
    /* Count this NULLTHREAD as the first thread in the system. */
    thrcount = 1;

    /* Initialize free memory list */
    memheap = roundmb(memheap);
    platform.maxaddr = truncmb(platform.maxaddr);
    memlist.next = pmblock = (struct memblock *)memheap;
    memlist.length = (uintptr_t)(platform.maxaddr - memheap);
    pmblock->next = NULL;
    pmblock->length = (uintptr_t)(platform.maxaddr - memheap);

    /* Initialize thread table */
    for (i = 0; i < NTHREAD; i++)
    {
        thrtab[i].state = THRFREE;
    }

    /* initialize percpu (core0 activo, core1 preparado) */
    percpu[0].current = NULLTHREAD;
    percpu[0].resdef = 0;
    percpu[0].nest = 0;
    percpu[0].idle = 0;
    percpu[0].need_resched = 0;
    percpu[0].readylock.lock = 0;
    percpu[1].current = -1;
    percpu[1].resdef = 0;
    percpu[1].nest = 0;
    percpu[1].idle = 1;
    percpu[1].need_resched = 0;
    percpu[1].readylock.lock = 0;

    /* initialize null thread entry */
    thrptr = &thrtab[NULLTHREAD];
    thrptr->state = THRCURR;
    thrptr->prio = 0;
    strlcpy(thrptr->name, "prnull", TNMLEN);
    thrptr->stkbase = (void *)&_end;
    thrptr->stklen = (ulong)memheap - (ulong)&_end;
    thrptr->stkptr = 0;
    thrptr->memlist.next = NULL;
    thrptr->memlist.length = 0;
    thrptr->cpu_home = 0;
    thrptr->cpu_last = 0;
    thrptr->klock_depth = 0;

    /* initialize idle thread for core1 (TID 1, reservado) */
    {
        extern char _core1stk[];
        extern char _core1stk_end[];
        thrptr = &thrtab[1];
        thrptr->state = THRCURR;
        thrptr->prio = 0;
        strlcpy(thrptr->name, "idle1", TNMLEN);
        thrptr->stkbase = (void *)_core1stk_end;
        thrptr->stklen = (ulong)(_core1stk_end - _core1stk);
        thrptr->stkptr = 0;
        thrptr->memlist.next = NULL;
        thrptr->memlist.length = 0;
        thrptr->cpu_home = 1;
        thrptr->cpu_last = 1;
        thrptr->klock_depth = 0;
    }

    /* Initialize semaphores */
    for (i = 0; i < NSEM; i++)
    {
        semtab[i].state = SFREE;
        semtab[i].queue = queinit();
    }

    /* Initialize monitors */
    for (i = 0; i < NMON; i++)
    {
        montab[i].state = MFREE;
    }

    /* Shared SDMMC controller lock (microSD + C6), owned by the kernel */
    sdmmcBusInit();

    /* Initialize buffer pools */
    for (i = 0; i < NPOOL; i++)
    {
        bfptab[i].state = BFPFREE;
    }

    /* initialize ready lists (una por core, afinidad) */
    percpu[0].readyq = queinit();
    percpu[1].readyq = queinit();

#if RTCLOCK
    /* initialize real time clock */
    clkinit();
#endif                          /* RTCLOCK */

#if NMAILBOX
    /* Mailboxes are used for inter-task message passing. */
    mailboxInit();
#endif

#if NDEVS
    for (i = 0; i < NDEVS; i++)
    {
        devtab[i].init((device*)&devtab[i]);
    }
#endif

#if NNETIF
    /* W3: arrancar la pila nativa de Xinu despues de los devices, porque
     * espethInit() ya ha registrado su callback de tramas Ethernet en
     * esp_hosted.  netInit() (network/net/netInit.c) monta el pool de
     * paquetes -- bfpalloc(NET_MAX_PKTLEN + sizeof(struct packet),
     * NET_POOLSIZE), 1598+32 por 24 buffers, ~39 KB del heap de L2MEM --
     * las tablas ARP/ruta/ICMP y crea el hilo tcpTimer. */
    if (SYSERR == netInit())
    {
        kprintf("WARNING: netInit failed\r\n");
    }
#endif

    return OK;
}
