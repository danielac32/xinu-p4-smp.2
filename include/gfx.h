/**
 * @file gfx.h
 *
 * FASE 3 -- API de graficos para apps ELF, en sustitucion de LVGL.
 *
 * El device GFX0 envuelve el panel LCD0: cada control() dibuja de forma
 * inmediata y no retiene objetos ni punteros entre llamadas.  Eso elimina la
 * clase de fallos de LVGL (una sola instancia global, widgets vivos cuando
 * la hebra que los creo muere con exit()==kill()).
 *
 * Las apps conservan su modelo: estado en PSRAM, eventos de gestos en un
 * anillo y ui_poll() desde su propia hebra.  Solo cambia el como se pinta.
 */
#ifndef _GFX_H_
#define _GFX_H_

#include <device.h>
#include <lcd.h>                    /* RGB565, LCD_W, LCD_H, struct lcd_* */

/* Geometria util (la misma que el panel). */
#define GFX_W   LCD_W
#define GFX_H   LCD_H

/* Colores RGB565 (heredados de lcd.h). */
#define GFX_BLACK   C_BLACK
#define GFX_WHITE   C_WHITE
#define GFX_RED     C_RED
#define GFX_GREEN   C_GREEN
#define GFX_BLUE    C_BLUE
#define GFX_YELLOW  C_YELLOW
#define GFX_CYAN    C_CYAN
#define GFX_MAGENTA C_MAGENTA
#define GFX_GRAY    C_GRAY
#define GFX_AMBER   C_AMBER

/* control(GFX0, CMD, arg1, arg2) */
#define GFX_CTRL_CLEAR   0x01  /**< sin arg: rellenar la pantalla de negro    */
#define GFX_CTRL_FILL    0x02  /**< arg1=color RGB565                         */
#define GFX_CTRL_RECT    0x03  /**< arg1=&struct lcd_rect                     */
#define GFX_CTRL_PIXEL   0x04  /**< arg1=&struct lcd_pixel                    */
#define GFX_CTRL_TEXT    0x05  /**< arg1=&struct lcd_text                     */
#define GFX_CTRL_BLIT    0x06  /**< arg1=&struct lcd_blit (RGB565 desde PSRAM)*/
#define GFX_CTRL_LINE    0x0C  /**< arg1=&struct lcd_line                    */
#define GFX_CTRL_CIRCLE  0x0D  /**< arg1=&struct lcd_circle                  */

/* Estructuras publicas: se reutilizan las del panel para que una app no
 * tenga que convertir. */
#define gfx_rect   lcd_rect
#define gfx_pixel  lcd_pixel
#define gfx_text   lcd_text
#define gfx_blit   lcd_blit
#define gfx_line   lcd_line
#define gfx_circle lcd_circle

/* Driver functions */
devcall gfxInit(device *);
devcall gfxControl(device *, int, long, long);

#endif                          /* _GFX_H_ */
