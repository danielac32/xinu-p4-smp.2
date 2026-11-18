/**
 * Micdemo -- Material/gfx (sin LVGL).
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

#define ID_TOG 1

struct md { ui_input_t *g; ui_status_t *st; int slot; int on; int level; unsigned t; };

static void dibujar(struct md *m)
{
    ui_buttons_clear(m->g);
    ui_fill(UI_BG);
    if (m->st) ui_status_draw(m->st);
    ui_text("Microfono", 12, 52, UI_TEXT, UI_BG);
    ui_icon(5, 40, 100, 80, 80, UI_PRIMARY);
    ui_text(m->on ? "ACTIVO" : "APAGADO", 12, 210,
            m->on ? UI_GREEN : UI_TEXT_DIM, UI_BG);
    /* barra de nivel simulada/visual */
    ui_rrect(20, 250, 280, 24, 6, UI_SURFACE);
    if (m->on) ui_rrect(22, 252, (276 * m->level) / 100, 20, 5, UI_GREEN);
    ui_button(m->g, ID_TOG, m->on ? "Apagar" : "Encender", 60, 320, 200, 54,
              m->on ? UI_AMBER : UI_PRIMARY);
    ui_text_center("borde=salir (fondo)", GFX_W/2, 460, UI_TEXT_DIM, UI_BG);
}

static void ir_fondo(struct md *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

int main(int argc, char *argv[])
{
    struct md *m; ui_event_t ev; int fuera = 0;
    (void)argc; (void)argv;
    m = psram_malloc(sizeof(*m)); if (!m) exit(1);
    memset(m, 0, sizeof(*m)); m->slot = -1;
    m->g = ui_create_ex(UI_CHROME_NONE); if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Microfono", "/apps/micdemo/app.elf", 0x07FF, gettid());
    appmgr_take_foreground(m->slot);
    dibujar(m);
    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP)) ir_fondo(m, &fuera);
            else if (ev.tipo == UI_EVENT_BUTTON && ev.id == ID_TOG) { m->on = !m->on; dibujar(m); }
        }
        if (m->on && millis() - m->t >= 120) {
            m->t = millis();
            m->level = (m->level + 17) % 100;
            dibujar(m);
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot)) fuera = 1;
        if (!fuera) sleep(30);
    }
    appmgr_unregister(m->slot); ui_status_destroy(m->st); ui_destroy(m->g); psram_free(m);
    exit(0); return 0;
}
