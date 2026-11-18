/**
 * @file spinlock.h
 * Candado de spin (BKL). Fase 1a: no activo.
 */
#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include <stdint.h>

typedef struct { volatile uint32_t lock; } spinlock_t;

extern spinlock_t kernel_lock;

static inline void spin_acquire(spinlock_t *l)
{
    uint32_t tmp;
    __asm__ volatile(
        "1:\n"
        "  li %0, 1\n"
        "  amoswap.w.aq %0, %0, %1\n"
        "  bnez %0, 1b\n"
        : "=&r"(tmp), "+A"(l->lock)
        :
        : "memory");
}

static inline void spin_release(spinlock_t *l)
{
    __asm__ volatile(
        "  amoswap.w.rl x0, x0, %0\n"
        : "+A"(l->lock)
        :
        : "memory");
}

#endif
