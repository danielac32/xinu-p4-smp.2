/**
 * @file kvprintf.c
 */

/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <device.h>
#include <kernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <spinlock.h>

static spinlock_t kprintf_lock = { 0 };

/**
 * @ingroup uartgeneric
 *
 * kernel printf: formatted, synchronous output to SERIAL0.
 *
 * @param format
 *      The format string.  Not all standard format specifiers are supported by
 *      this implementation.  See _doprnt() for a description of supported
 *      conversion specifications.
 * @param ap
 *      Arguments matching those in the format string.
 *
 * @return
 *      The number of characters written.
 */
syscall kvprintf(const char *format, va_list ap)
{
    int retval;
    irqmask im;

    /* Serializar salida entre nucleos e interrupciones locales. */
    im = disable();
    spin_acquire(&kprintf_lock);
    retval = _doprnt(format, ap, (int (*)(int, uintptr_t))kputc, (uintptr_t)&devtab[SERIAL0]);
    spin_release(&kprintf_lock);
    restore(im);
    return retval;
}
