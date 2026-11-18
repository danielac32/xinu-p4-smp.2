/**
 * Archivos -- Material/gfx (sin LVGL).  Navegador simple de /sd.
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <timer.h>
#include <gfx.h>
#include <appmgr.h>
#include <fat_filelib.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

#define MAXE 10

struct archivos {
    ui_input_t *g; ui_status_t *st; int slot;
    char dir[96];
    char nom[MAXE][32];
    int dirf[MAXE];
    int n, sel;
    char msg[48];
};

static void listar(struct archivos *a)
{
    FL_DIR d; fl_dirent e;
    a->n = 0;
    if (!fl_opendir(a->dir, &d)) {
        snprintf(a->msg, sizeof(a->msg), "No abre %s", a->dir);
        return;
    }
    while (0 == fl_readdir(&d, &e) && a->n < MAXE) {
        if (e.filename[0] == '.') continue;
        strncpy(a->nom[a->n], e.filename, 31);
        a->nom[a->n][31] = '\0';
        a->dirf[a->n] = e.is_dir;
        a->n++;
    }
    fl_closedir(&d);
    if (a->sel >= a->n) a->sel = 0;
    if (a->n == 0) snprintf(a->msg, sizeof(a->msg), "(vacio)");
    else a->msg[0] = '\0';
}

static void dibujar(struct archivos *a)
{
    int i;
    ui_buttons_clear(a->g);
    ui_fill(UI_BG);
    if (a->st) ui_status_draw(a->st);
    ui_text("Archivos", 12, 52, UI_TEXT, UI_BG);
    ui_text(a->dir, 12, 72, UI_TEXT_DIM, UI_BG);

    for (i = 0; i < a->n; i++) {
        int y = 100 + i * 38;
        unsigned bg = (i == a->sel) ? UI_SURFACE2 : UI_SURFACE;
        char label[40];
        snprintf(label, sizeof(label), "%s%s", a->nom[i], a->dirf[i] ? "/" : "");
        ui_button(a->g, i + 1, "", 8, y, 304, 34, bg);
        ui_text(label, 16, y + 9, (i == a->sel) ? UI_TEXT : UI_TEXT_DIM, bg);
    }
    if (a->msg[0]) ui_text(a->msg, 12, 440, UI_TEXT_DIM, UI_BG);
    ui_text_center("tap abrir  borde=salir", GFX_W/2, 462, UI_TEXT_DIM, UI_BG);
}

static void abrir(struct archivos *a, int idx)
{
    if (idx < 0 || idx >= a->n) return;
    a->sel = idx;
    if (a->dirf[idx]) {
        if (strcmp(a->dir, "/") == 0 || strcmp(a->dir, "/sd") == 0)
            snprintf(a->dir, sizeof(a->dir), "/sd/%s", a->nom[idx]);
        else {
            int l = (int)strlen(a->dir);
            snprintf(a->dir + l, sizeof(a->dir) - l, "/%s", a->nom[idx]);
        }
        listar(a);
    } else {
        snprintf(a->msg, sizeof(a->msg), "Archivo: %s", a->nom[idx]);
    }
    dibujar(a);
}

static void ir_fondo(struct archivos *a, int *fuera)
{
    if (a->slot < 0) { *fuera = 1; return; }
    if (ui_background(a->slot)) { *fuera = 1; return; }
    dibujar(a);
}

int main(int argc, char *argv[])
{
    struct archivos *a;
    ui_event_t ev;
    int fuera = 0;

    (void)argc; (void)argv;
    a = psram_malloc(sizeof(*a));
    if (!a) exit(1);
    memset(a, 0, sizeof(*a));
    a->slot = -1;
    strcpy(a->dir, "/sd");

    a->g = ui_create_ex(UI_CHROME_NONE);
    if (!a->g) { psram_free(a); exit(1); }
    a->st = ui_status_create();
    a->slot = appmgr_register("Archivos", "/apps/archivos/app.elf", 0xFD20, gettid());
    appmgr_take_foreground(a->slot);
    listar(a);
    dibujar(a);

    while (!fuera) {
        while (ui_poll(a->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP))
                ir_fondo(a, &fuera);
            else if (ev.tipo == UI_EVENT_SWIPE && ev.dir == UI_DIR_LEFT) {
                /* subir un nivel */
                char *p = strrchr(a->dir, '/');
                if (p && p != a->dir) { *p = '\0'; if (a->dir[0]=='\0') strcpy(a->dir,"/"); }
                else strcpy(a->dir, "/sd");
                listar(a); dibujar(a);
            } else if (ev.tipo == UI_EVENT_BUTTON && ev.id >= 1 && ev.id <= a->n) {
                abrir(a, ev.id - 1);
            }
        }
        if (!fuera && a->slot >= 0 && APP_CLOSING == appmgr_get_state(a->slot)) fuera = 1;
        if (!fuera) sleep(30);
    }

    appmgr_unregister(a->slot);
    ui_status_destroy(a->st);
    ui_destroy(a->g);
    psram_free(a);
    exit(0);
    return 0;
}
