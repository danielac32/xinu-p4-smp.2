/**
 * @file percpu.h
 * Estado por núcleo para SMP (afinidad). Fase 1a: infraestructura inerte.
 */
#ifndef _PERCPU_H_
#define _PERCPU_H_

#include <stdint.h>
#include <spinlock.h>

struct percpu
{
    int current;   /* tid actual */
    int readyq;    /* qid readylist de este core */
    int resdef;    /* reschedule diferido */
    int nest;      /* anidamiento disable/BKL */
    int idle;      /* núcleo en espera */
    volatile int need_resched; /* solicitud de replanificación */
    spinlock_t readylock;      /* protege la cola de listos del core */
};

extern struct percpu percpu[2];

static inline struct percpu *thiscpu(void)
{
    uintptr_t v;
    __asm__ volatile("csrr %0, mscratch" : "=r"(v));
    if (v == 0) return &percpu[0];
    return (struct percpu *)v;
}

static inline int cpu_id(void)
{
    return (int)(thiscpu() - &percpu[0]);
}

#endif
