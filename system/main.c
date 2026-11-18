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
static thread bootPrintTask(void);
static thread launcherTask(void);
extern int netBootTask(void);

/* FASE 3: el splash LVGL se sustituye por bootPrintTask(): imprime el estado
 * del arranque con kprintf (que el fbcon refleja en la rejilla del LCD) y
 * termina, para que main() sepa (via receive) que ya puede pintar el log. */
#define LAUNCHER_ELF "/apps/launcher/launcher.elf"

/**
 * Hebra de arranque: imprime el estado del kernel por kprintf.  Cada caracter
 * cae en lcdConsolePutc() (kputc), se acumula en la rejilla y se pinta al final
 * con LCD_CTRL_SYNC.  Termina con send al padre para que launch() espere.
 */
static thread bootPrintTask(void)
{
    extern ulong cpuid;
    kprintf("\r\n=== arranque Xinu P4 / SMP (sin LVGL, con gfx) ===\r\n");
    kprintf("core0/1 activos, tick preemptivo, red nativa\r\n");
    kprintf("pantalla: fbcon verde + device GFX0\r\n");
#ifdef DETAIL
    kprintf("cpuid 0x%lX\r\n", cpuid);
#endif
    return OK;
}

/** Carga el launcher gfx desde la SD en hebra propia (elf_run usa la pila del
 * llamador y exit()=kill(gettid()) se lleva la hebra, igual que en el original). */
static thread launcherTask(void)
{
    kprintf("[launcher] elf_run %s\r\n", LAUNCHER_ELF);
    if (SYSERR == elf_run(LAUNCHER_ELF, 0, NULL))
        kprintf("[launcher] fallo al cargar %s\r\n", LAUNCHER_ELF);
    return OK;
}

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

    /* 1. Red en segundo plano (core 1), sin bloquear el arranque. */
    if (SYSERR == ready(create_on(1, netBootTask, 16 * 1024, INITPRIO,
                                   "netboot", 0), RESCHED_YES))
    {
        kprintf("WARNING: Failed to create netboot\r\n");
    }

    /* 2. Tarea de arranque: imprime por kprintf (-> fbcon).  No se espera con
     * receive(): en SMP el send de kill() no siempre despierta a main y se
     * quedaba colgado aqui (medido en HW).  Un sleep breve le da tiempo. */
    ready(create_on(1, bootPrintTask, 8 * 1024, INITPRIO, "bootprint", 0),
          RESCHED_NO);
    sleep(50);

    print_os_info();

    /* 3. Volcar el log completo al panel (fbcon verde), una sola vez. */
    control(LCD0, LCD_CTRL_SYNC, 0, 0);

    /* 4. Launcher sobre gfx, en hebra propia, sin esperar (bucle infinito). */
    if (SYSERR == ready(create_on(1, launcherTask, 32 * 1024, INITPRIO,
                                   "launcher", 0), RESCHED_YES))
    {
        kprintf("WARNING: Failed to create launcher\r\n");
    }

    /* 5. Consola al final.  RESCHED_NO para que main() devuelva y la shell tome
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
