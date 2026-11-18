/**
 * @file lcdControl.c
 *
 * LCD device control(): the whole drawing API.  Each operation runs with the
 * device lock held, because a window plus its pixel run must not interleave
 * with another thread's.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <kernel.h>
#include <device.h>
#include <lcd.h>
#include "lcdhw.h"

/**
 * @ingroup lcd
 *
 * Operate the panel.
 *
 * @param devptr  LCD device table entry
 * @param func    command (LCD_CTRL_*)
 * @param arg1    command-dependent
 * @param arg2    command-dependent (unused)
 * @return OK, or SYSERR for a bad command, a null argument or a panel that
 *          never came up
 */
devcall lcdControl(device *devptr, int func, long arg1, long arg2)
{
    struct lcdctl *lcdptr;
    const struct lcd_rect *rect;
    const struct lcd_pixel *pixel;
    const struct lcd_text *text;
    const struct lcd_blit *blit;
    const struct lcd_line *line;
    const struct lcd_circle *circle;

    (void)arg2;

    lcdptr = &lcdtab[devptr->minor];
    if (!lcdptr->up)
    {
        return SYSERR;
    }

    lock(lcdptr->lock);

    switch (func)
    {
    case LCD_CTRL_BACKLIGHT:
        lcdPanelBacklight(arg1 != 0);
        break;

    /* Plain drawing owns the panel: stop mirroring the log first so the
     * boot screen / demo / test pattern is not overwritten by the next
     * kernel line or wiped by the next scroll. */
    case LCD_CTRL_FILL:
        lcdPanelFill((uint16_t)arg1);
        break;

    case LCD_CTRL_RECT:
        rect = (const struct lcd_rect *)arg1;
        if (rect == NULL)
        {
            unlock(lcdptr->lock);
            return SYSERR;
        }
        //lcdConsoleEnable(FALSE);
        lcdPanelFillRect(rect->x, rect->y, rect->w, rect->h, rect->color);
        break;

    case LCD_CTRL_PIXEL:
        pixel = (const struct lcd_pixel *)arg1;
        if (pixel == NULL)
        {
            unlock(lcdptr->lock);
            return SYSERR;
        }
        //lcdConsoleEnable(FALSE);
        lcdPanelPixel(pixel->x, pixel->y, pixel->color);
        break;

    case LCD_CTRL_TEXT:
        text = (const struct lcd_text *)arg1;
        if (text == NULL)
        {
            unlock(lcdptr->lock);
            return SYSERR;
        }
        //lcdConsoleEnable(FALSE);
        lcdPanelText(text->s, text->x, text->y, text->fg, text->bg);
        break;

    case LCD_CTRL_BLIT:
        blit = (const struct lcd_blit *)arg1;
        if (blit == NULL)
        {
            unlock(lcdptr->lock);
            return SYSERR;
        }
        //lcdConsoleEnable(FALSE);
        lcdPanelBlit(blit->x, blit->y, blit->w, blit->h, blit->img);
        break;

    case LCD_CTRL_DEMO:
        //lcdConsoleEnable(FALSE);
        lcdPanelDemo();
        break;

    case LCD_CTRL_SYNC:
        /* FASE 2: volcar de una vez la rejilla que lcdConsolePutc() ha estado
         * acumulando desde el primer printf del kernel.  Bajo el lock del
         * panel para que ningun dibujo se intercalle con el repintado. */
        lcdConsolePaint();
        break;

    case LCD_CTRL_LINE:
        /* Bresenham: pocos pixeles, se hace bajo el lock del panel. */
        line = (const struct lcd_line *)arg1;
        if (line == NULL) { unlock(lcdptr->lock); return SYSERR; }
        {
            int x0 = line->x1, y0 = line->y1;
            int x1 = line->x2, y1 = line->y2;
            int dx = x1 - x0, dy = y1 - y0;
            int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
            int err = (dx >= 0 ? dx : -dx) - (dy >= 0 ? dy : -dy);
            for (;;)
            {
                lcdPanelPixel(x0, y0, line->color);
                if (x0 == x1 && y0 == y1) break;
                {
                    int e2 = err * 2;
                    if (e2 > -(dy >= 0 ? dy : -dy)) { err -= (dy >= 0 ? dy : -dy); x0 += sx; }
                    if (e2 <  (dx >= 0 ? dx : -dx)) { err += (dx >= 0 ? dx : -dx); y0 += sy; }
                }
            }
        }
        break;

    case LCD_CTRL_CIRCLE:
        /* Circunferencia (o disco) con ecuacion de punto medio. */
        circle = (const struct lcd_circle *)arg1;
        if (circle == NULL) { unlock(lcdptr->lock); return SYSERR; }
        {
            int cx = circle->cx, cy = circle->cy, r = circle->r;
            int x = r, y = 0, err = 0;
            while (y <= x)
            {
                if (circle->fill)
                {
                    lcdPanelFillRect(cx - x, cy + y, 2 * x + 1, 1, circle->color);
                    lcdPanelFillRect(cx - x, cy - y, 2 * x + 1, 1, circle->color);
                }
                else
                {
                    lcdPanelPixel(cx + x, cy + y, circle->color);
                    lcdPanelPixel(cx + y, cy + x, circle->color);
                    lcdPanelPixel(cx - y, cy + x, circle->color);
                    lcdPanelPixel(cx - x, cy + y, circle->color);
                    lcdPanelPixel(cx - x, cy - y, circle->color);
                    lcdPanelPixel(cx - y, cy - x, circle->color);
                    lcdPanelPixel(cx + y, cy - x, circle->color);
                    lcdPanelPixel(cx + x, cy - y, circle->color);
                }
                y++;
                err += 1 + 2 * y;
                if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
            }
        }
        break;

    case LCD_CTRL_ORIENT:
        if (!lcdPanelOrient((uint8_t)arg1))
        {
            unlock(lcdptr->lock);
            return SYSERR;
        }
        lcdptr->rot = (uint8_t)arg1;
        break;

    /* Banco de pruebas del bus: pinta N pantallas y mide.  Se cuelga de
     * control() como el resto para que el lock del panel lo proteja de la
     * hebra de LVGL mientras corre. */
    case LCD_CTRL_SPEED:
        (void)lcdPanelSpeed((int)arg1);
        break;

    case LCD_CTRL_CLOCK:
        lcdSpiSetClock((uint32_t)arg1);
        kprintf("lcd: reloj de bits = %lu Hz (pedido %ld)\r\n",
                (unsigned long)lcdSpiClockHz(), (long)arg1);
        break;

    default:
        unlock(lcdptr->lock);
        return SYSERR;
    }

    unlock(lcdptr->lock);
    return OK;
}
