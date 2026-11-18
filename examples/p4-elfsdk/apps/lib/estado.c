/**
 * @file estado.c
 *
 * FASE 3 -- Barra de estado sin LVGL: dibuja con control(GFX0,...).
 * Lee la bateria (POWER0), el enlace (espethLinkIsUp) y la IP (netLookup)
 * por los mismos exports que antes; la senal por wifiSignalRead/Quality.
 * Todo el estado vive en el handle (PSRAM); sin estaticas escribibles.
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
#include "estado.h"

/* De exports.c (no hay cabecera publica para el RPC de senal). */
extern int wifiSignalRead(int32 *rssi, uint32 *channel);
extern int wifiSignalQuality(int32 rssi);

struct estado
{
    int x, y, w;
    unsigned long last_dato;
    unsigned long last_rssi;
    int quality;
    char ip[20];
};

static void barra_rect(int x, int y, int w, int h, unsigned c)
{
    struct lcd_rect r;
    r.x = x; r.y = y; r.w = w; r.h = h; r.color = c;
    control(GFX0, GFX_CTRL_RECT, (long)&r, 0);
}

static void barra_texto(const char *s, int x, int y, unsigned fg, unsigned bg)
{
    struct lcd_text t;
    t.s = s; t.x = x; t.y = y; t.fg = fg; t.bg = bg;
    control(GFX0, GFX_CTRL_TEXT, (long)&t, 0);
}

statusbar_t *statusbar_create(int x, int y, int w)
{
    estado_t *e = (estado_t *)psram_malloc(sizeof(estado_t));
    if (NULL == e) return NULL;
    memset(e, 0, sizeof(*e));
    e->x = x; e->y = y; e->w = w;
    strcpy(e->ip, "sin IP");
    barra_rect(x, y, w, ES_ALTO, ES_CAJA);
    barra_texto("Xinu P4", x + 4, y + 4, ES_PAPEL, ES_CAJA);
    return e;
}

void statusbar_refresh(statusbar_t *sb)
{
    unsigned long now;
    char linea[64];
    int pct;

    if (NULL == sb) return;
    now = millis();
    if (now - sb->last_dato < ES_DATO_MS) return;
    sb->last_dato = now;

    pct = (int)control(POWER0, POWER_CTRL_PERCENT, 0, 0);
    if (SYSERR == pct) pct = 0;

    if (now - sb->last_rssi >= ES_RSSI_MS)
    {
        int32 rssi = 0;
        uint32 ch = 0;
        sb->last_rssi = now;
        if (wifiSignalRead(&rssi, &ch) == 0)
            sb->quality = wifiSignalQuality(rssi);
    }

    barra_rect(sb->x, sb->y, sb->w, ES_ALTO, ES_CAJA);
    snprintf(linea, sizeof(linea), " Bat %3d%%  WiFi %3d%%  %s",
             pct, sb->quality, espethLinkIsUp() ? "up" : "down");
    barra_texto(linea, sb->x + 4, sb->y + 4, ES_PAPEL, ES_CAJA);
}

void statusbar_destroy(statusbar_t *sb)
{
    if (NULL == sb) return;
    psram_free(sb);
}

estado_t *estado_nueva(int x, int y, int w) { return statusbar_create(x, y, w); }
void estado_refrescar(estado_t *e) { statusbar_refresh(e); }
void estado_cerrar(estado_t *e) { statusbar_destroy(e); }
