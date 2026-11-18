/**
 * Tetris -- Material/gfx (sin LVGL).  Port fiel de la logica original.
 *
 *   elf /apps/tetris/app.elf
 *
 * Tablero 10x20, celdas 22px.  Gravedad, lineas, puntuacion, niveles,
 * pieza fantasma y vista previa.  Controles: swipe izq/der mover, swipe
 * abajo soft drop, swipe arriba hard drop, botones CAE (hard drop) y
 * GIRA (rotar).  Swipe vertical (desde borde o cualquier vertical segun
 * ui_quit_event) = fondo cooperativo (app sigue viva).
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

#define COLS 10
#define ROWS 20
#define CELL 22
#define ANCHO (COLS * CELL)
#define ALTO  (ROWS * CELL)
#define X_TAB 4
#define Y_TAB 46

#define PXW 64
#define PXH 48
#define PSZ 14
#define X_PREV 240
#define Y_PREV 170

#define ID_CAE   11
#define ID_ROT   13
#define ID_SALIR 12

#define ESTADO_JUEGA 0
#define ESTADO_FIN   1

#define C_FONDO   UI_BG
#define C_TABLERO UI_SURFACE
#define C_REJILLA UI_BORDER
#define C_BORDE   UI_PRIMARY
#define C_TEXTO   UI_TEXT
#define C_AMBER   UI_AMBER
#define C_ROJO    UI_RED
#define C_GRIS    UI_TEXT_DIM

#define C_I 0x2E86DE
#define C_O 0xE67E22
#define C_T 0x9B59B6
#define C_S 0x16A085
#define C_Z 0xC0392B
#define C_J 0x2471A3
#define C_L 0xE1A100

struct juego
{
    ui_input_t *g;
    ui_status_t *st;
    int slot;
    int b[COLS * ROWS];
    int ptipo, sigi, prot;
    int pcells[4][2];
    int pcol, prow;
    int score, nivel, lineas;
    int estado;
    int bajada;
    unsigned rnd;
};

static unsigned color_ficha(int tipo)
{
    switch (tipo)
    {
    case 1: return C_I; case 2: return C_O; case 3: return C_T;
    case 4: return C_S; case 5: return C_Z; case 6: return C_J;
    default: return C_L;
    }
}

static void ficha_base(int tipo, int c[4][2])
{
    switch (tipo)
    {
    case 1: c[0][0]=0;c[0][1]=0;c[1][0]=1;c[1][1]=0;c[2][0]=2;c[2][1]=0;c[3][0]=3;c[3][1]=0; break;
    case 2: c[0][0]=0;c[0][1]=0;c[1][0]=1;c[1][1]=0;c[2][0]=0;c[2][1]=1;c[3][0]=1;c[3][1]=1; break;
    case 3: c[0][0]=0;c[0][1]=1;c[1][0]=1;c[1][1]=1;c[2][0]=2;c[2][1]=1;c[3][0]=1;c[3][1]=0; break;
    case 4: c[0][0]=1;c[0][1]=0;c[1][0]=2;c[1][1]=0;c[2][0]=0;c[2][1]=1;c[3][0]=1;c[3][1]=1; break;
    case 5: c[0][0]=0;c[0][1]=0;c[1][0]=1;c[1][1]=0;c[2][0]=1;c[2][1]=1;c[3][0]=2;c[3][1]=1; break;
    case 6: c[0][0]=0;c[0][1]=0;c[1][0]=0;c[1][1]=1;c[2][0]=0;c[2][1]=2;c[3][0]=1;c[3][1]=2; break;
    default:c[0][0]=1;c[0][1]=0;c[1][0]=1;c[1][1]=1;c[2][0]=0;c[2][1]=2;c[3][0]=1;c[3][1]=2; break;
    }
}

static void rotar(int c[4][2])
{
    int nc[4][2];
    int k, minx = 99, miny = 99;
    for (k = 0; k < 4; k++)
    {
        int x = c[k][0], y = c[k][1];
        nc[k][0] = y; nc[k][1] = -x;
        if (nc[k][0] < minx) minx = nc[k][0];
        if (nc[k][1] < miny) miny = nc[k][1];
    }
    for (k = 0; k < 4; k++) { c[k][0] = nc[k][0]-minx; c[k][1] = nc[k][1]-miny; }
}

static int choca(const struct juego *m, int dx, int dy)
{
    int k;
    for (k = 0; k < 4; k++)
    {
        int x = m->pcol + m->pcells[k][0] + dx;
        int y = m->prow + m->pcells[k][1] + dy;
        if (x < 0 || x >= COLS || y >= ROWS) return 1;
        if (y >= 0 && m->b[y * COLS + x]) return 1;
    }
    return 0;
}

static int limpiar_lineas(struct juego *m)
{
    int r = ROWS - 1, n = 0;
    while (r >= 0)
    {
        int c, llena = 1, rr;
        for (c = 0; c < COLS; c++) if (0 == m->b[r * COLS + c]) { llena = 0; break; }
        if (!llena) { r--; continue; }
        for (rr = r; rr > 0; rr--)
            memcpy(&m->b[rr * COLS], &m->b[(rr - 1) * COLS], COLS * sizeof(int));
        for (c = 0; c < COLS; c++) m->b[c] = 0;
        n++;
    }
    m->lineas += n;
    return n;
}

static void dibujar(struct juego *m);

static void nueva_ficha(struct juego *m)
{
    m->rnd = m->rnd * 1103515245u + 12345u;
    m->ptipo = m->sigi;
    m->sigi = (int)((m->rnd >> 16) % 7u) + 1;
    m->prot = 0;
    ficha_base(m->ptipo, m->pcells);
    m->pcol = 3;
    m->prow = 0;
    if (choca(m, 0, 0)) m->estado = ESTADO_FIN;
}

static void fijar(struct juego *m)
{
    int k, n;
    for (k = 0; k < 4; k++)
    {
        int x = m->pcol + m->pcells[k][0];
        int y = m->prow + m->pcells[k][1];
        if (x >= 0 && x < COLS && y >= 0 && y < ROWS)
            m->b[y * COLS + x] = m->ptipo;
    }
    n = limpiar_lineas(m);
    if (n > 0) m->score += 100 * n * m->nivel;
    m->nivel = m->lineas / 10 + 1;
    nueva_ficha(m);
}

static void paso_gravedad(struct juego *m)
{
    if (choca(m, 0, 1)) fijar(m);
    else m->prow++;
}

static void bajar(struct juego *m)
{
    if (choca(m, 0, 1)) fijar(m);
    else { m->prow++; m->score += 1; }
}

static void caer(struct juego *m)
{
    int d = 0;
    while (!choca(m, 0, 1)) { m->prow++; d++; }
    m->score += 2 * d;
    fijar(m);
}

static void mover(struct juego *m, int dx)
{
    if (!choca(m, dx, 0)) m->pcol += dx;
}

static void girar(struct juego *m)
{
    int guard[4][2];
    memcpy(guard, m->pcells, sizeof(guard));
    rotar(m->pcells);
    if (choca(m, 0, 0)) { memcpy(m->pcells, guard, sizeof(guard)); return; }
    m->prot = (m->prot + 1) % 4;
}

static int gravedad_ms(const struct juego *m)
{
    int ms = 700 - (m->nivel - 1) * 60;
    if (ms < 80) ms = 80;
    return ms;
}

/* ---- pintura ---- */

static void celda_draw(int x, int y, int size, unsigned color)
{
    ui_rrect(x, y, size, size, 3, color);
}

static void dibujar(struct juego *m)
{
    int r, c, k;

    ui_buttons_clear(m->g);
    ui_fill(C_FONDO);
    if (m->st) ui_status_draw(m->st);

    /* tablero */
    ui_rect(X_TAB, Y_TAB, ANCHO, ALTO, C_TABLERO);
    for (c = 1; c < COLS; c++) ui_rect(X_TAB + c * CELL, Y_TAB, 1, ALTO, C_REJILLA);
    for (r = 1; r < ROWS; r++) ui_rect(X_TAB, Y_TAB + r * CELL, ANCHO, 1, C_REJILLA);
    ui_rect(X_TAB, Y_TAB, ANCHO, 2, C_BORDE);
    ui_rect(X_TAB, Y_TAB + ALTO - 2, ANCHO, 2, C_BORDE);
    ui_rect(X_TAB, Y_TAB, 2, ALTO, C_BORDE);
    ui_rect(X_TAB + ANCHO - 2, Y_TAB, 2, ALTO, C_BORDE);

    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
        {
            int v = m->b[r * COLS + c];
            if (v)
                celda_draw(X_TAB + c * CELL + 1, Y_TAB + r * CELL + 1, CELL - 2, color_ficha(v));
        }

    if (ESTADO_JUEGA == m->estado)
    {
        int d = 0;
        while (!choca(m, 0, d + 1)) d++;
        if (d > 0)
            for (k = 0; k < 4; k++)
            {
                int x = m->pcol + m->pcells[k][0];
                int y = m->prow + m->pcells[k][1] + d;
                if (y >= 0)
                    ui_rrect(X_TAB + x * CELL + 3, Y_TAB + y * CELL + 3, CELL - 6, CELL - 6, 2, UI_BORDER);
            }
        for (k = 0; k < 4; k++)
        {
            int x = m->pcol + m->pcells[k][0];
            int y = m->prow + m->pcells[k][1];
            if (y >= 0)
                celda_draw(X_TAB + x * CELL + 1, Y_TAB + y * CELL + 1, CELL - 2, color_ficha(m->ptipo));
        }
    }

    /* panel lateral */
    ui_text("TETRIS", 232, 52, C_AMBER, C_FONDO);
    ui_text(m->estado == ESTADO_FIN ? "FIN" : "JUEGA", 232, 72,
            m->estado == ESTADO_FIN ? C_ROJO : C_TEXTO, C_FONDO);
    {
        char s[32];
        snprintf(s, sizeof(s), "PTS %d", m->score); ui_text(s, 232, 96, C_TEXTO, C_FONDO);
        snprintf(s, sizeof(s), "NIV %d", m->nivel); ui_text(s, 232, 116, C_TEXTO, C_FONDO);
        snprintf(s, sizeof(s), "LIN %d", m->lineas); ui_text(s, 232, 136, C_TEXTO, C_FONDO);
    }
    ui_text("SIGUIENTE", 232, 156, C_GRIS, C_FONDO);
    ui_rrect(X_PREV - 2, Y_PREV - 2, PXW, PXH, 4, C_TABLERO);
    {
        int bc[4][2], bk, minx=99,maxx=-99,miny=99,maxy=-99,dx,dy,offx,offy;
        ficha_base(m->sigi, bc);
        for (bk=0; bk<4; bk++) {
            if (bc[bk][0]<minx) minx=bc[bk][0]; if (bc[bk][0]>maxx) maxx=bc[bk][0];
            if (bc[bk][1]<miny) miny=bc[bk][1]; if (bc[bk][1]>maxy) maxy=bc[bk][1];
        }
        dx = maxx-minx+1; dy = maxy-miny+1;
        offx = (PXW - dx*PSZ)/2 - minx*PSZ; offy = (PXH - dy*PSZ)/2 - miny*PSZ;
        for (bk=0; bk<4; bk++)
            celda_draw(X_PREV + offx + bc[bk][0]*PSZ + 1,
                       Y_PREV + offy + bc[bk][1]*PSZ + 1, PSZ-2, color_ficha(m->sigi));
    }

    /* botones Material */
    ui_button(m->g, ID_CAE, "v",   232, 230, 80, 50, UI_PRIMARY);
    ui_button(m->g, ID_ROT, "GIRA",232, 290, 80, 50, C_T);
    ui_button(m->g, ID_SALIR,"X",  232, 350, 80, 40, C_ROJO);
}

/* ---- partida ---- */

static void reiniciar(struct juego *m)
{
    memset(m->b, 0, sizeof(m->b));
    m->score = 0; m->nivel = 1; m->lineas = 0; m->bajada = 0;
    m->estado = ESTADO_JUEGA;
    m->rnd = 0x1234567u;
    m->rnd = m->rnd * 1103515245u + 12345u;
    m->sigi = (int)((m->rnd >> 16) % 7u) + 1;
    nueva_ficha(m);
    dibujar(m);
}

static void ir_fondo(struct juego *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);
}

static int es_quit_gesto(ui_event_t *ev)
{
    /* Salir al launcher: gesto vertical desde un borde (natural),
     * para no pisar los controles de swipe del juego en el centro. */
    return (ev->tipo == UI_EVENT_SWIPE &&
            (ev->dir == UI_DIR_UP || ev->dir == UI_DIR_DOWN) &&
            (ev->edge == UI_EDGE_BOTTOM || ev->edge == UI_EDGE_TOP));
}

static void procesar(struct juego *m, ui_event_t *ev, int *fuera)
{
    if (es_quit_gesto(ev))
    {
        ir_fondo(m, fuera);
        return;
    }
    if (ev->tipo == UI_EVENT_SWIPE)
    {
        if (ESTADO_JUEGA != m->estado) return;
        if (ev->dir == UI_DIR_LEFT)       mover(m, -1);
        else if (ev->dir == UI_DIR_RIGHT) mover(m, 1);
        else if (ev->dir == UI_DIR_DOWN)  { bajar(m); m->bajada = 0; }
        else if (ev->dir == UI_DIR_UP)    caer(m);
        dibujar(m);
        return;
    }
    if (ev->tipo != UI_EVENT_BUTTON) return;
    if (ID_SALIR == ev->id) { *fuera = 1; }
    else if (ID_CAE == ev->id) { if (ESTADO_JUEGA == m->estado) caer(m); dibujar(m); }
    else if (ID_ROT == ev->id) { if (ESTADO_JUEGA == m->estado) { girar(m); dibujar(m); } }
}

static void salir(struct juego *m)
{
    if (m->slot >= 0) appmgr_unregister(m->slot);
    ui_status_destroy(m->st);
    ui_destroy(m->g);
    psram_free(m);
}

int main(int argc, char *argv[])
{
    struct juego *m;
    ui_event_t ev;
    int fuera = 0, t_ant;

    (void)argc; (void)argv;
    printf("tetris: arranca (tid=%d)\r\n", gettid());

    m = (struct juego *)psram_malloc(sizeof(*m));
    if (!m) exit(1);
    memset(m, 0, sizeof(*m));
    m->slot = -1;

    m->g = ui_create_ex(UI_CHROME_NONE);
    if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Tetris", "/apps/tetris/app.elf", 0x8E44AD, gettid());
    appmgr_take_foreground(m->slot);

    reiniciar(m);
    t_ant = millis();

    while (!fuera)
    {
        while (ui_poll(m->g, &ev))
        {
            procesar(m, &ev, &fuera);
            if (fuera) break;
        }
        if (!fuera && ESTADO_JUEGA == m->estado)
        {
            int ahora = millis();
            int dt = ahora - t_ant;
            if (dt > 200) dt = 200;
            t_ant = ahora;
            m->bajada += dt;
            if (m->bajada >= gravedad_ms(m))
            {
                m->bajada = 0;
                paso_gravedad(m);
                dibujar(m);
            }
        }
        else
        {
            t_ant = millis();
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot))
            fuera = 1;
        if (!fuera) sleep(15);
    }

    salir(m);
    exit(0);
    return 0;
}
