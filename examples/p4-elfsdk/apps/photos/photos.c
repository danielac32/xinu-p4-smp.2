/**
 * Galeria (photos) -- Material/gfx (sin LVGL).
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <timer.h>
#include <gfx.h>
#include <appmgr.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

struct ph { ui_input_t *g; ui_status_t *st; int slot; int sel; };

static void dibujar(struct ph *m)
{
    int i;
    ui_buttons_clear(m->g);
    ui_fill(UI_BG);
    if (m->st) ui_status_draw(m->st);
    ui_text("Galeria", 12, 52, UI_TEXT, UI_BG);
    for (i = 0; i < 6; i++) {
        int x = 12 + (i % 2) * 152, y = 84 + (i / 2) * 120;
        unsigned bg = (i == m->sel) ? UI_PRIMARY : UI_SURFACE;
        ui_shadow(x, y, 144, 110, 8);
        ui_button(m->g, i + 1, "", x, y, 144, 110, bg);
        ui_icon(8, x + 24, y + 18, 96, 70, UI_TEXT);
    }
    ui_text_center("tap seleccionar  borde=salir", GFX_W/2, 462, UI_TEXT_DIM, UI_BG);
}

static void ir_fondo(struct ph *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

int main(int argc, char *argv[])
{
    struct ph *m; ui_event_t ev; int fuera = 0;
    (void)argc; (void)argv;
    m = psram_malloc(sizeof(*m)); if (!m) exit(1);
    memset(m, 0, sizeof(*m)); m->slot = -1;
    m->g = ui_create_ex(UI_CHROME_NONE); if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Galeria", "/apps/photos/app.elf", 0xE8A2, gettid());
    appmgr_take_foreground(m->slot);
    dibujar(m);
    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP)) ir_fondo(m, &fuera);
            else if (ev.tipo == UI_EVENT_BUTTON && ev.id >= 1 && ev.id <= 6) { m->sel = ev.id - 1; dibujar(m); }
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot)) fuera = 1;
        if (!fuera) sleep(30);
    }
    appmgr_unregister(m->slot); ui_status_destroy(m->st); ui_destroy(m->g); psram_free(m);
    exit(0); return 0;
}
