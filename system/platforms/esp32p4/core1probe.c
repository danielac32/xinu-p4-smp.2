/**
 * @file core1probe.c
 *
 * Fase 0 SMP: bucle de sondas ejecutado por core1 (desde start.S:park) y
 * API para que core0 le envíe comandos.  No usa el planificador: corre
 * sobre la pila _core1stk y solo toca el buzón y memoria/regs de prueba.
 * Temporal: se retirará o evolucionará en la Fase 1.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <stdint.h>
#include <stddef.h>
#include <core1probe.h>

extern char _core1stk[];

static inline void fence_rw(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline uint32_t rd_mhartid(void)
{
    uint32_t v;
    __asm__ volatile("csrr %0, mhartid" : "=r"(v));
    return v;
}

static volatile uint32_t *mbox(void)
{
    return (volatile uint32_t *)(_core1stk + 12);
}

/* mbox: [0]=state [1]=cmd [2]=a0 [3]=a1 [4]=a2 [5]=a3 [6]=res0 [7]=res1 */

int core1_probe_ready(void)
{
    volatile uint32_t *w = (volatile uint32_t *)_core1stk;
    fence_rw();
    return (w[0] == MBOX_MAGIC);
}

int core1_probe(uint32_t cmd, uint32_t a0, uint32_t a1,
                uint32_t *r0, uint32_t *r1)
{
    volatile uint32_t *m = mbox();
    volatile uint32_t guard = 0;

    if (!core1_probe_ready())
    {
        return -1;
    }

    /* esperar a que esté idle */
    while (m[0] != MBOX_IDLE)
    {
        if (++guard > 50000000u) return -2;
    }

    m[1] = cmd;
    m[2] = a0;
    m[3] = a1;
    m[4] = 0;
    m[5] = 0;
    m[6] = 0;
    m[7] = 0;
    fence_rw();
    m[0] = MBOX_CMD;
    fence_rw();

    guard = 0;
    while (m[0] != MBOX_DONE)
    {
        if (++guard > 100000000u) return -3;
    }

    fence_rw();
    if (r0) *r0 = m[6];
    if (r1) *r1 = m[7];
    m[0] = MBOX_IDLE;
    fence_rw();
    return 0;
}

/* ---- Código que ejecuta core1 ---- */

void core1_probe_loop(void)
{
    volatile uint32_t *m = mbox();

    m[0] = MBOX_IDLE;
    fence_rw();

    for (;;)
    {
        fence_rw();
        if (m[0] == MBOX_CMD)
        {
            uint32_t cmd = m[1];
            uint32_t a0 = m[2];
            uint32_t a1 = m[3];
            uint32_t res0 = 0, res1 = 0;

            m[0] = MBOX_BUSY;
            fence_rw();

            switch (cmd)
            {
            case PROBE_NOP:
                res0 = a0;
                break;
            case PROBE_READ32:
                res0 = *(volatile uint32_t *)(uintptr_t)a0;
                break;
            case PROBE_WRITE32:
                *(volatile uint32_t *)(uintptr_t)a0 = a1;
                fence_rw();
                res0 = *(volatile uint32_t *)(uintptr_t)a0;
                break;
            case PROBE_ADD:
                res0 = a0 + a1;
                break;
            case PROBE_HARTID:
                res0 = rd_mhartid();
                break;
            case PROBE_MTIMER:
            {
                /* mtime: leer solo LO desde core1 (HI dio fault en sonda). */
                volatile uint32_t *mt = (volatile uint32_t *)0x200BFF8u;
                res0 = mt[0];
                res1 = 0;
                break;
            }
            default:
                res0 = 0xBAD00BADu;
                break;
            }

            m[6] = res0;
            m[7] = res1;
            fence_rw();
            m[0] = MBOX_DONE;
            fence_rw();
        }
        else
        {
            __asm__ volatile("nop");
        }
    }
}

