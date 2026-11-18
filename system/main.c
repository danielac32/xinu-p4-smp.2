/**
 * @file     main.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <device.h>
#include <lcd.h>                    /* LCD_CTRL_SYNC: volcar el log al panel */
#include <net.h>
#include <platform.h>
#include <shell.h>
#include <stdio.h>
#include <thread.h>
#include <version.h>
#include <string.h>
#include <stdlib.h>
#include <elfload.h>
static void print_os_info(void);
extern int netBootTask(void);

/* FASE 1: bootTask() (splash LVGL) y launcherTask() (ELF con LVGL) se
 * desactivan:LVGL sale del kernel.  Fase 3 sustituira el splash por
 * bootPrintTask() (kprintf -> fbcon) y el lanzador por uno sobre gfx. */

/* FASE 1: LAUNCHER_ELF/LAUNCHER_STK se retiran con el lanzador LVGL. */

/**
 * Crea una hebra que TERMINA y espera su tid.  El join es receive(): al devolver,
 * userret()->kill() hace send(parent, tid), y el padre de esta hebra es main().
 * Solo vale para tareas con fin (netBootTask y, en la Fase 3, bootPrintTask).
 */
static void launch(void *proc, uint pila, const char *nombre)
{
    /* Ejecutar en core 1 (configuración probada/limpia en p4.2). */
    tid_typ tid = create_on(1, proc, pila, INITPRIO, nombre, 0);

    if (SYSERR == (int)tid)
    {
        kprintf("WARNING: no se pudo crear %s\r\n", nombre);
        return;
    }
    ready(tid, RESCHED_YES);
    while (receive() != (int)tid)
    {
        ;
    }
}

thread main(void)
{
    if (SYSERR == open(CONSOLE, SERIAL0))
    {
        kprintf("WARNING: Can't open CONSOLE over SERIAL0\r\n");
        return 0;
    }

    /* FASE 1: sin splash ni launcher LVGL.  Queda la red en segundo plano y la
     * informacion del kernel.  Fase 3 anadira: 1) bootPrintTask (esperada con
     * launch/receive) que imprime el arranque hacia el fbcon, y 2) el launcher
     * sobre gfx que se lanza al final. */

    if (SYSERR == ready(create_on(1, netBootTask, 16 * 1024, INITPRIO,
                                   "netboot", 0), RESCHED_YES))
    {
        kprintf("WARNING: Failed to create netboot\r\n");
    }

    print_os_info();

    /* FASE 2: volcar el log de arranque capturado por lcdConsolePutc() a la
     * pantalla, de una sola vez, antes de que la shell tome el puerto. */
    control(LCD0, LCD_CTRL_SYNC, 0, 0);

    /* La consola al final.  RESCHED_NO para que main() devuelva y la shell tome
     * el procesador cuando el planificador la elija. */
    if (SYSERR == ready(create(shell, INITSTK, INITPRIO, "SHELL", 3,
                               CONSOLE, CONSOLE, CONSOLE), RESCHED_NO))
    {
        kprintf("WARNING: Failed to create SHELL\r\n");
    }

    return 0;
}

/* FASE 1: launcherTask() (ELF con LVGL) eliminada; Fase 3 la sustituye. */


#ifdef DETAIL
extern ulong cpuid;
#endif

#if __LP64__
#define HEXWIDTH 16
#define DECWIDTH 20
#else
#define HEXWIDTH 8
#define DECWIDTH 10
#endif

/* Start of kernel in memory (provided by linker)  */
extern void _start(void);

static void print_os_info(void)
{
    kprintf(VERSION);
    kprintf("\r\n\r\n");

#ifdef DETAIL
    /* Output detected platform. */
    kprintf("Processor identification: 0x%lX\r\n", cpuid);
    kprintf("Detected platform as: %s, %s\r\n\r\n",
            platform.family, platform.name);
#endif

    /* Output Xinu memory layout */
    kprintf("%*ld bytes physical memory.\r\n",
        DECWIDTH, (uintptr_t)platform.maxaddr - (uintptr_t)platform.minaddr);
#ifdef DETAIL
    kprintf("           [0x%0*X to 0x%0*X]\r\n",
        HEXWIDTH, (uintptr_t)platform.minaddr,
        HEXWIDTH, (uintptr_t)(platform.maxaddr - 1));
#endif

 

    kprintf("%*ld bytes Xinu code.\r\n", DECWIDTH, (uintptr_t)&_etext - (uintptr_t)_start);
#ifdef DETAIL
    kprintf("           [0x%0*lX to 0x%0*lX]\r\n",
        HEXWIDTH, (uintptr_t)_start,
        HEXWIDTH, (uintptr_t)&_end - 1);
#endif

    kprintf("%*ld bytes stack space.\r\n",
        DECWIDTH, (uintptr_t)memheap - (uintptr_t)&_end);
#ifdef DETAIL
    kprintf("           [0x%0*lX to 0x%0*lX]\r\n",
        HEXWIDTH, (uintptr_t)&_end,
        HEXWIDTH, (uintptr_t)memheap - 1);
#endif

    kprintf("%*ld bytes heap space.\r\n",
            DECWIDTH, (uintptr_t)platform.maxaddr - (uintptr_t)memheap);
#ifdef DETAIL
    kprintf("           [0x%0*lX to 0x%0*lX]\r\n\r\n",
        HEXWIDTH, (uintptr_t)memheap,
        HEXWIDTH, (uintptr_t)platform.maxaddr - 1);
#endif
    kprintf("\r\n");
}
