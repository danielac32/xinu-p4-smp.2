/**
 * @file icmpDaemon.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <icmp.h>
#include <ipv4.h>
#include <mailbox.h>
#include <stdint.h>

/**
 * @ingroup icmp
 *
 * Daemon que responde a las peticiones ICMP echo (ping).
 */
thread icmpDaemon(void)
{
#if NNETIF

    while (TRUE)
    {
        struct packet *pkt;
        uintptr_t msg;

        /* Recibir la siguiente peticion ICMP echo. */
        int rcv = mailboxReceive(icmpqueue);

        msg = (uintptr_t)rcv;
        /* Descartar error, NULL o puntero fuera de rangos RAM validos. */
        if (rcv <= 0 || (msg & 0x3UL) != 0
            || ((msg < 0x48000000UL || msg >= 0x4C000000UL)
                && (msg < 0x4F000000UL || msg >= 0x50000000UL)))
        {
            continue;
        }

        pkt = (struct packet *)rcv;
        ICMP_TRACE("Daemon received ICMP packet");
        ICMP_TRACE("%u bytes total; %u bytes ICMP header+data",
                   pkt->len, pkt->len - (pkt->curr - pkt->data));

        /* Enviar la respuesta ICMP echo reutilizando el paquete. */
        if (OK != icmpEchoReply(pkt))
        {
            ICMP_TRACE("Failed to send ICMP Echo Reply.");
        }

        /* Liberar el buffer del paquete. */
        netFreebuf(pkt);
    }
#endif /* NNETIF */
    return OK;
}
