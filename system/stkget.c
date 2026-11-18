/**
 * @file stkget.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <stddef.h>
#include <memory.h>
#include <interrupt.h>
#include <platform.h>

/**
 * @ingroup memory_mgmt
 *
 * Allocate stack memory.
 *
 * Intenta reservar la pila en PSRAM (heap grande) para no agotar el heap de
 * L2MEM (memlist) con muchas hebras/apps; si PSRAM no está lista o no hay
 * bloque, cae al heap de L2MEM como antes.
 *
 * @param nbytes
 *      Number of bytes requested.
 *
 * @return
 *      ::SYSERR if @p nbytes was 0 or there is no memory to satisfy the
 *      request; otherwise returns a pointer to the <b>topmost (highest address)
 *      word</b> of the allocated memory region.  The intention is that this is
 *      the base of a stack growing down.  Free the stack with stkfree() when
 *      done with it.
 */
void *stkget(uintptr_t nbytes)
{
    irqmask im;
    struct memblock *prev, *curr, *fits, *fitsprev;
    uintptr_t rnbytes;

    if (0 == nbytes)
    {
        return (void *)SYSERR;
    }

    /* round to multiple of memblock size   */
    rnbytes = (uintptr_t)roundmb(nbytes);

    /* Preferir PSRAM: heap grande, evita agotar L2MEM con muchas pilas. */
    im = disable();
    if (psramlist.next != NULL)
    {
        prev = &psramlist;
        curr = psramlist.next;
        fits = NULL;
        fitsprev = NULL;
        while (curr != NULL)
        {
            if (curr->length >= rnbytes)
            {
                fits = curr;
                fitsprev = prev;
            }
            prev = curr;
            curr = curr->next;
        }
        if (fits != NULL)
        {
            if (rnbytes == fits->length)
            {
                fitsprev->next = fits->next;
            }
            else
            {
                fits->length -= rnbytes;
                fits = (struct memblock *)((ulong)fits + fits->length);
            }
            psramlist.length -= rnbytes;
            restore(im);
            return (void *)((ulong)fits + rnbytes - sizeof(void *));
        }
    }
    restore(im);

    /* Fallback: heap de L2MEM (memlist) */
    im = disable();

    prev = &memlist;
    curr = memlist.next;
    fits = NULL;
    fitsprev = NULL;

    /* scan list for highest block that fits */
    while (curr != NULL)
    {
        if (curr->length >= rnbytes)
        {
            fits = curr;
            fitsprev = prev;
        }
        prev = curr;
        curr = curr->next;
    }

    if (NULL == fits)
    {
        /* no block big enough */
        restore(im);
        return (void *)SYSERR;
    }

    if (rnbytes == fits->length)
    {
        fitsprev->next = fits->next;
    }
    else
    {
        /* take top portion */
        fits->length -= rnbytes;
        fits = (struct memblock *)((ulong)fits + fits->length);
    }

    memlist.length -= rnbytes;
    restore(im);
    return (void *)((ulong)fits + rnbytes - sizeof(void *));
}

/**
 * @ingroup memory_mgmt
 *
 * Free stack memory allocated with stkget(). Libera al pool del que salió
 * (PSRAM o L2MEM) según el rango de direcciones.
 */
syscall stkfree(void *p, uintptr_t len)
{
    uintptr_t rnbytes;
    void *base;

    if (NULL == p || 0 == len)
    {
        return SYSERR;
    }

    rnbytes = (uintptr_t)roundmb(len);
    base = (void *)((ulong)p - rnbytes + sizeof(void *));

    /* Si la pila vino de PSRAM, devolverla a PSRAM; si no, a memlist. */
    if (psminheap != NULL &&
        (ulong)base >= (ulong)psminheap &&
        (ulong)base < (ulong)psmaxheap)
    {
        return psram_freemem(base, rnbytes);
    }

    return memfree(base, rnbytes);
}
