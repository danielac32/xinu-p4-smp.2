/**
 * Grabador -- Material/gfx (sin LVGL).
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

#define ID_REC 1

struct grab { ui_input_t *g; ui_status_t *st; int slot; int rec; unsigned t0; };

static void dibujar(struct grab *m)
{
    ui_buttons_clear(m->g);
    ui_fill(UI_BG);
    if (m->st) ui_status_draw(m->st);
    ui_text("Grabador", 12, 52, UI_TEXT, UI_BG);
    ui_icon(5, 40, 100, 80, 80, UI_PRIMARY);
    ui_text(m->rec ? "GRABANDO..." : "DETENIDO", 12, 210,
            m->rec ? UI_RED : UI_TEXT_DIM, UI_BG);
    if (m->rec) {
        char s[32];
        unsigned sec = (millis() - m->t0) / 1000;
        snprintf(s, sizeof(s), "%u s", sec);
        ui_text(s, 12, 236, UI_TEXT, UI_BG);
    }
    ui_button(m->g, ID_REC, m->rec ? "Parar" : "Grabar", 60, 300, 200, 54,
              m->rec ? UI_RED : UI_PRIMARY);
    ui_text_center("borde=salir (fondo)", GFX_W/2, 460, UI_TEXT_DIM, UI_BG);
}

static void ir_fondo(struct grab *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

int main(int argc, char *argv[])
{
    struct grab *m; ui_event_t ev; int fuera = 0;
    (void)argc; (void)argv;
    m = psram_malloc(sizeof(*m)); if (!m) exit(1);
    memset(m, 0, sizeof(*m)); m->slot = -1;
    m->g = ui_create_ex(UI_CHROME_NONE); if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Grabador", "/apps/grabador/app.elf", 0xE2BF, gettid());
    appmgr_take_foreground(m->slot);
    dibujar(m);
    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP)) ir_fondo(m, &fuera);
            else if (ev.tipo == UI_EVENT_BUTTON && ev.id == ID_REC) {
                m->rec = !m->rec; if (m->rec) m->t0 = millis();
                dibujar(m);
            }
        }
        if (m->rec && (millis() % 500) < 40) dibujar(m);
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot)) fuera = 1;
        if (!fuera) sleep(30);
    }
    appmgr_unregister(m->slot); ui_status_destroy(m->st); ui_destroy(m->g); psram_free(m);
    exit(0); return 0;
}
