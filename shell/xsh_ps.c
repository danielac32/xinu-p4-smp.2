/**
 * @file     xsh_ps.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <stddef.h>
#include <thread.h>
#include <stdio.h>
#include <string.h>
#include <clock.h>
#include <stack.h>
#include <interrupt.h>

/**
 * @ingroup shell
 *
 * Shell command (ps) outputs thread table information.
 *
 * El CPU% es una VENTANA de 1 segundo (tipo top), no el promedio desde que
 * nacio la tarea.  Antes se calculaba cpu_ticks/(vida/100): el idle acumulaba
 * casi todos los ticks desde el arranque (99%) y las tareas I/O-bound
 * redondeaban a 0%.  Ademas el denominador era clktotal (solo core0) mientras
 * cpu_ticks lo incrementan ambos cores, asi que la suma podia dar 200%.
 *
 * Ahora: used = delta de cpu_ticks en 1 s; window = delta de clktotal (core0)
 * o de core1_ticks (core1) segun la afinidad de la tarea.  ambos snapshots se
 * toman bajo el BKL para que todas las tareas se lean en el mismo instante.
 * @param nargs number of arguments in args array
 * @param args  array of arguments
 * @return non-zero value on error
 */
shellcmd xsh_ps(int nargs, char *args[])
{
    struct thrent *thrptr;
    uint64_t cyc0[NTHREAD];
    uint64_t mt0, mt1;
    uint32_t pct[NTHREAD];
    int i;
    irqmask im;
    extern volatile uint64_t cpu_cycles[NTHREAD];
    extern uint64_t mtime64_public(void);

    static const char * const pstnams[] = {
        "curr ", "free ", "ready", "recv ",
        "sleep", "susp ", "wait ", "rtim ", "migr "
    };

    if (nargs == 2 && strcmp(args[1], "--help") == 0)
    {
        printf("Usage: %s\n\n", args[0]);
        printf("Description:\n");
        printf("\tDisplays a table of running threads.\n");
        printf("\tCPU%% is a 1-second window (not lifetime average).\n");
        printf("Options:\n");
        printf("\t--help\t display this help and exit\n");
        return 0;
    }
    if (nargs > 1)
    {
        fprintf(stderr, "%s: too many arguments\n", args[0]);
        fprintf(stderr, "Try '%s --help' for more information\n", args[0]);
        return 1;
    }

    /* snapshot 0: ciclos de mtime y contadores de CPU por tarea, bajo BKL */
    im = disable();
    mt0 = mtime64_public();
    for (i = 0; i < NTHREAD; i++)
    {
        cyc0[i] = cpu_cycles[i];
    }
    restore(im);

    sleep(1000);   /* ~1s de ventana (ticks de kernel) */

    /* snapshot 1 y calculo: pct = ciclos de CPU de la tarea / ciclos
     * transcurridos en toda la maquina.  Resolucion de ciclo, asi que
     * captura tambien tareas I/O-bound que trabajan <1ms por ctxsw.
     * El BKL garantiza que todas las tareas se lean en el mismo instante. */
    im = disable();
    mt1 = mtime64_public();
    for (i = 0; i < NTHREAD; i++)
    {
        uint64_t used;
        thrptr = &thrtab[i];
        pct[i] = 0;
        if (thrptr->state == THRFREE || mt1 <= mt0)
        {
            continue;
        }
        used = cpu_cycles[i] - cyc0[i];
        if (used > (mt1 - mt0))
        {
            used = mt1 - mt0;
        }
        pct[i] = (uint32_t)((used * 100ULL + (mt1 - mt0) / 2ULL) / (mt1 - mt0));
        if (pct[i] > 100U)
        {
            pct[i] = 100U;
        }
    }
    restore(im);

    printf("%3s %-16s %5s %4s %4s %3s %5s %-17s\n",
           "TID", "NAME", "STATE", "PRIO", "PPID", "CPU",
           "CPU%", "STK peak/total");
    printf("%3s %-16s %5s %4s %4s %3s %5s %-17s\n",
           "---", "----------------", "-----", "----", "----",
           "---", "-----", "---------------");

    for (i = 0; i < NTHREAD; i++)
    {
        thrptr = &thrtab[i];
        if (thrptr->state == THRFREE)
        {
            continue;
        }
        printf("%3d %-16s %s %4d %4d %3d %4lu%% %8lu/%-8lu\n",
               i, thrptr->name,
               (thrptr->state >= 1 && thrptr->state <= 9)
                   ? pstnams[(int)thrptr->state - 1] : "?????",
               thrptr->prio, thrptr->parent,
               (int)thrptr->cpu_home,
               (ulong)pct[i],
               (ulong)stack_peak(i), (ulong)stack_total(i));
    }

    return 0;
}
