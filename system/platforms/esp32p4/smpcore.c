/**
 * @file smpcore.c
 * Estado por núcleo y BKL (SMP afinidad).
 */
#include <percpu.h>
#include <spinlock.h>

struct percpu percpu[2];
spinlock_t kernel_lock = { 0 };
