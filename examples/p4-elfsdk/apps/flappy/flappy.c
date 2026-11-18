/**
 * Flappy Bird -- Material/gfx (sin LVGL).  Port fiel de la logica original.
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <timer.h>
#include <memory.h>
#include <gfx.h>
#include <appmgr.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

#define ESTADO_LISTO     0
#define ESTADO_CORRIENDO 1
#define ESTADO_FIN       2

#define ANCHO      320
#define PISO_Y     440
#define PISO_H     40
#define AN_BIRD    14
#define Y_INICIO   200
#define V_FLAP     260
#define V_GRAV     1000
#define VY_MAX     420
#define VELO_PIPE  105
#define PASO_PIPE  130
#define AN_PIPE    30
#define GAP        150
#define MIN_GAP    70
#define MAX_GAP    190
#define BIRD_X     160
#define MAX_PIPE   3

#define ID_REINICIAR 1
#define ID_SALIR     2
#define X_SALIR     272
#define Y_SALIR     50
#define AN_SALIR    40
#define AL_SALIR    32
#define X_JUGAR     60
#define Y_JUGAR     360
#define AN_JUGAR    200
#define AL_JUGAR    50

#define C_CIELO  0x4A90C4
#define C_PIPE   0x9BD14F
#define C_BIRD   0xFFC93C
#define C_ALA    0xD9990A
#define C_PICO   0xFF6B1A
#define C_PISO   0x8B5A2B
#define C_TEXTO  UI_TEXT
#define C_ROJO   UI_RED
#define C_AMBER  UI_AMBER
#define C_CAJA   UI_SURFACE

struct flit
{
    int x;
    int gap_top;
    int anotado;
};

struct flappy
{
    ui_input_t *g;
    ui_status_t *st;
    int estado;
    int bird_y;
    int bird_vy;
    int puntos;
    struct flit f[MAX_PIPE];
    unsigned lcg;
    unsigned ultimo_tick;
    int slot;
};

static void spawn(struct flappy *m, int slot)
{
    struct flit *p = &m->f[slot];
    unsigned r = m->lcg = m->lcg * 1103515245u + 12345u;
    int top = MIN_GAP + (int)(r % (unsigned)(MAX_GAP - MIN_GAP));
    p->gap_top = top;
    p->anotado = 0;
}

static void dibujar_pajaro(int bx, int by)
{
    /* cuerpo, ala, pico, ojo, pupila (mismas piezas que original) */
    ui_rrect(bx,      by + 2, 20, 13, 6, C_BIRD);
    ui_rrect(bx + 3,  by + 6,  8,  6, 3, C_ALA);
    ui_rrect(bx + 19, by + 6,  5,  4, 2, C_PICO);
    ui_rrect(bx + 12, by + 3,  5,  5, 3, 0xFFFF);
    ui_rrect(bx + 14, by + 4,  2,  2, 1, 0x101820);
}

static void pintar(struct flappy *m)
{
    int i;
    char txt[16];

    ui_buttons_clear(m->g);
    /* fondo cielo */
    ui_fill(C_CIELO);
    if (m->st) ui_status_draw(m->st);

    /* tubos */
    for (i = 0; i < MAX_PIPE; i++)
    {
        struct flit *p = &m->f[i];
        if (p->x + AN_PIPE < -4) continue;
        ui_rect(p->x, 46, AN_PIPE, p->gap_top - 46 > 0 ? p->gap_top - 46 : 0, C_PIPE);
        ui_rect(p->x, p->gap_top + GAP, AN_PIPE,
                PISO_Y - (p->gap_top + GAP), C_PIPE);
    }

    /* suelo */
    ui_rect(0, PISO_Y, ANCHO, PISO_H, C_PISO);

    /* pajaro */
    dibujar_pajaro(BIRD_X, m->bird_y);

    /* marcador */
    snprintf(txt, sizeof(txt), "SCORE %d", m->puntos);
    {
        int tw = (int)strlen(txt) * 8 + 20;
        ui_rrect(4, 50, tw, 28, 6, C_CAJA);
        ui_text(txt, 14, 56, C_TEXTO, C_CAJA);
    }

    if (ESTADO_FIN == m->estado)
    {
        ui_text("GAME OVER", 120, 220, C_ROJO, C_CIELO);
    }

    if (ESTADO_CORRIENDO != m->estado)
    {
        ui_button(m->g, ID_REINICIAR, "JUGAR", X_JUGAR, Y_JUGAR,
                  AN_JUGAR, AL_JUGAR, C_AMBER);
    }
    ui_button(m->g, ID_SALIR, "X", X_SALIR, Y_SALIR, AN_SALIR, AL_SALIR, UI_RED);

    if (ESTADO_LISTO == m->estado)
    {
        ui_text("TOQUE PARA VOLAR", 90, 300, C_TEXTO, C_CIELO);
    }
}

static void fin(struct flappy *m)
{
    m->estado = ESTADO_FIN;
}

static void reiniciar(struct flappy *m)
{
    int i;
    m->estado = ESTADO_LISTO;
    m->bird_y = Y_INICIO;
    m->bird_vy = 0;
    m->puntos = 0;
    m->lcg = 1;
    for (i = 0; i < MAX_PIPE; i++)
    {
        m->f[i].x = ANCHO + 30 + i * PASO_PIPE;
        spawn(m, i);
    }
}

static void dar_flap(struct flappy *m)
{
    if (ESTADO_FIN == m->estado) reiniciar(m);
    m->estado = ESTADO_CORRIENDO;
    m->bird_vy = -V_FLAP;
}

static void tick(struct flappy *m)
{
    unsigned ahora = millis();
    unsigned dt = ahora - m->ultimo_tick;
    int i;

    m->ultimo_tick = ahora;
    if (dt > 40) dt = 40;

    if (ESTADO_CORRIENDO == m->estado)
    {
        m->bird_vy = m->bird_vy + (V_GRAV * (int)dt) / 1000;
        if (m->bird_vy > VY_MAX) m->bird_vy = VY_MAX;
        m->bird_y = m->bird_y + (m->bird_vy * (int)dt) / 1000;
        if (m->bird_y < 46) m->bird_y = 46;

        for (i = 0; i < MAX_PIPE; i++)
        {
            struct flit *p = &m->f[i];
            if (p->x + AN_PIPE < -4)
            {
                p->x = ANCHO + 30 + i * PASO_PIPE;
                spawn(m, i);
                continue;
            }
            p->x = p->x - ((VELO_PIPE * (int)dt) / 1000);
            if (!p->anotado && (BIRD_X + AN_BIRD) > (p->x + AN_PIPE))
            {
                p->anotado = 1;
                m->puntos = m->puntos + 1;
            }
        }

        if (m->bird_y + AN_BIRD >= PISO_Y) { fin(m); return; }
        for (i = 0; i < MAX_PIPE; i++)
        {
            struct flit *p = &m->f[i];
            if (p->x + AN_PIPE < -4) continue;
            if ((BIRD_X + AN_BIRD) > p->x && BIRD_X < (p->x + AN_PIPE))
            {
                if (m->bird_y < p->gap_top ||
                    (m->bird_y + AN_BIRD) > (p->gap_top + GAP))
                { fin(m); return; }
            }
        }
    }
}

static void ir_fondo(struct flappy *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    m->ultimo_tick = millis();
    pintar(m);
}

static void procesar(struct flappy *m, ui_event_t *ev, int *fuera)
{
    if (ev->tipo == UI_EVENT_SWIPE &&
        (ev->edge == UI_EDGE_BOTTOM || ev->edge == UI_EDGE_TOP) &&
        (ev->dir == UI_DIR_UP || ev->dir == UI_DIR_DOWN))
    {
        ir_fondo(m, fuera);
        return;
    }
    if (ev->tipo == UI_EVENT_TAP || ev->tipo == UI_EVENT_LONG_PRESS)
    {
        dar_flap(m);
        return;
    }
    if (ev->tipo != UI_EVENT_BUTTON) return;
    if (ID_SALIR == ev->id) { *fuera = 1; return; }
    if (ID_REINICIAR == ev->id) dar_flap(m);
}

static void salir(struct flappy *m)
{
    if (m->slot >= 0) appmgr_unregister(m->slot);
    ui_status_destroy(m->st);
    ui_destroy(m->g);
    psram_free(m);
}

int main(int argc, char *argv[])
{
    struct flappy *m;
    ui_event_t ev;
    int fuera = 0;

    (void)argc; (void)argv;
    printf("flappy: arranca (tid=%d)\r\n", gettid());

    m = (struct flappy *)psram_malloc(sizeof(*m));
    if (!m) exit(1);
    memset(m, 0, sizeof(*m));
    m->slot = -1;
    m->ultimo_tick = millis();

    m->g = ui_create_ex(UI_CHROME_NONE);
    if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Flappy", "/apps/flappy/app.elf", 0x9BD14F, gettid());
    appmgr_take_foreground(m->slot);

    reiniciar(m);
    pintar(m);

    while (!fuera)
    {
        while (ui_poll(m->g, &ev))
        {
            procesar(m, &ev, &fuera);
            if (fuera) break;
        }

        if (ESTADO_CORRIENDO == m->estado)
        {
            unsigned ahora = millis();
            if (ahora - m->ultimo_tick >= 30u)
            {
                tick(m);
                pintar(m);
            }
        }
        else if (!fuera)
        {
            pintar(m);
        }

        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot))
            fuera = 1;
        sleep(30);
    }

    salir(m);
    exit(0);
    return 0;
}
