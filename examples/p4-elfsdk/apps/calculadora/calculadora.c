/**
 * @file calculadora.c
 *
 * FASE 3 -- Calculadora migrada a gfx/ui (sin LVGL).  Logica de float
 * exactamente igual a la version original; solo cambia el dibujo.
 * Gesto de salir: swipe abajo (ui_quit_event) -> exit al launcher.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <timer.h>
#include <memory.h>
#include <device.h>
#include <gfx.h>
#include <appmgr.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);
extern float floorf(float x);
#define FLT_MAXF 3.4028235e38f

#define T_0 0
#define T_1 1
#define T_2 2
#define T_3 3
#define T_4 4
#define T_5 5
#define T_6 6
#define T_7 7
#define T_8 8
#define T_9 9
#define T_MAS 10
#define T_MENOS 11
#define T_POR 12
#define T_DIV 13
#define T_IGUAL 14
#define T_CLR 15
#define T_SIGNO 16
#define T_BORRA 17
#define T_DP 19

#define OP_NINGUNA 0
#define OP_MAS 1
#define OP_MENOS 2
#define OP_POR 3
#define OP_DIV 4

/* Material oscuro: grises neutros + un solo acento azul, sin rojo/naranja */
#define C_NUM   0x318C     /* gris superficie            */
#define C_OP    0x2D7F     /* azul acento                */
#define C_ACC   0x2D7F     /* azul (igual)               */
#define C_CLR   0x4208     /* gris oscuro                */
#define C_PANT  0x0810     /* casi negro                 */

struct tecla { const char *t; int id; unsigned c; };

struct calc
{
    ui_input_t *g;
    ui_status_t *status;
    float acum;
    int op, nuevo, error, neg, slot;
    char entrada[24], ebuf[48];
    unsigned disp_color;
};

/* ---- logica (identica a la original) ---- */
static int mal(float r) { return !(r == r) || r > FLT_MAXF || r < -FLT_MAXF; }

static void fmt_float(char *out, size_t n, float v)
{
    char ibuf[24], fbuf[12];
    int neg, big, e = 0, ip, q;
    float a, frac;

    if (mal(v)) { snprintf(out, n, "ERR"); return; }
    neg = (v < 0.0f); a = neg ? -v : v;
    big = (a >= 1e9f) || (a != 0.0f && a < 1e-6f);
    if (big) {
        while (a >= 10.0f) { a /= 10.0f; e++; }
        while (a < 1.0f && a != 0.0f) { a *= 10.0f; e--; }
    }
    ip = (int)floorf(a); frac = a - (float)ip;
    q = (int)(frac * 1000000.0f + 0.5f);
    if (q >= 1000000) { q = 0; ip += 1; if (ip >= 10 && big) { ip = 1; e++; } }
    if (ip >= 1000000000) { ip = 999999999; q = 0; }
    snprintf(ibuf, sizeof(ibuf), "%d", ip);
    snprintf(fbuf, sizeof(fbuf), "%06d", q);
    {
        size_t l = strlen(fbuf);
        while (l > 0 && '0' == fbuf[l - 1]) fbuf[--l] = '\0';
    }
    if (big) {
        if (fbuf[0]) snprintf(out, n, "%s%s.%se%+d", neg?"-":"", ibuf, fbuf, e);
        else snprintf(out, n, "%s%se%+d", neg?"-":"", ibuf, e);
    } else {
        if (fbuf[0]) snprintf(out, n, "%s%s.%s", neg?"-":"", ibuf, fbuf);
        else snprintf(out, n, "%s%s", neg?"-":"", ibuf);
    }
}

static float parse_entrada(const struct calc *c)
{
    const char *s = c->entrada;
    float ip = 0.0f, fp = 0.0f, esc = 0.1f;
    int punto = 0;
    for (; *s; s++) {
        if ('.' == *s) { punto = 1; continue; }
        if (punto) { fp += (float)(*s - '0') * esc; esc *= 0.1f; }
        else ip = ip * 10.0f + (float)(*s - '0');
    }
    { float r = ip + fp; return c->neg ? -r : r; }
}

static float valor_actual(const struct calc *c)
{
    return c->nuevo ? c->acum : parse_entrada(c);
}

static float binop(float a, float b, int op)
{
    if (OP_MAS == op) return a + b;
    if (OP_MENOS == op) return a - b;
    if (OP_POR == op) return a * b;
    return a / b;
}

static void mostrar(struct calc *c)
{
    char buf[40];
    if (c->error) snprintf(buf, sizeof(buf), "ERR");
    else if (c->nuevo) fmt_float(buf, sizeof(buf), c->acum);
    else if ('\0' == c->entrada[0]) snprintf(buf, sizeof(buf), "%s0", c->neg?"-":"");
    else snprintf(buf, sizeof(buf), "%s%s", c->neg?"-":"", c->entrada);

    /* Panel de numeros: fondo + expresion (y=60) + resultado (y=84).
     * 24px de separacion > 16px de altura del glyph: no se solapan. */
    ui_rrect(8, 54, 304, 58, 6, C_PANT);
    {
        int ex = 300 - (int)strlen(c->ebuf) * 8;
        int nx = 300 - (int)strlen(buf) * 8;
        if (ex < 12) ex = 12;
        if (nx < 12) nx = 12;
        ui_text(c->ebuf, ex, 60, UI_TEXT_DIM, C_PANT);
        ui_text(buf, nx, 84, UI_TEXT, C_PANT);
    }
}

static void poner_expr(struct calc *c, const char *sop)
{
    char num[24];
    fmt_float(num, sizeof(num), c->acum);
    snprintf(c->ebuf, sizeof(c->ebuf), "%s %s", num, sop);
}

static void limpiar_todo(struct calc *c)
{
    c->acum = 0.0f; c->op = OP_NINGUNA; c->nuevo = 1;
    c->error = 0; c->neg = 0; c->entrada[0] = '\0'; c->ebuf[0] = '\0';
}

static void ejecutar_pendiente(struct calc *c)
{
    float v = valor_actual(c), r;
    if (OP_NINGUNA == c->op) { c->acum = v; return; }
    if (OP_DIV == c->op && 0.0f == v) { c->error = 1; mostrar(c); return; }
    r = binop(c->acum, v, c->op);
    if (mal(r)) { c->error = 1; mostrar(c); return; }
    c->acum = r;
}

static void aplicar_operador(struct calc *c, int op, const char *sop)
{
    if (c->error) return;
    ejecutar_pendiente(c);
    if (c->error) return;
    c->op = op; c->nuevo = 1; c->neg = 0; c->entrada[0] = '\0';
    poner_expr(c, sop); mostrar(c);
}

static void tecla_igual(struct calc *c)
{
    if (c->error || OP_NINGUNA == c->op) return;
    ejecutar_pendiente(c);
    if (c->error) return;
    c->op = OP_NINGUNA; c->nuevo = 1; c->neg = 0;
    c->entrada[0] = '\0'; c->ebuf[0] = '\0'; mostrar(c);
}

static void tecla_digito(struct calc *c, int d)
{
    size_t l;
    if (c->error) return;
    if (c->nuevo) { c->entrada[0] = '\0'; c->neg = 0; c->nuevo = 0; }
    l = strlen(c->entrada);
    if (l >= sizeof(c->entrada) - 2) return;
    if (1 == l && '0' == c->entrada[0] && NULL == strchr(c->entrada, '.')) {
        c->entrada[0] = (char)('0' + d); c->entrada[1] = '\0';
    } else {
        c->entrada[l] = (char)('0' + d); c->entrada[l + 1] = '\0';
    }
    mostrar(c);
}

static void tecla_decimal(struct calc *c)
{
    size_t l;
    if (c->error) return;
    if (c->nuevo) { c->entrada[0]='0'; c->entrada[1]='\0'; c->nuevo=0; }
    if (strchr(c->entrada, '.')) return;
    if (strlen(c->entrada) >= sizeof(c->entrada) - 2) return;
    l = strlen(c->entrada); c->entrada[l] = '.'; c->entrada[l+1] = '\0';
    mostrar(c);
}

static void tecla_signo(struct calc *c)
{
    if (c->error) return;
    if (c->nuevo) c->acum = -c->acum;
    else c->neg = !c->neg;
    mostrar(c);
}

static void tecla_borra(struct calc *c)
{
    size_t l;
    if (c->error || c->nuevo) return;
    l = strlen(c->entrada);
    if (0 == l) return;
    c->entrada[l-1] = '\0';
    if ('\0' == c->entrada[0]) c->neg = 0;
    mostrar(c);
}

/* ---- UI ---- */
static const struct tecla keys[20] = {
    {"C",T_CLR,C_CLR}, {"+/-",T_SIGNO,C_OP}, {".",T_DP,C_NUM}, {"/",T_DIV,C_OP},
    {"7",T_7,C_NUM}, {"8",T_8,C_NUM}, {"9",T_9,C_NUM}, {"x",T_POR,C_OP},
    {"4",T_4,C_NUM}, {"5",T_5,C_NUM}, {"6",T_6,C_NUM}, {"-",T_MENOS,C_OP},
    {"1",T_1,C_NUM}, {"2",T_2,C_NUM}, {"3",T_3,C_NUM}, {"+",T_MAS,C_OP},
    {"0",T_0,C_NUM}, {"<-",T_BORRA,C_OP}, {"=",T_IGUAL,C_ACC}, {"",0,0}
};

static void crear_ui(struct calc *c)
{
    int i;
    ui_buttons_clear(c->g);          /* limpiar areas tactiles anteriores */
    ui_fill(UI_BG);
    if (c->status) ui_status_draw(c->status);
    else ui_text("Calculadora", 12, 8, UI_TEXT, UI_BG);

    for (i = 0; i < 19; i++) {
        int col = i % 4, row = i / 4;
        int x = 8 + col * 77, y = 112 + row * 68;
        ui_shadow(x, y, 70, 58, 6);
        /* ui_button dibuja Y registra el area tactil (antes solo se dibujaba,
         * por eso las teclas no respondian: ui_poll no conocia su id). */
        ui_button(c->g, keys[i].id, keys[i].t, x, y, 70, 58, keys[i].c);
    }
    ui_text_center("swipe v salir", GFX_W / 2, 460, UI_TEXT_DIM, UI_BG);
    mostrar(c);
}

static void procesar(struct calc *c, ui_event_t *ev, int *fuera)
{
    if (UI_EVENT_BUTTON != ev->tipo) return;
    switch (ev->id) {
    case T_0: case T_1: case T_2: case T_3: case T_4:
    case T_5: case T_6: case T_7: case T_8: case T_9:
        tecla_digito(c, ev->id); break;
    case T_MAS: aplicar_operador(c, OP_MAS, "+"); break;
    case T_MENOS: aplicar_operador(c, OP_MENOS, "-"); break;
    case T_POR: aplicar_operador(c, OP_POR, "x"); break;
    case T_DIV: aplicar_operador(c, OP_DIV, "/"); break;
    case T_IGUAL: tecla_igual(c); break;
    case T_CLR: limpiar_todo(c); mostrar(c); break;
    case T_SIGNO: tecla_signo(c); break;
    case T_BORRA: tecla_borra(c); break;
    case T_DP: tecla_decimal(c); break;
    default: break;
    }
}

int main(int argc, char *argv[])
{
    struct calc *c;
    ui_event_t ev;
    int fuera = 0, pantalla = 0;

    (void)argc; (void)argv;
    printf("calculadora: gfx (tid=%d)\r\n", gettid());
    c = (struct calc *)psram_malloc(sizeof(struct calc));
    if (NULL == c) exit(1);
    memset(c, 0, sizeof(struct calc));
    c->slot = SYSERR;
    c->g = ui_create_ex(UI_CHROME_NONE);
    if (NULL == c->g) { psram_free(c); exit(1); }
    c->status = ui_status_create();
    c->slot = appmgr_register("Calculadora", "/apps/calculadora/app.elf", 0x5DADE2, gettid());
    appmgr_take_foreground(c->slot);   /* quitar el foco al launcher */
    crear_ui(c);

    while (!fuera) {
        while (ui_poll(c->g, &ev)) {
            if (ui_quit_event(&ev)) {
                /* Ir a segundo plano: la app sigue viva; el launcher la muestra
                 * en Recientes con X para matarla. */
                if (ui_background(c->slot)) { fuera = 1; break; }
                pantalla = 1;   /* reanudada: repintar */
            } else {
                procesar(c, &ev, &fuera);
            }
            if (fuera) break;
        }
        if (pantalla) { crear_ui(c); pantalla = 0; }
        if (!fuera && c->slot >= 0 && APP_CLOSING == appmgr_get_state(c->slot)) fuera = 1;
        if (!fuera) sleep(20);
    }
    appmgr_unregister(c->slot);
    ui_status_destroy(c->status);
    ui_destroy(c->g);
    psram_free(c);
    exit(0);
    return 0;
}
