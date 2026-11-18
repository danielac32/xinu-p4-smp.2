/**
 * @file intutils.S
 *
 * Interrupciones globales + BKL por núcleo (SMP afinidad).
 * Con mscratch==0 (temprano) se comporta como mononúcleo.
 */
/* Embedded Xinu, Copyright (C) 2018, 2026.  All rights reserved. */
#include <riscv.h>

/* struct percpu: current(0) readyq(4) resdef(8) nest(12) idle(16) */
#define PCPU_NEST 12

.globl enable
.globl disable
.globl restore

enable:
	.func enable
	csrrsi a0, mstatus, MSTATUS_MIE
	ret
	.endfunc

/* irqmask disable(void): guarda IRQ local; si SMP activo, anida y toma BKL. */
disable:
	.func disable
	csrrci a0, mstatus, MSTATUS_MIE
	not    a0, a0
	andi   a0, a0, MSTATUS_MIE
	csrr   t0, mscratch
	beqz   t0, 2f
	lw     t1, PCPU_NEST(t0)
	addi   t2, t1, 1
	sw     t2, PCPU_NEST(t0)
	bnez   t1, 2f                /* ya anidado: BKL retenido */
	la     t3, kernel_lock
	li     t4, 1
1:	amoswap.w.aq t5, t4, 0(t3)
	bnez   t5, 1b
2:	ret
	.endfunc

/* void restore(irqmask): suelta BKL si nest llega a 0 y restaura IRQ local. */
restore:
	.func restore
	andi   a0, a0, MSTATUS_MIE
	csrr   t0, mscratch
	beqz   t0, 3f
	lw     t1, PCPU_NEST(t0)
	addi   t1, t1, -1
	sw     t1, PCPU_NEST(t0)
	bnez   t1, 3f                /* sigue anidado */
	la     t3, kernel_lock
	amoswap.w.rl x0, x0, 0(t3)
3:	beqz   a0, 4f
	csrc   mstatus, a0
	ret
4:	li     a1, MSTATUS_MIE
	csrs   mstatus, a1
	ret
	.endfunc
