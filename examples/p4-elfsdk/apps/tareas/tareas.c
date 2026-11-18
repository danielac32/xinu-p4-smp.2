/**
 * Tareas -- Material/gfx (sin LVGL).  Vista de apps activas (appmgr).
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

static const char *stname(int st)
{
    switch (st) {
    case APP_RUNNING:    return "RUN ";
    case APP_BACKGROUND: return "BG  ";
    case APP_CLOSING:    return "CLOS";
    default:             return "FREE";
    }
}

struct mon { ui_input_t *g; ui_status_t *st; int slot; unsigned long t; };

static void dibujar(struct mon *m)
{
    struct app_entry *t = appmgr_table();
    int n = appmgr_count(), i, y = 80, shown = 0;
    ui_buttons_clear(m->g);
    ui_fill(UI_BG);
    if (m->st) ui_status_draw(m->st);
    ui_text("Tareas / Apps", 12, 52, UI_TEXT, UI_BG);
    for (i = 0; i < n; i++) {
        char s[48];
        if (APP_FREE == t[i].state) continue;
        ui_rrect(8, y, 304, 44, 6, UI_SURFACE);
        snprintf(s, sizeof(s), "%-12.12s %s tid %d", t[i].name, stname(t[i].state), (int)t[i].tid);
        ui_text(s, 14, y + 14, UI_TEXT, UI_SURFACE);
        y += 50; shown++;
        if (y > 430) break;
    }
    if (!shown) ui_text("Sin apps registradas", 20, 120, UI_TEXT_DIM, UI_BG);
    ui_text_center("swipe borde = salir", GFX_W/2, 462, UI_TEXT_DIM, UI_BG);
}

static void ir_fondo(struct mon *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

int main(int argc, char *argv[])
{
    struct mon *m; ui_event_t ev; int fuera = 0;
    (void)argc; (void)argv;
    m = psram_malloc(sizeof(*m)); if (!m) exit(1);
    memset(m, 0, sizeof(*m)); m->slot = -1;
    m->g = ui_create_ex(UI_CHROME_NONE); if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Tareas", "/apps/tareas/app.elf", 0x4A6F, gettid());
    appmgr_take_foreground(m->slot);
    dibujar(m);
    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP))
                ir_fondo(m, &fuera);
        }
        if (millis() - m->t >= 1000) { m->t = millis(); dibujar(m); }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot)) fuera = 1;
        if (!fuera) sleep(30);
    }
    appmgr_unregister(m->slot); ui_status_destroy(m->st); ui_destroy(m->g); psram_free(m);
    exit(0); return 0;
}
