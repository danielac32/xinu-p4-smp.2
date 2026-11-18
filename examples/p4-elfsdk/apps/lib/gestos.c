/**
 * @file gestos.c
 *
 * FASE 3 -- Gestos sobre TOUCH0 directo, sin LVGL.
 *
 * La app llama ui_poll() desde su propia hebra; ui_poll() lee el panel y
 * detecta press/release/swipe, apilando eventos en el anillo del handle.
 * No hay hebra lvglui, ni callbacks, ni lvgl_lock().  Todo el estado esta
 * en el handle de PSRAM (el cargador ELF no admite estaticas escribibles).
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <memory.h>
#include <device.h>
#include <touch.h>
#include <gfx.h>
#include <timer.h>                 /* millis(): sello de tiempo de gestos */
#include "gestos.h"
#include "ui.h"                    /* ui_rrect: botones estilo Material */

#define GE_LIMO     40        /* px de recorrido para considerar arrastre */
#define GE_ANILLO   8
#define GE_BOTONES  32
#define GE_LARGA_MS 600       /* pulsacion larga */

#define UI_W 320
#define UI_H 480
#define UI_EDGE_MARGIN 48

#define COLOR_ARRIBA  0x2FFF5E
#define COLOR_ABAJO   0xE8453C
#define COLOR_IZQ     0x38A5FF
#define COLOR_DER     0xFFB302
#define COLOR_REPOSO  0x7A8C9C

struct ui_button
{
    int id;
    int x, y, w, h;
    unsigned int rgb;
};

struct gestos
{
    struct ui_button botones[GE_BOTONES];
    int nbotones;
    volatile gev_t anillo[GE_ANILLO];
    volatile int cola, tope;
    int prensado;
    int x1, y1;
    int lastx, lasty;          /* ultima posicion vista con dedo abajo */
    int swipe_sent;            /* ya se apilo el swipe de este gesto */
    unsigned long t_down;
    int ultimo_id;             /* ultimo boton tocado (devuelto por ui_button) */
    int chrome;
    int arrow_dir;
    int visible;
    unsigned long arrow_until; /* ms hasta los que se muestra la flecha */
};

static int entero(int v) { return v < 0 ? -v : v; }

static int direccion(int dx, int dy)
{
    if (entero(dx) >= entero(dy))
        return dx > 0 ? UI_DIR_RIGHT : UI_DIR_LEFT;
    return dy > 0 ? UI_DIR_DOWN : UI_DIR_UP;
}

static int start_edge(int x1, int y1)
{
    if (y1 >= (UI_H - UI_EDGE_MARGIN)) return UI_EDGE_BOTTOM;
    if (y1 <= UI_EDGE_MARGIN)          return UI_EDGE_TOP;
    if (x1 <= UI_EDGE_MARGIN)          return UI_EDGE_LEFT;
    if (x1 >= (UI_W - UI_EDGE_MARGIN)) return UI_EDGE_RIGHT;
    return UI_EDGE_NONE;
}

const char *ui_dir_name(int dir)
{
    switch (dir)
    {
    case UI_DIR_UP:    return "de abajo HACIA ARRIBA";
    case UI_DIR_DOWN:  return "de arriba HACIA ABAJO";
    case UI_DIR_LEFT:  return "de derecha A LA IZQUIERDA";
    case UI_DIR_RIGHT: return "de izquierda A LA DERECHA";
    default:           return "sin arrastre";
    }
}

unsigned int ui_dir_color(int dir)
{
    switch (dir)
    {
    case UI_DIR_UP:    return COLOR_ARRIBA;
    case UI_DIR_DOWN:  return COLOR_ABAJO;
    case UI_DIR_LEFT:  return COLOR_IZQ;
    case UI_DIR_RIGHT: return COLOR_DER;
    default:           return COLOR_REPOSO;
    }
}

static void apilar(gestos_t *g, int tipo, int dir, int x1, int y1, int x2,
                   int y2, int id, int edge)
{
    int n = (g->tope + 1) & (GE_ANILLO - 1);
    volatile gev_t *e;

    if (n == g->cola) return;
    e = &g->anillo[g->tope];
    e->tipo = tipo;
    e->dir = dir;
    e->id = id;
    e->x1 = x1; e->y1 = y1;
    e->x2 = x2; e->y2 = y2;
    e->dx = x2 - x1; e->dy = y2 - y1;
    e->edge = edge;
    g->tope = n;
}

/* Dibuja una flecha simple con rectangulos sobre el panel. */
static void dibujar_flecha(gestos_t *g, int dir)
{
    struct lcd_rect r;
    int cx = UI_W / 2, cy = UI_H / 2;
    (void)g;

    r.color = ui_dir_color(dir);
    switch (dir)
    {
    case UI_DIR_UP:
        r.x = cx - 8; r.y = cy - 40; r.w = 16; r.h = 80; break;
    case UI_DIR_DOWN:
        r.x = cx - 8; r.y = cy - 40; r.w = 16; r.h = 80; break;
    case UI_DIR_LEFT:
        r.x = cx - 40; r.y = cy - 8; r.w = 80; r.h = 16; break;
    default:
        r.x = cx - 40; r.y = cy - 8; r.w = 80; r.h = 16; break;
    }
    control(GFX0, GFX_CTRL_RECT, (long)&r, 0);
}

ui_input_t *ui_create(void) { return ui_create_ex(UI_CHROME_ALL); }

ui_input_t *ui_create_ex(int chrome)
{
    gestos_t *g;

    g = (gestos_t *)psram_malloc(sizeof(gestos_t));
    if (NULL == g)
    {
        printf("ui: no hay PSRAM para el handle\r\n");
        return NULL;
    }
    memset(g, 0, sizeof(gestos_t));
    g->chrome = chrome;
    g->visible = 1;
    g->arrow_dir = UI_DIR_NONE;
    return g;
}

/* Lee el panel y genera eventos.  La llama la app desde su bucle. */
int ui_poll(ui_input_t *g, ui_event_t *ev)
{
    touch_point_t pt;
    int n;

    if (NULL == g) return 0;

    n = control(TOUCH0, TOUCH_CTRL_READ, (long)&pt, 1);
    if (n > 0)
    {
        if (!g->prensado)
        {
            g->prensado = 1;
            g->x1 = pt.x; g->y1 = pt.y;
            g->swipe_sent = 0;
            g->t_down = millis();
        }
        g->lastx = pt.x; g->lasty = pt.y;   /* seguimiento durante el arrastre */
        /* Apilar el SWIPE EN CUANTO se pasa el umbral, sin esperar a soltar:
         * asi no se pierde si el dedo se levanta entre dos muestreos. */
        if (!g->swipe_sent &&
            (entero(pt.x - g->x1) >= GE_LIMO ||
             entero(pt.y - g->y1) >= GE_LIMO))
        {
            int d = direccion(pt.x - g->x1, pt.y - g->y1);
            g->swipe_sent = 1;
            apilar(g, UI_EVENT_SWIPE, d, g->x1, g->y1,
                   pt.x, pt.y, 0, start_edge(g->x1, g->y1));
            if (g->chrome & UI_CHROME_ARROW) dibujar_flecha(g, d);
        }
    }
    else if (g->prensado)
    {
        /* La lectura devolvio 0: usar la ultima posicion vista con dedo abajo */
        int dx = g->lastx - g->x1;
        int dy = g->lasty - g->y1;
        int moved = entero(dx) >= GE_LIMO || entero(dy) >= GE_LIMO;
        g->prensado = 0;

        if (moved && !g->swipe_sent)
        {
            apilar(g, UI_EVENT_SWIPE, direccion(dx, dy), g->x1, g->y1,
                   pt.x, pt.y, 0, start_edge(g->x1, g->y1));
        }
        else if (!moved && millis() - g->t_down >= GE_LARGA_MS)
        {
            apilar(g, UI_EVENT_LONG_PRESS, UI_DIR_NONE, g->x1, g->y1,
                   pt.x, pt.y, 0, UI_EDGE_NONE);
        }
        else if (!moved)
        {
            /* Tap: si cae dentro de un boton recordado, llevar su id. */
            int bid = 0;
            int i;
            for (i = 0; i < g->nbotones; i++)
            {
                struct ui_button *b = &g->botones[i];
                if (g->x1 >= b->x && g->x1 < b->x + b->w &&
                    g->y1 >= b->y && g->y1 < b->y + b->h)
                {
                    bid = b->id;
                    break;
                }
            }
            apilar(g, UI_EVENT_BUTTON, UI_DIR_NONE, g->x1, g->y1,
                   pt.x, pt.y, bid, UI_EDGE_NONE);
        }
        g->arrow_dir = UI_DIR_NONE;
    }

    /* Entregar un evento pendiente */
    if (g->cola == g->tope) return 0;
    {
        volatile gev_t *e = &g->anillo[g->cola];
        ev->tipo = e->tipo; ev->dir = e->dir; ev->id = e->id;
        ev->x1 = e->x1; ev->y1 = e->y1; ev->x2 = e->x2; ev->y2 = e->y2;
        ev->dx = e->dx; ev->dy = e->dy; ev->edge = e->edge;
        g->cola = (g->cola + 1) & (GE_ANILLO - 1);
    }
    return 1;
}

void ui_clear_events(gestos_t *g)
{
    if (NULL == g) return;
    g->cola = g->tope;
    g->prensado = 0;
    g->swipe_sent = 0;
    g->arrow_dir = UI_DIR_NONE;
}

int ui_button(gestos_t *g, int id, const char *label, int x, int y,
              int w, int h, unsigned int rgb)
{
    struct lcd_rect r;
    if (NULL == g) return SYSERR;
    if (g->nbotones < GE_BOTONES)
    {
        struct ui_button *b = &g->botones[g->nbotones++];
        b->id = id; b->x = x; b->y = y; b->w = w; b->h = h; b->rgb = rgb;
    }
    /* Dibujo Material: rectangulo redondeado + texto centrado claro. */
    {
        int rr = 6;
        ui_rrect(x, y, w, h, rr, rgb);
    }
    if (label && label[0])
    {
        struct lcd_text t;
        int n = (int)strlen(label);
        t.s = label; t.x = x + (w - n * 8) / 2; t.y = y + h / 2 - 8;
        t.fg = 0xFFFF; t.bg = rgb;
        control(GFX0, GFX_CTRL_TEXT, (long)&t, 0);
    }
    g->ultimo_id = id;
    return id;
}

void ui_buttons_clear(gestos_t *g)
{
    if (NULL == g) return;
    g->nbotones = 0;
}

void ui_show(gestos_t *g, int on)
{
    if (NULL == g) return;
    g->visible = on;
}

void ui_destroy(gestos_t *g)
{
    if (NULL == g) return;
    psram_free(g);
}
