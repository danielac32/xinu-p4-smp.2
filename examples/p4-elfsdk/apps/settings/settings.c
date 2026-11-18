/**
 * Ajustes -- Material/gfx (sin LVGL).  Port funcional del ajuste WiFi.
 * Escanea redes, muestra senal, conecta y guarda en /wifi.txt.
 * Swipe vertical desde borde = fondo (app sigue viva).
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <timer.h>
#include <gfx.h>
#include <appmgr.h>
#include <wifinet.h>
#include <fat_filelib.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

extern int wifiSignalQuality(int32 rssi);

#define MAXR      8
#define ID_SCAN   100
#define ID_CONN   101
#define ID_SALIR  102
#define ID_NET0   1   /* 1..MAXR: seleccion de red */

struct ajustes {
    ui_input_t *g;
    ui_status_t *st;
    int slot;
    struct wifi_red redes[MAXR];
    int n, sel;
    char msg[64];
    int conectando;
};

static void guardar(const char *ssid)
{
    void *f = fl_fopen("/wifi.txt", "w");
    if (f) {
        fl_fputs(ssid, f);
        fl_fputc('\n', f);
        fl_fclose(f);
    }
}

static void dibujar(struct ajustes *m)
{
    int i;
    ui_buttons_clear(m->g);
    ui_fill(UI_BG);
    if (m->st) ui_status_draw(m->st);
    ui_text("Ajustes / WiFi", 12, 52, UI_TEXT, UI_BG);

    if (m->n <= 0) {
        ui_text_center("Sin redes. Pulsa Escanear.", GFX_W / 2, 140, UI_TEXT_DIM, UI_BG);
    }
    for (i = 0; i < m->n && i < MAXR; i++) {
        int y = 90 + i * 56;
        int sel = (i == m->sel);
        int q = m->redes[i].hay_rssi ? wifiSignalQuality(m->redes[i].rssi) : 0;
        unsigned bg = sel ? UI_SURFACE2 : UI_SURFACE;
        char s[40];

        ui_shadow(10, y, 300, 50, 6);
        ui_button(m->g, ID_NET0 + i, "", 10, y, 300, 50, bg);
        snprintf(s, sizeof(s), "%-22.22s %2d%%", m->redes[i].ssid, q);
        ui_text(s, 18, y + 8, sel ? UI_TEXT : UI_TEXT_DIM, bg);
        {
            int b;
            for (b = 0; b < 4; b++)
                ui_rect(18 + b * 9, y + 30, 6, 8,
                        b < (q + 12) / 25 ? UI_GREEN : UI_BORDER);
        }
    }

    if (m->msg[0])
        ui_text(m->msg, 12, 400, UI_TEXT_DIM, UI_BG);

    ui_button(m->g, ID_SCAN, "Escanear", 10, 430, 145, 40, UI_PRIMARY);
    ui_button(m->g, ID_CONN, "Conectar", 165, 430, 145, 40, UI_GREEN);
}

static void escanear(struct ajustes *m)
{
    snprintf(m->msg, sizeof(m->msg), "Escaneando...");
    dibujar(m);
    m->n = wifiScanFill(m->redes, MAXR);
    if (m->n < 0) { m->n = 0; snprintf(m->msg, sizeof(m->msg), "Error al escanear"); }
    else snprintf(m->msg, sizeof(m->msg), "%d redes", m->n);
    if (m->sel >= m->n) m->sel = 0;
    dibujar(m);
}

static void conectar(struct ajustes *m)
{
    int rc;
    if (m->n <= 0 || m->sel < 0 || m->sel >= m->n) return;
    snprintf(m->msg, sizeof(m->msg), "Conectando a %s...", m->redes[m->sel].ssid);
    dibujar(m);
    /* wifiRedConectar usa clave vacia aqui; redes abiertas o ya conocidas por netboot.
     * Se guarda SSID para que netboot lo intente en siguiente arranque. */
    rc = wifiRedConectar(m->redes[m->sel].ssid, "");
    if (OK == rc) {
        guardar(m->redes[m->sel].ssid);
        snprintf(m->msg, sizeof(m->msg), "Conectado: %s", m->redes[m->sel].ssid);
    } else {
        snprintf(m->msg, sizeof(m->msg), "Fallo al conectar");
    }
    dibujar(m);
}

static void ir_fondo(struct ajustes *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

int main(int argc, char *argv[])
{
    struct ajustes *m;
    ui_event_t ev;
    int fuera = 0;

    (void)argc; (void)argv;
    printf("ajustes: gfx (tid=%d)\r\n", gettid());

    m = psram_malloc(sizeof(*m));
    if (!m) exit(1);
    memset(m, 0, sizeof(*m));
    m->g = ui_create_ex(UI_CHROME_NONE);
    if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Ajustes", "/apps/ajustes/app.elf", 0x5ACF, gettid());
    appmgr_take_foreground(m->slot);

    escanear(m);

    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            if (ev.tipo == UI_EVENT_SWIPE &&
                (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP))
            { ir_fondo(m, &fuera); }
            else if (ev.tipo == UI_EVENT_BUTTON) {
                if (ev.id >= ID_NET0 && ev.id < ID_NET0 + m->n) {
                    m->sel = ev.id - ID_NET0;
                    dibujar(m);
                } else if (ev.id == ID_SCAN) {
                    escanear(m);
                } else if (ev.id == ID_CONN) {
                    conectar(m);
                } else if (ev.id == ID_SALIR) {
                    fuera = 1;
                }
            }
            if (fuera) break;
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot))
            fuera = 1;
        if (!fuera) sleep(30);
    }

    appmgr_unregister(m->slot);
    ui_status_destroy(m->st);
    ui_destroy(m->g);
    psram_free(m);
    exit(0);
    return 0;
}
