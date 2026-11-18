/**
 * Tres en raya -- Material/gfx (sin LVGL).  Dos jugadores en el pad.
 *
 *   elf /apps/tictac/app.elf
 *
 * El tablero son nueve zonas de toque (ids 1..9) con el fondo claro de la
 * casilla.  Las fichas NO son texto: la X se dibuja con dos lineas y la O con
 * un anillo (ui_line / ui_circle).  Las lineas y el anillo se dibujan sobre el
 * fondo de la casilla, que es un rectangulo redondeado (ui_rrect).
 *
 * Dos jugadores en el mismo pad: X empieza y cada toque en una casilla libre
 * pone la ficha del turno.  Gana quien alinee tres; si se llena el tablero sin
 * tres en raya, empate.  Al ganar, una raya cruza las tres fichas de la linea.
 *
 * El estado vive en PSRAM (struct colgado del handle).  El bucle saca eventos
 * con ui_poll() y solo llama a exit().
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

#define FX 1                     /* ficha X, jugador 1 (empieza)                 */
#define FO 2                     /* ficha O, jugador 2                           */

#define ANCHO 320
#define CELDA 84                 /* lado de cada casilla                         */
#define PASO  92                 /* casilla + 8 px de hueco                      */
#define X0    26
#define Y0    92

#define ID_CELDA 1               /* ids 1..9: las nueve casillas                 */
#define ID_NUEVA 10
#define ID_SALIR 11

#define X_SALIR   280
#define Y_SALIR   50
#define AN_SALIR  40
#define AL_SALIR  32
#define X_NUEVA   60
#define Y_NUEVA   350
#define AN_NUEVA  200
#define AL_NUEVA  40

/* Material dark + semantica del juego */
#define C_TEXTO   UI_TEXT
#define C_AMBER   UI_AMBER
#define C_CELDA   UI_SURFACE2
#define C_X       UI_PRIMARY
#define C_O       UI_AMBER
#define C_VERDE   UI_GREEN
#define C_ROJO    UI_RED
#define C_FONDO   UI_BG

struct juego
{
    ui_input_t *g;
    ui_status_t *st;
    int slot;
    int b[9];                    /* 0 vacia, FX = X, FO = O                     */
    int turno;                   /* FX o FO                                     */
    int fin;                     /* 0 jugando; FX/FO ganador; 3 tablas          */
    int vx, vo, ve;              /* marcador acumulado de la sesion             */
    int idx[3];                  /* lineas ganadoras para pintar la raya       */
};

/* ---- reglas --------------------------------------------------------------- */

static int linea_ganadora(const int *b, int idx[3])
{
    int i;

    for (i = 0; i < 3; i++)
    {
        if (b[3 * i] && b[3 * i] == b[3 * i + 1] && b[3 * i] == b[3 * i + 2])
        {
            idx[0] = 3 * i; idx[1] = 3 * i + 1; idx[2] = 3 * i + 2;
            return 1;
        }
        if (b[i] && b[i] == b[i + 3] && b[i] == b[i + 6])
        {
            idx[0] = i; idx[1] = i + 3; idx[2] = i + 6;
            return 1;
        }
    }
    if (b[0] && b[0] == b[4] && b[0] == b[8]) { idx[0]=0; idx[1]=4; idx[2]=8; return 1; }
    if (b[2] && b[2] == b[4] && b[2] == b[6]) { idx[0]=2; idx[1]=4; idx[2]=6; return 1; }
    return 0;
}

static int lleno(const int *b)
{
    int i;

    for (i = 0; i < 9; i++)
    {
        if (0 == b[i]) return 0;
    }
    return 1;
}

/* ---- pintura -------------------------------------------------------------- */

static void pintar_raya(struct juego *m, const int *idx)
{
    int x1 = X0 + (idx[0] % 3) * PASO + CELDA / 2;
    int y1 = Y0 + (idx[0] / 3) * PASO + CELDA / 2;
    int x2 = X0 + (idx[2] % 3) * PASO + CELDA / 2;
    int y2 = Y0 + (idx[2] / 3) * PASO + CELDA / 2;

    if (x1 == x2) { y1 -= 16; y2 += 16; }
    else if (y1 == y2) { x1 -= 16; x2 += 16; }
    else { x1 -= 11; y1 -= 11; x2 += 11; y2 += 11; }

    ui_line(x1, y1, x2, y2, C_TEXTO);
}

static void dibujar(struct juego *m)
{
    int i;
    int rr = 8;

    ui_buttons_clear(m->g);
    ui_fill(C_FONDO);
    if (m->st) ui_status_draw(m->st);
    ui_text("TRES EN RAYA", 12, 52, C_TEXTO, C_FONDO);

    if (m->fin == FX)
        ui_text("GANA X", 12, 72, C_VERDE, C_FONDO);
    else if (m->fin == FO)
        ui_text("GANA O", 12, 72, C_VERDE, C_FONDO);
    else if (m->fin == 3)
        ui_text("EMPATE", 12, 72, C_AMBER, C_FONDO);
    else
        ui_text(m->turno == FX ? "TURNO X" : "TURNO O",
               12, 72, (m->turno == FX) ? C_X : C_O, C_FONDO);

    /* Celdas: registrar zonas tactiles y dibujar base */
    for (i = 0; i < 9; i++)
    {
        int x = X0 + (i % 3) * PASO;
        int y = Y0 + (i / 3) * PASO;
        ui_button(m->g, ID_CELDA + i, "", x, y, CELDA, CELDA, C_CELDA);
        if (m->b[i] == FX)
        {
            int cx = x + CELDA / 2, cy = y + CELDA / 2;
            ui_line(cx - 20, cy - 20, cx + 20, cy + 20, C_X);
            ui_line(cx + 20, cy - 20, cx - 20, cy + 20, C_X);
        }
        else if (m->b[i] == FO)
        {
            int cx = x + CELDA / 2, cy = y + CELDA / 2;
            ui_circle(cx, cy, 20, C_O, 0);
        }
    }

    /* Raya de la victoria */
    if (m->fin && linea_ganadora(m->b, m->idx))
    {
        pintar_raya(m, m->idx);
    }

    /* Marcador */
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "X %d  O %d  EMPATES %d", m->vx, m->vo, m->ve);
        ui_text(buf, 20, 412, C_TEXTO, C_FONDO);
    }

    /* Botones */
    ui_button(m->g, ID_NUEVA, "OTRA PARTIDA", X_NUEVA, Y_NUEVA,
             AN_NUEVA, AL_NUEVA, C_AMBER);
    ui_button(m->g, ID_SALIR, "X", X_SALIR, Y_SALIR,
             AN_SALIR, AL_SALIR, C_ROJO);
}

/* ---- maquina de juego ------------------------------------------------------ */

static void reiniciar(struct juego *m)
{
    memset(m->b, 0, sizeof(m->b));
    m->turno = FX;
    m->fin = 0;
    dibujar(m);
}

/* Cierra la partida si ya hay tres en raya o tablero lleno.  1 = terminada. */
static int cerrar(struct juego *m)
{
    int idx[3];

    if (linea_ganadora(m->b, idx))
    {
        int g = m->b[idx[0]];
        m->fin = g;
        if (FX == g) m->vx++;
        else         m->vo++;
        return 1;
    }
    if (lleno(m->b))
    {
        m->fin = 3;
        m->ve++;
        return 1;
    }
    return 0;
}

static void jugar(struct juego *m, int i)
{
    if (m->fin || m->b[i]) return;

    m->b[i] = m->turno;
    if (!cerrar(m))
    {
        m->turno = (FX == m->turno) ? FO : FX;
    }
    dibujar(m);
}

/* ---- salida ---------------------------------------------------------------- */

static void ir_fondo(struct juego *m, int *fuera);

static void procesar(struct juego *m, ui_event_t *ev, int *fuera)
{
    if (ui_quit_event(ev))
    {
        ir_fondo(m, fuera);
        return;
    }
    /* Solo interesan los botones: un toque en el fondo o un arrastre no mueve
     * ficha (el arrastre se sigue midiendo para no romper el gesto comun). */
    if (ev->tipo != UI_EVENT_BUTTON) return;

    if (ev->id >= ID_CELDA && ev->id < ID_CELDA + 9)
    {
        jugar(m, ev->id - ID_CELDA);
    }
    else if (ID_NUEVA == ev->id)
    {
        reiniciar(m);
    }
    else if (ID_SALIR == ev->id)
    {
        *fuera = 1;
    }
}

static void salir(struct juego *m)
{
    if (m->slot >= 0) appmgr_unregister(m->slot);
    ui_status_destroy(m->st);
    ui_destroy(m->g);
    psram_free(m);
}

static void ir_fondo(struct juego *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    if (ui_background(m->slot)) { *fuera = 1; return; }
    dibujar(m);  /* reanudada: repintar (gfx inmediato, sin retencion) */
}

/* ---- arranque ------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    struct juego *m;
    ui_event_t ev;
    int fuera = 0;

    (void)argc;
    (void)argv;
    printf("tictac: arranca (tid=%d)\r\n", gettid());

    m = (struct juego *)psram_malloc(sizeof(*m));
    if (NULL == m) { exit(1); }
    memset(m, 0, sizeof(*m));
    m->g = ui_create_ex(UI_CHROME_NONE);
    if (NULL == m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("TicTac", "/apps/tictac/app.elf", 0xE67E22, gettid());
    appmgr_take_foreground(m->slot);

    reiniciar(m);

    while (!fuera)
    {
        while (ui_poll(m->g, &ev))
        {
            procesar(m, &ev, &fuera);
            if (fuera) break;
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot))
        {
            fuera = 1;
        }
        if (!fuera) sleep(20);
    }

    salir(m);
    exit(0);
    return 0;
}
