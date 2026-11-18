/**
 * @file riscvclk.c
 *
 * ESP32-P4 system timer (CLINT mtime), used as the Xinu real-time clock.
 *
 * The mtime counter increments at the CPU root clock (360 MHz).  mtime is
 * 64-bit but the P4 is RV32, so it is accessed as two 32-bit words with a
 * rollover-safe read and the safe mtimecmp write sequence (high = all
 * ones, then low, then high).
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <clock.h>
#include <interrupt.h>
#include <stdint.h>
#include <riscv.h>
#include <esp32p4.h>

static uint64_t mtime_read(void)
{
    uint32_t hi, lo;

    do
    {
        hi = P4_MTIME_HI;
        lo = P4_MTIME_LO;
    }
    while (hi != P4_MTIME_HI);

    return ((uint64_t)hi << 32) | lo;
}

/* Version publica para medir tiempo de CPU en los cambios de contexto
 * (system/resched.c).  Misma lectura rollover-safe del CLINT mtime de 64 bits. */
uint64_t mtime64_public(void)
{
    return mtime_read();
}

/* clkcount() interface is documented in clock.h  */
ulong clkcount(void)
{
    return (ulong)mtime_read();
}

/* clkupdate() interface is documented in clock.h  */
void clkupdate(ulong cycles)
{
    uint64_t when = mtime_read() + (uint64_t)cycles;

    P4_MTIMECMP_HI = 0xFFFFFFFFU;
    P4_MTIMECMP_LO = (uint32_t)when;
    P4_MTIMECMP_HI = (uint32_t)(when >> 32);
}

/* Programa el comparador de timer del hart 1 (core1).  La secuencia
 * HI=sentinel, LO, HI debe ser atomica frente a la IRQ de timer del propio
 * hart1 (si preempta entre LO y HI, el comparador queda mixto).  El BKL
 * anidado lo serializa sin dormir. */
void clkupdate1(ulong cycles)
{
    irqmask im = disable();
    uint64_t when = mtime_read() + (uint64_t)cycles;

    P4_MTIMECMP1_HI = 0xFFFFFFFFU;
    P4_MTIMECMP1_LO = (uint32_t)when;
    P4_MTIMECMP1_HI = (uint32_t)(when >> 32);
    restore(im);
}
