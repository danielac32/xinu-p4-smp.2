/**
 * @file ui.c
 *
 * FASE 3 -- UI profesional sobre gfx.  Sin LVGL, sin objetos, sin locks:
 * cada llamada pinta directo en el panel.  Iconos vectoriales y barra de
 * estado (WiFi rojo/verde, IP compacta, bateria).
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <device.h>
#include <timer.h>
#include <gfx.h>
#include <espeth.h>
#include <network.h>
#include <power.h>
#include <appmgr.h>
#include "ui.h"

extern int wifiSignalRead(int32 *rssi, uint32 *channel);
extern int wifiSignalQuality(int32 rssi);

struct ui_status
{
    unsigned long last;
    int quality;
    char ip[20];
    int bat;
};

/* ---- primitivas ---- */
void ui_fill(unsigned c) { control(GFX0, GFX_CTRL_FILL, c, 0); }

void ui_rect(int x, int y, int w, int h, unsigned c)
{
    struct lcd_rect r;
    r.x = x; r.y = y; r.w = w; r.h = h; r.color = c;
    control(GFX0, GFX_CTRL_RECT, (long)&r, 0);
}

void ui_rrect(int x, int y, int w, int h, int r, unsigned c)
{
    /* Esquinas aproximadas con circulos/recorte: rect interior + 4 discos */
    int rr = r < (w / 2) ? r : w / 2;
    if (rr > h / 2) rr = h / 2;
    ui_rect(x + rr, y, w - 2 * rr, h, c);
    ui_rect(x, y + rr, w, h - 2 * rr, c);
    ui_circle(x + rr, y + rr, rr, c, 1);
    ui_circle(x + w - rr - 1, y + rr, rr, c, 1);
    ui_circle(x + rr, y + h - rr - 1, rr, c, 1);
    ui_circle(x + w - rr - 1, y + h - rr - 1, rr, c, 1);
}

void ui_shadow(int x, int y, int w, int h, int r)
{
    ui_rrect(x + 3, y + 4, w, h, r, 0x080A0E);
}

void ui_line(int x1, int y1, int x2, int y2, unsigned c)
{
    struct lcd_line l;
    l.x1 = x1; l.y1 = y1; l.x2 = x2; l.y2 = y2; l.color = c;
    control(GFX0, GFX_CTRL_LINE, (long)&l, 0);
}

void ui_circle(int cx, int cy, int r, unsigned c, int fill)
{
    struct lcd_circle o;
    o.cx = cx; o.cy = cy; o.r = r; o.color = c; o.fill = fill ? 1 : 0;
    control(GFX0, GFX_CTRL_CIRCLE, (long)&o, 0);
}

void ui_text(const char *s, int x, int y, unsigned fg, unsigned bg)
{
    struct lcd_text t;
    t.s = s; t.x = x; t.y = y; t.fg = fg; t.bg = bg;
    control(GFX0, GFX_CTRL_TEXT, (long)&t, 0);
}

void ui_text_center(const char *s, int cx, int y, unsigned fg, unsigned bg)
{
    int n = (int)strlen(s);
    ui_text(s, cx - n * 4, y, fg, bg);   /* 8x16 => 8 px por caracter */
}

/* ---- iconos vectoriales ---- */
void ui_icon(int kind, int x, int y, int w, int h, unsigned color)
{
    int cx = x + w / 2, cy = y + h / 2;
    switch (kind)
    {
    case 0: /* calculadora: cuerpo + cuadricula */
        ui_rrect(x + 12, y + 8, w - 24, h - 16, 4, color);
        ui_rect(cx - 14, cy - 12, 28, 8, UI_BG);
        ui_rect(cx - 14, cy, 6, 6, UI_BG);
        ui_rect(cx - 4, cy, 6, 6, UI_BG);
        ui_rect(cx + 6, cy, 6, 6, UI_BG);
        ui_rect(cx - 14, cy + 10, 6, 6, UI_BG);
        ui_rect(cx - 4, cy + 10, 16, 6, UI_BG);
        break;
    case 1: /* tres en raya: X y O */
        ui_line(cx - 20, cy - 20, cx + 4, cy + 4, color);
        ui_line(cx - 20, cy + 4, cx + 4, cy - 20, color);
        ui_circle(cx + 16, cy - 6, 12, color, 0);
        break;
    case 2: /* tetris: bloques */
        ui_rect(cx - 20, cy - 4, 12, 12, color);
        ui_rect(cx - 6, cy - 4, 12, 12, color);
        ui_rect(cx + 8, cy - 4, 12, 12, color);
        ui_rect(cx - 6, cy + 10, 12, 12, color);
        break;
    case 3: /* archivos: dos carpetas */
        ui_rect(cx - 22, cy - 10, 28, 24, color);
        ui_rect(cx - 12, cy - 18, 28, 24, color);
        ui_rect(cx - 12, cy - 18, 28, 6, UI_BG);
        break;
    case 4: /* ajustes: engranaje (circulo + dientes) */
        ui_circle(cx, cy, 14, color, 0);
        ui_circle(cx, cy, 5, color, 1);
        ui_rect(cx - 2, cy - 22, 4, 8, color);
        ui_rect(cx - 2, cy + 14, 4, 8, color);
        ui_rect(cx - 22, cy - 2, 8, 4, color);
        ui_rect(cx + 14, cy - 2, 8, 4, color);
        break;
    case 5: /* musica: nota */
        ui_circle(cx - 8, cy + 12, 8, color, 1);
        ui_rect(cx - 1, cy - 18, 4, 30, color);
        ui_rect(cx + 3, cy - 18, 14, 5, color);
        break;
    case 6: /* reloj / tareas */
        ui_circle(cx, cy, 18, color, 0);
        ui_line(cx, cy, cx, cy - 12, color);
        ui_line(cx, cy, cx + 10, cy, color);
        break;
    case 7: /* info */
        ui_circle(cx, cy, 18, color, 0);
        ui_rect(cx - 2, cy - 10, 4, 4, color);
        ui_rect(cx - 2, cy - 2, 4, 14, color);
        break;
    case 8: /* foto */
        ui_rect(cx - 20, cy - 14, 40, 28, color);
        ui_circle(cx + 10, cy - 5, 4, UI_BG, 1);
        ui_line(cx - 18, cy + 10, cx - 4, cy - 4, UI_BG);
        ui_line(cx - 4, cy - 4, cx + 16, cy + 10, UI_BG);
        break;
    case 9: /* terminal */
        ui_rect(cx - 20, cy - 14, 40, 28, color);
        ui_line(cx - 12, cy - 5, cx - 2, cy, UI_BG);
        ui_line(cx - 2, cy, cx - 12, cy + 5, UI_BG);
        ui_rect(cx + 2, cy + 6, 14, 3, UI_BG);
        break;
    default:
        ui_circle(cx, cy, 14, color, 1);
        break;
    }
}

/* ---- barra de estado ---- */
ui_status_t *ui_status_create(void)
{
    ui_status_t *s = (ui_status_t *)psram_malloc(sizeof(ui_status_t));
    if (NULL == s) return NULL;
    memset(s, 0, sizeof(*s));
    strcpy(s->ip, "sin IP");
    return s;
}

void ui_status_draw(ui_status_t *s)
{
    char linea[48];
    int x, linked;
    int32 rssi = 0;
    uint32 ch = 0;

    if (NULL == s) return;

    /* Leer RSSI directo cada segundo.  Si hay respuesta -> enlace y señal;
     * si falla -> sin enlace (rojo).  No depender de espethLinkIsUp(). */
    if (millis() - s->last >= 1000)
    {
        int rc;
        s->last = millis();
        rc = wifiSignalRead(&rssi, &ch);
        /* wifiSignalRead devuelve 1 (NO 0) cuando el RPC 294 contesta con un
         * AP asociado;SYSERR es negativo.  Medido en HW: rc=1, rssi=-41. */
        if (rc > 0 && rssi > -100 && rssi < 0)
            s->quality = wifiSignalQuality(rssi);   /* 0..100 */
        else
            s->quality = 0;
    }
    linked = (s->quality > 0) ? 1 : 0;

    /* franja */
    ui_rect(0, 0, GFX_W, 46, UI_SURFACE);
    ui_rect(0, 45, GFX_W, 2, linked ? UI_GREEN : UI_RED);

    /* 4 rallitas PROPORCIONALES al RSSI: rojo si no hay señal,
     * verde y crecientes (q/25) si la hay. */
    for (x = 0; x < 4; x++)
    {
        int h = 4 + x * 5;                 /* 4,9,14,19 px */
        int on = linked && (x < (s->quality + 12) / 25);
        unsigned c = linked ? (on ? UI_GREEN : UI_SURFACE2) : UI_RED;
        ui_rect(10 + x * 7, 38 - h, 5, h, c);
    }

    /* IP compacta */
    {
        struct netif *nif = netLookup(getdev("ETH0"));
        if (NULL == nif) strcpy(s->ip, "sin IP");
        else netaddrsprintf(s->ip, &nif->ip);
    }
    ui_text(s->ip, 46, 6, linked ? UI_TEXT : UI_TEXT_DIM, UI_SURFACE);

    /* bateria */
    s->bat = (int)control(POWER0, POWER_CTRL_PERCENT, 0, 0);
    if (SYSERR == s->bat) s->bat = 0;
    ui_rect(GFX_W - 62, 12, 34, 18, UI_BORDER);
    ui_rect(GFX_W - 60, 14, 30 * s->bat / 100, 14,
            s->bat > 20 ? UI_GREEN : UI_OFF);
    ui_rect(GFX_W - 27, 17, 4, 8, UI_BORDER);
    snprintf(linea, sizeof(linea), "%d%%", s->bat);
    ui_text(linea, GFX_W - 96, 16, UI_TEXT_DIM, UI_SURFACE);
}

void ui_status_destroy(ui_status_t *s)
{
    if (NULL == s) return;
    psram_free(s);
}

/* FASE 3: pasar a segundo plano sin morir.  El launcher ve APP_BACKGROUND,
 * la lista en "Recientes" y ofrece X para cerrar.  Esperamos aqui hasta que
 * nos reanuden (RUNNING) o nos cierren (CLOSING/FREE). */
int ui_background(int slot)
{
    if (slot < 0) return 1;
    appmgr_set_state(slot, APP_BACKGROUND);
    for (;;)
    {
        int st = appmgr_get_state(slot);
        if (APP_RUNNING == st) return 0;          /* reanudada: seguir */
        if (APP_CLOSING == st || APP_FREE == st) return 1;  /* cerrar */
        sleep(50);
    }
}
