/**
 * @file lcdInit.c
 *
 * LCD device init: bind the SPI controller named in the device table
 * (csr), bring up the panel and create the lock that serializes drawing
 * between threads.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <kernel.h>
#include <device.h>
#include <lcd.h>
#include "lcdhw.h"

struct lcdctl lcdtab[NLCD];

/**
 * @ingroup lcd
 *
 * Bring up the ST7796 panel.
 *
 * @param devptr  LCD device table entry (csr = SPI controller base)
 * @return OK, or SYSERR if the bus or the panel did not respond
 */
devcall lcdInit(device *devptr)
{
    struct lcdctl *lcdptr;

    lcdptr = &lcdtab[devptr->minor];
    lcdptr->lock = moncreate();
    lcdptr->up = FALSE;
    lcdptr->rot = LCD_ROT_DEFAULT;

    lcdSpiInit((uint32_t)devptr->csr, LCD_SPI_HZ);

    if (!lcdPanelInit())
    {
        kprintf("[lcd] ST7796 no init (SPI timeout)\r\n");
        return SYSERR;
    }

    lcdptr->up = TRUE;

    /* Apagar de inmediato cualquier patron de test del ST7796 (el panel
     * puede arrancar mostrando azul) rellenando de negro antes de que el
     * fbcon pinte el log.  Evita el pantallazo azul ocasional tras el reset. */
    lcdPanelFill(C_BLACK);

    /* The panel is a boot display, not a second console: live mirroring
     * stays off.  lcdConsolePutc() still records every character into the
     * grid, and the shell paints that grid once at startup (LCD_CTRL_SYNC)
     * so the boot log is on screen while the session runs on the UART. */
    kprintf("[lcd] ST7796 %dx%d rot %d @ %u Hz\r\n",
            LCD_W, LCD_H, (int)lcdptr->rot, (unsigned)LCD_SPI_HZ);
    return OK;
}
