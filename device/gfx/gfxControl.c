/**
 * @file gfxControl.c
 *
 * FASE 3 -- dibujo inmediato sobre LCD0, sin objetos retenidos.
 *
 * Cada llamada es autocontenida: se traduce un GFX_CTRL_* al LCD_CTRL_*
 * equivalente y se devuelve.  No hay monitor propio: el lock del panel LCD0
 * serializa ya el bus SPI, asi que dos apps no pueden intercalar una ventana
 * de dibujo con otra.
 */
#include <device.h>
#include <gfx.h>
#include <lcd.h>

devcall gfxControl(device *devptr, int func, long arg1, long arg2)
{
    (void)devptr;
    (void)arg2;

    switch (func)
    {
    case GFX_CTRL_CLEAR:
        return control(LCD0, LCD_CTRL_FILL, C_BLACK, 0);

    case GFX_CTRL_FILL:
        return control(LCD0, LCD_CTRL_FILL, arg1, 0);

    case GFX_CTRL_RECT:
    case GFX_CTRL_PIXEL:
    case GFX_CTRL_TEXT:
    case GFX_CTRL_BLIT:
    case GFX_CTRL_LINE:
    case GFX_CTRL_CIRCLE:
        /* Los comandos comparten la numeracion con LCD_CTRL_*:
         * GFX_CTRL_RECT=3 -> LCD_CTRL_RECT=3, etc. */
        return control(LCD0, func, arg1, 0);

    default:
        return SYSERR;
    }
}
