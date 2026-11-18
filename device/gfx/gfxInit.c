/**
 * @file gfxInit.c
 *
 * FASE 3 -- init del device GFX0.  No posee hardware: comprueba que el panel
 * LCD0 este arriba y se guarda el descriptor.  Todo dibujo se hace despues
 * en gfxControl reenviando a control(LCD0, LCD_CTRL_*).
 */
#include <device.h>
#include <gfx.h>
#include <lcd.h>

devcall gfxInit(device *devptr)
{
    /* LCD0 se declara antes que GFX0 en xinu.conf, asi que ya esta listo. */
    if (isbaddev(LCD0))
    {
        return SYSERR;
    }
    return OK;
}
