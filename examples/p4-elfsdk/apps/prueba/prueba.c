/**
 * @file prueba.c
 *
 * FASE 3 -- app minima de prueba del gfx: pinta una pantalla con rectangulos
 * y texto, y sale al tocar.  Valida el ciclo completo sin LVGL:
 *   lanzar (elf_run) -> dibujar (GFX0) -> tocar (TOUCH0) -> salir (exit).
 *
 * Estado en PSRAM (sin estaticas escribibles).
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <device.h>
#include <gfx.h>
#include <touch.h>
#include <appmgr.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

static void rect(int x, int y, int w, int h, unsigned c)
{
    struct lcd_rect r;
    r.x = x; r.y = y; r.w = w; r.h = h; r.color = c;
    control(GFX0, GFX_CTRL_RECT, (long)&r, 0);
}

static void text(const char *s, int x, int y, unsigned fg, unsigned bg)
{
    struct lcd_text t;
    t.s = s; t.x = x; t.y = y; t.fg = fg; t.bg = bg;
    control(GFX0, GFX_CTRL_TEXT, (long)&t, 0);
}

int main(int argc, char *argv[])
{
    ui_input_t *g;
    ui_event_t ev;
    int fuera = 0, slot;
    char linea[48];

    (void)argc; (void)argv;
    printf("prueba: arranca (tid=%d)\r\n", gettid());

    g = ui_create_ex(UI_CHROME_NONE);
    if (NULL == g) exit(1);
    slot = appmgr_register("PruebaGFX", "/apps/prueba/app.elf", 0x2D7F, gettid());

    control(GFX0, GFX_CTRL_FILL, 0x101820, 0);
    rect(20, 60, 280, 120, 0x2D7F);
    text("Hola desde gfx", 40, 100, C_WHITE, 0x2D7F);
    text("swipe v = salir", 40, 130, C_WHITE, 0x2D7F);

    while (!fuera)
    {
        while (ui_poll(g, &ev))
        {
            if (ui_quit_event(&ev))
            {
                /* pasar a segundo plano (seguir vivo), no matar */
                if (ui_background(slot)) { fuera = 1; break; }
            }
            else if (ev.tipo == UI_EVENT_BUTTON)
            {
                snprintf(linea, sizeof(linea), "toque %d,%d", ev.x1, ev.y1);
                text(linea, 20, 220, C_WHITE, 0x101820);
            }
        }
        sleep(40);
    }
    appmgr_unregister(slot);
    ui_destroy(g);
    printf("prueba: saliendo\r\n");
    exit(0);
    return 0;
}
