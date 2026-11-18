/**
 * @file launcher.c
 *
 * FASE 3 -- Launcher profesional sobre gfx/ui (sin LVGL).
 * Grid de iconos vectoriales, barra de estado (WiFi rojo/verde, IP, bateria),
 * paginacion con swipe <-/->, conmutador de apps con swipe ^, y salir del
 * conmutador con swipe v.  Un hilo por app via appmgr + elf_run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <thread.h>
#include <device.h>
#include <timer.h>
#include <memory.h>
#include <gfx.h>
#include <elfload.h>
#include <appmgr.h>
#include <fat_filelib.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

#define MAX_APPS     12
#define PER_PAGE     6
#define APP_NAMELEN 32
#define APP_PATHLEN 80

#define P_HOME   0
#define P_SWITCH 1

#define RUN_STK     16384
#define VIGIA_STK   8192
#define VIGIA_MS    200

struct app_info
{
    char   name[APP_NAMELEN];
    char   dir[APP_NAMELEN];
    char   exec[APP_PATHLEN];
    uint32 color;
    int    kind;               /* icono vectorial */
};

struct launcher
{
    ui_input_t *g;
    ui_status_t *status;
    struct app_info apps[MAX_APPS];
    int napp;
    int page;
    int pagina;
    int oculto;
    int cerrar;
    tid_typ vig_tid;
};

/* Paleta de colores de app (RGB565).  Aqui es constante de solo lectura,
 * el compilador la pone en .rodata (no es estatica escribible). */
static const unsigned app_colors[12] = {
    0x4C66, 0xF986, 0x9B3F, 0xFD20, 0x5ACF, 0xE2BF,
    0x34DF, 0x07EF, 0xE8A2, 0x2D7F, 0x4A6F, 0x8A24
};

static void recorta(char *s)
{
    char *p = s, *f;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    f = s + strlen(s);
    while (f > s && (f[-1] == ' ' || f[-1] == '\t' || f[-1] == '\r' || f[-1] == '\n')) f--;
    *f = '\0';
}

static uint32 parse_color(const char *s)
{
    uint32 v = 0;
    char c;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s)
    {
        c = *s++;
        if (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
        else break;
    }
    return v;
}

static int icon_kind(const char *name)
{
    if (strstr(name, "calc")) return 0;
    if (strstr(name, "tictac") || strstr(name, "tres")) return 1;
    if (strstr(name, "tetris")) return 2;
    if (strstr(name, "archiv") || strstr(name, "file")) return 3;
    if (strstr(name, "set") || strstr(name, "ajust")) return 4;
    if (strstr(name, "music") || strstr(name, "audio")) return 5;
    if (strstr(name, "tarea") || strstr(name, "monitor")) return 6;
    if (strstr(name, "info")) return 7;
    if (strstr(name, "photo") || strstr(name, "foto")) return 8;
    return 9;
}

static int leer_app(const char *dir, struct app_info *ai, int idx)
{
    char path[96], buf[384], *p;
    void *f;
    int n, lin;

    memset(ai, 0, sizeof(*ai));
    ai->color = app_colors[idx % 12];
    ai->kind = 9;
    snprintf(path, sizeof(path), "/apps/%s/app.txt", dir);
    f = fl_fopen(path, "r");
    if (NULL == f) return 0;
    n = fl_fread(buf, 1, (int)sizeof(buf) - 1, f);
    fl_fclose(f);
    if (n <= 0) return 0;
    buf[n] = '\0';

    p = buf;
    for (lin = 0; lin < 32 && *p; lin++)
    {
        char *eol = p, *eq;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        if (*eol) { *eol = '\0'; eol++; }
        while (*eol == '\r' || *eol == '\n') eol++;
        recorta(p);
        if (*p == '\0' || *p == '#') { p = eol; continue; }
        eq = p;
        while (*eq && *eq != '=') eq++;
        if (*eq == '=')
        {
            *eq = '\0'; recorta(p); recorta(eq + 1);
            if (0 == strcmp(p, "name")) strncpy(ai->name, eq + 1, APP_NAMELEN - 1);
            else if (0 == strcmp(p, "exec")) strncpy(ai->exec, eq + 1, APP_PATHLEN - 1);
            else if (0 == strcmp(p, "color")) ai->color = parse_color(eq + 1);
        }
        p = eol;
    }
    strncpy(ai->dir, dir, APP_NAMELEN - 1);
    if (ai->name[0] == '\0') strncpy(ai->name, dir, APP_NAMELEN - 1);
    if (ai->exec[0] == '\0') snprintf(ai->exec, sizeof(ai->exec), "/apps/%s/app.elf", dir);
    ai->kind = icon_kind(ai->name);
    return 1;
}

static void scan_apps(struct launcher *l)
{
    FL_DIR d;
    fl_dirent e;
    l->napp = 0;
    memset(l->apps, 0, sizeof(l->apps));
    if (NULL == fl_opendir("/apps", &d)) return;
    while (0 == fl_readdir(&d, &e))
    {
        if (e.filename[0] == '.' || !e.is_dir) continue;
        if (0 == strcmp(e.filename, "launcher")) continue;
        if (l->napp >= MAX_APPS) break;
        if (leer_app(e.filename, &l->apps[l->napp], l->napp)) l->napp++;
    }
    fl_closedir(&d);
}

static int app_runner(char *path)
{
    int r = elf_run(path, 0, NULL);
    if (SYSERR == r)
    {
        int slot = appmgr_find_path(path);
        if (slot >= 0) appmgr_unregister(slot);
    }
    return 0;
}

static void lanzar_app(struct launcher *l, int idx)
{
    int slot, tid;
    struct app_info *ai;

    if (idx < 0 || idx >= l->napp) return;
    ai = &l->apps[idx];
    slot = appmgr_find_path(ai->exec);
    if (slot >= 0)
    {
        int st = appmgr_get_state(slot);
        if (APP_CLOSING == st) return;
        if (APP_BACKGROUND == st || APP_RUNNING == st)
        {
            appmgr_take_foreground(slot);
            return;
        }
        appmgr_unregister(slot);
    }
    tid = create((void *)app_runner, RUN_STK, getprio(gettid()), ai->name, 1,
                 (long)ai->exec);
    if (SYSERR != tid)
    {
        appmgr_register(ai->name, ai->exec, ai->color, tid);
        appmgr_take_foreground(appmgr_find_path(ai->exec));
        ready(tid, RESCHED_NO);
    }
}

static void home_draw(struct launcher *l)
{
    int i, col, row, x, y, base, pages;
    char h[32];

    ui_fill(UI_BG);
    ui_status_draw(l->status);
    ui_text("Aplicaciones", 12, 54, UI_TEXT, UI_BG);

    pages = (l->napp + PER_PAGE - 1) / PER_PAGE;
    if (pages < 1) pages = 1;
    if (l->page >= pages) l->page = pages - 1;
    if (l->page < 0) l->page = 0;
    base = l->page * PER_PAGE;

    for (i = 0; i < PER_PAGE && (base + i) < l->napp; i++)
    {
        col = i % 2; row = i / 2;
        x = 14 + col * 152; y = 92 + row * 120;
        ui_shadow(x, y, 132, 104, 8);
        ui_rrect(x, y, 132, 104, 8, UI_SURFACE);
        ui_rrect(x + 9, y + 7, 54, 54, 8, l->apps[base + i].color);
        ui_icon(l->apps[base + i].kind, x + 9, y + 7, 54, 54, UI_TEXT);
        ui_text(l->apps[base + i].name, x + 7, y + 70, UI_TEXT, UI_SURFACE);
    }

    if (0 == l->napp)
        ui_text("No hay apps en /apps", 20, 200, UI_TEXT_DIM, UI_BG);

    /* paginacion */
    if (pages > 1)
    {
        snprintf(h, sizeof(h), "< %d / %d >", l->page + 1, pages);
        ui_text_center(h, GFX_W / 2, 452, UI_TEXT_DIM, UI_BG);
    }
    else ui_text_center("swipe ^ recientes", GFX_W / 2, 452, UI_TEXT_DIM, UI_BG);

    /* pistas de gestos: SOLO texto, sin franja (no solapa la paginacion) */
    ui_text_center("< / > paginas   |   ^ recientes", GFX_W / 2, 470,
                   UI_TEXT_DIM, UI_BG);
}

static void switch_draw(struct launcher *l)
{
    struct app_entry *t = appmgr_table();
    int n = appmgr_count(), i, y = 92, activos = 0;
    char label[40];

    ui_fill(UI_BG);
    ui_status_draw(l->status);
    ui_text("Recientes", 12, 54, UI_TEXT, UI_BG);
    for (i = 0; i < n; i++)
    {
        int color;
        if (APP_FREE == t[i].state) continue;
        activos++;
        color = t[i].color ? t[i].color : UI_SURFACE2;
        ui_shadow(10, y, 300, 52, 6);
        ui_rrect(10, y, 300, 52, 6, UI_SURFACE);
        /* Miniatura: icono vectorial de la app dentro de su color */
        ui_rrect(14, y + 6, 40, 40, 5, color);
        ui_icon(icon_kind(t[i].name), 14, y + 6, 40, 40, UI_TEXT);
        if (APP_BACKGROUND == t[i].state)
            snprintf(label, sizeof(label), "%s · bg", t[i].name);
        else
            snprintf(label, sizeof(label), "%s", t[i].name);
        ui_text(label, 62, y + 10, UI_TEXT, UI_SURFACE);
        ui_text("Toca para reanudar", 62, y + 30, UI_TEXT_DIM, UI_SURFACE);
        /* Boton X para MATAR la app (appmgr_request_close) */
        ui_rrect(272, y + 10, 30, 30, 5, UI_RED);
        ui_text_center("X", 287, y + 16, UI_TEXT, UI_RED);
        y += 60;
        if (y > 430) break;
    }
    if (0 == activos) ui_text("Sin apps activas", 20, 200, UI_TEXT_DIM, UI_BG);
    ui_text_center("^ inicio   |   v salir", GFX_W / 2, 465, UI_TEXT_DIM, UI_BG);
}

static void pantalla_home(struct launcher *l)
{
    l->pagina = P_HOME;
    l->page = 0;
    ui_buttons_clear(l->g);
    scan_apps(l);
    home_draw(l);
    ui_clear_events(l->g);
}

static void mostrar_switch(struct launcher *l)
{
    l->pagina = P_SWITCH;
    ui_buttons_clear(l->g);
    switch_draw(l);
    ui_clear_events(l->g);
}

static void procesar(struct launcher *l, ui_event_t *ev)
{
    if (UI_EVENT_SWIPE == ev->tipo)
    {
        if (P_HOME == l->pagina)
        {
            if (UI_DIR_UP == ev->dir) mostrar_switch(l);
            else if (UI_DIR_LEFT == ev->dir) { l->page++; home_draw(l); }
            else if (UI_DIR_RIGHT == ev->dir) { l->page--; home_draw(l); }
        }
        else
        {
            if (UI_DIR_UP == ev->dir) pantalla_home(l);
            else if (UI_DIR_DOWN == ev->dir)
            {
                /* salir del conmutador: cerrar todas las apps activas */
                struct app_entry *t = appmgr_table();
                int n = appmgr_count(), i;
                for (i = 0; i < n; i++)
                    if (APP_FREE != t[i].state) appmgr_request_close(i);
                pantalla_home(l);
            }
        }
        return;
    }
    if (UI_EVENT_BUTTON == ev->tipo)
    {
        if (P_HOME == l->pagina)
        {
            int base = l->page * PER_PAGE, col, row;
            for (row = 0; row < 3; row++)
                for (col = 0; col < 2; col++)
                {
                    int idx = base + row * 2 + col;
                    int x, y;
                    if (idx >= l->napp) continue;
                    x = 14 + col * 152; y = 92 + row * 120;
                    if (ev->x1 >= x && ev->x1 < x + 132 &&
                        ev->y1 >= y && ev->y1 < y + 104)
                    { lanzar_app(l, idx); return; }
                }
        }
        else
        {
            struct app_entry *t = appmgr_table();
            int n = appmgr_count(), i, y = 92;
            for (i = 0; i < n; i++)
            {
                if (APP_FREE == t[i].state) continue;
                if (ev->y1 >= y && ev->y1 < y + 52)
                {
                    if (ev->x1 >= 272) appmgr_request_close(i);
                    else appmgr_take_foreground(i);
                    switch_draw(l);
                    return;
                }
                y += 60;
            }
        }
    }
}

static int hay_app_running(void)
{
    int i, n = appmgr_count();
    struct app_entry *t = appmgr_table();
    for (i = 0; i < n; i++) if (APP_RUNNING == t[i].state) return 1;
    return 0;
}

static void vigia(ulong arg)
{
    struct launcher *l = (struct launcher *)arg;
    while (!l->cerrar) sleep(VIGIA_MS);
    send(l->vig_tid, OK);
}

int main(int argc, char *argv[])
{
    struct launcher *l;
    ui_event_t ev;
    unsigned long t_status = 0;

    (void)argc; (void)argv;
    printf("launcher: UI gfx (tid=%d)\r\n", gettid());

    l = (struct launcher *)psram_malloc(sizeof(struct launcher));
    if (NULL == l) exit(1);
    memset(l, 0, sizeof(struct launcher));
    l->vig_tid = SYSERR;

    l->g = ui_create_ex(UI_CHROME_NONE);
    if (NULL == l->g) { psram_free(l); exit(1); }
    l->status = ui_status_create();
    if (NULL == l->status) { ui_destroy(l->g); psram_free(l); exit(1); }

    pantalla_home(l);
    l->vig_tid = create_on(1, (void *)vigia, VIGIA_STK, getprio(gettid()),
                           "launch-vig", 1, (long)l);
    if (SYSERR != l->vig_tid) ready(l->vig_tid, RESCHED_NO);

    for (;;)
    {
        if (!l->oculto && hay_app_running()) l->oculto = 1;
        if (l->oculto && !hay_app_running())
        {
            /* La app paso a BACKGROUND: mostrar directamente "Recientes" con
             * sus tarjetas y el boton X para matarla. */
            l->oculto = 0;
            {
                struct app_entry *t = appmgr_table();
                int n = appmgr_count(), i, hay_bg = 0;
                for (i = 0; i < n; i++)
                    if (APP_BACKGROUND == t[i].state) hay_bg = 1;
                if (hay_bg) mostrar_switch(l);
                else pantalla_home(l);
            }
        }
        /* Si una app esta en primer plano, NO leer TOUCH0: si el launcher
         * tambien sondea el panel, se queda con los eventos antes que la app
         * y esta no responde (medido en HW). */
        if (!l->oculto)
        {
            while (ui_poll(l->g, &ev)) procesar(l, &ev);
        }
        /* Refrescar la barra una vez por segundo con temporizador acumulativo
         * (millis()%1000<40 hacia parpadear el banner al repintar de golpe). */
        if (millis() - t_status >= 1000)
        {
            t_status = millis();
            if (!l->oculto) ui_status_draw(l->status);
        }
        sleep(40);
    }
    return 0;
}
