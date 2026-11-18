/**
 * @file monitor.c
 *
 * Monitor de tareas para el P4 — diseno "Material" (tipo Android).
 *
 *   elf /apps/tareas/app.elf
 *
 * Lista de tareas con tarjetas elevadas, chip de estado coloreado y barra de
 * uso de CPU real (lv_bar).  App bar con titulo, barra de estado reutilizada
 * (estado.c) y navegacion por gestos: TAP abre el detalle ("activity") con
 * animacion fade; swipe arriba desde el borde inferior vuelve a la lista, y
 * otro swipe sale al launcher.  El boton MATAR requiere doble toque.
 *
 * CPU% fiel: se lee `cpu_cycles[]` (ciclos mtime acumulados en cada ctxsw,
 * ver system/resched.c) con resolucion de ciclo, no el cpu_ticks en ms de
 * taskmon (que redondea a 0 las tareas I/O-bound).  Ventana de 1s.
 *
 * La ABI de la libreria de gestos (ui_*), appmgr y taskmon no cambia: solo
 * se moderniza la capa de pintado.  Nada de estaticas escribibles; estado en
 * PSRAM; los callbacks de LVGL solo corren en lvglui; exit() solo en main.
 */

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <timer.h>
#include <memory.h>
#include <device.h>
#include <thread.h>
#include <lvgl.h>
#include <appmgr.h>
#include <taskmon.h>
#include "gestos.h"

void exit(int codigo);

LV_FONT_DECLARE(lv_font_unscii_8);
LV_FONT_DECLARE(lv_font_unscii_16);

/* ---- tema Material You (oscuro, primary teal) --------------------------- */
#define M_BG        0x121212UL   /* superficie base                        */
#define M_SURFACE   0x1E1E1EUL   /* tarjeta                                */
#define M_SURFACE2  0x2C2C2CUL   /* tarjeta seleccionada / elevada         */
#define M_PRIMARY   0x26A69AUL   /* acento teal                            */
#define M_ONPRIM    0xFFFFFFUL
#define M_TEXT      0xE6E6E6UL
#define M_TEXT2     0x9AA0A6UL
#define M_OUTLINE   0x3C4043UL
#define M_RED       0xEF5350UL
#define M_GREEN     0x66BB6AUL
#define M_AMBER     0xFFCA28UL
#define M_BLUE      0x42A5F5UL

/* ---- layout -------------------------------------------------------------- */
#define CARD_X     8
#define CARD_Y     74
/* Ocupa todo el ancho.  El toque se arregla quitando CLICKABLE a la tarjeta
 * (mas EVENT_BUBBLE en el contenedor), no estrechando la tarjeta. */
#define CARD_W     304
#define CARD_H     40
#define CARD_GAP   3
#define ROW_H      (CARD_H + CARD_GAP)
#define MAX_ROWS   40
#define LIST_BOTTOM 452

#define ID_SALIR   30
#define ID_PAUSA   31
#define ID_MATAR   32
#define ID_CONFIRM 33

#define P_LISTA    0
#define P_DETALLE  1

struct tareas_app
{
    ui_input_t *g;
    lv_obj_t *lista;            /* contenedor scrollable de tarjetas        */
    lv_obj_t *pie;              /* pie con el conteo                         */
    lv_obj_t *detalle;          /* panel "activity" del detalle             */
    lv_obj_t *det_nom;          /* titulo del detalle                        */
    lv_obj_t *det_txt;          /* datos del detalle                         */
    lv_obj_t *det_bar;          /* barra de CPU del detalle (rectangulo)     */
    lv_obj_t *btn_matar;
    lv_obj_t *btn_confirm;

    struct taskmon_row filas[MAX_ROWS];
    int n_filas;
    int pantalla;
    int sel_tid;
    int confirm_tid;            /* tid pendiente de confirmacion de MATAR   */

    /* medición de CPU por ciclos mtime (cpu_cycles[] de resched.c) */
    unsigned long long cyc_prev[MAX_ROWS];
    int tid_prev[MAX_ROWS];
    unsigned long long mt_prev;
    int n_prev;

    unsigned long ms_ultimo;        /* millis() del ultimo refresco         */
    int pausa;
    int slot;
};

/* cpu_cycles[] se acumula en resched.c (ciclos mtime por ctxsw) */
extern volatile unsigned long long cpu_cycles[];
extern unsigned long long mtime64_public(void);

/* ---- utilidades ---------------------------------------------------------- */

/* lv_obj_set_style_pad_all no esta en la ABI exportada; los cuatro lados
 * individuales si.  lv_obj_center tampoco: se usa lv_obj_align(CENTER). */
static void pad_all(lv_obj_t *o, lv_coord_t v)
{
    lv_obj_set_style_pad_left(o, v, 0);
    lv_obj_set_style_pad_right(o, v, 0);
    lv_obj_set_style_pad_top(o, v, 0);
    lv_obj_set_style_pad_bottom(o, v, 0);
}

static unsigned color_estado(int s)
{
    switch (s)
    {
    case 1: return M_PRIMARY;   /* curr */
    case 3: return M_GREEN;     /* ready */
    case 4: return M_BLUE;      /* recv */
    case 5: return M_AMBER;     /* sleep */
    case 6: return M_TEXT2;     /* susp */
    case 7: return M_OUTLINE;   /* wait */
    default: return M_TEXT2;
    }
}

static const char *estado_corto(int s)
{
    static const char *const n[] = { "curr", "free", "ready", "recv",
                                     "sleep", "susp", "wait", "rtim", "migr" };
    return (s >= 1 && s <= 9) ? n[s - 1] : "???";
}

static const char *estado_largo(int s)
{
    static const char *const n[] = {
        "curr (en CPU)", "free", "ready (lista)", "recv (esperando mensaje)",
        "sleep", "susp", "wait (semaphore)", "rtim (timeout)", "migr"
    };
    return (s >= 1 && s <= 9) ? n[s - 1] : "???";
}

/* % de CPU en una ventana desde la ultima muestra, usando ciclos mtime.
 * cpu_cycles[] tiene resolucion de ciclo (a diferencia de cpu_ticks en ms). */
static int cpu_pct(struct tareas_app *t, int idx, int tid,
                   unsigned long long mt_now)
{
    int k, j = -1;
    unsigned long long used, win;

    for (k = 0; k < t->n_prev; k++)
    {
        if (t->tid_prev[k] == tid) { j = k; break; }
    }
    if (j < 0 || t->mt_prev == 0ULL || mt_now <= t->mt_prev)
    {
        return 0;
    }
    used = cpu_cycles[tid] - t->cyc_prev[j];
    win = mt_now - t->mt_prev;
    if (win == 0ULL) return 0;
    if (used > win) used = win;
    /* Evitar division de 64 bits (__udivdi3 no se enlaza en este target):
     * en una ventana de 1s, win ~ 320e6 y used <= win, ambos caben en 32
     * bits.  Hacemos el cast explicito antes de dividir para que GCC no
     * genere __udivdi3. */
    {
        uint32_t w32 = (uint32_t)win;
        uint32_t u32 = (uint32_t)used;
        uint32_t den = w32 / 100U;
        uint32_t p;
        if (den == 0U) return (used == 0ULL) ? 0 : 100;
        p = u32 / den;
        return (int)(p > 100U ? 100U : p);
    }
}

static void guardar_muestra(struct tareas_app *t, int n,
                            unsigned long long mt_now)
{
    int i;
    for (i = 0; i < n; i++)
    {
        t->tid_prev[i] = t->filas[i].tid;
        t->cyc_prev[i] = cpu_cycles[t->filas[i].tid];
    }
    t->n_prev = n;
    t->mt_prev = mt_now;
}

/* ---- lista de tarjetas --------------------------------------------------- */

static void pintar(struct tareas_app *t)
{
    struct taskmon_row filas[MAX_ROWS];
    int n, i, elfs = 0;
    char nombre[48];
    unsigned long long mt_now = mtime64_public();

    n = taskmon_get(filas, MAX_ROWS);
    memcpy(t->filas, filas, sizeof(filas));
    t->n_filas = n;

    lvgl_lock();
    lv_obj_clean(t->lista);

    for (i = 0; i < n; i++)
    {
        struct taskmon_row *f = &t->filas[i];
        int pct;
        lv_obj_t *card, *chip, *nom, *bar, *pct_lbl;
        lv_color_t col;

        if (f->elf_task) elfs++;
        pct = cpu_pct(t, i, f->tid, mt_now);
        if (pct > 100) pct = 100;

        /* tarjeta elevada */
        card = lv_obj_create(t->lista);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, 0, i * ROW_H);
        lv_obj_set_style_bg_color(card,
            lv_color_hex((t->sel_tid == f->tid) ? M_SURFACE2 : M_SURFACE), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        pad_all(card, 6);
        lv_obj_set_style_shadow_width(card, 6, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);  /* el TAP burbujea al fondo */

        /* chip de estado (pastilla) */
        chip = lv_obj_create(card);
        lv_obj_set_size(chip, 44, 16);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 0, 0);
        col = lv_color_hex(color_estado(f->state));
        lv_obj_set_style_bg_color(chip, col, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        pad_all(chip, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        {
            lv_obj_t *clbl = lv_label_create(chip);
            lv_obj_set_style_text_font(clbl, &lv_font_unscii_8, 0);
            lv_obj_set_style_text_color(clbl, lv_color_hex(M_ONPRIM), 0);
            lv_obj_align(clbl, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_text(clbl, estado_corto(f->state));
        }

        /* nombre + tid + nucleo */
        snprintf(nombre, sizeof(nombre), "%2d %s C%d",
                 f->tid, f->name, f->cpu);
        nom = lv_label_create(card);
        lv_obj_set_style_text_font(nom, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(nom, lv_color_hex(M_TEXT), 0);
        lv_obj_align(nom, LV_ALIGN_TOP_LEFT, 50, 0);
        lv_label_set_text(nom, nombre);

        /* barra de CPU: rectangulo de ancho proporcional (lv_bar no esta en
         * la ABI con el color del indicador, asi que se dibuja a mano) */
        bar = lv_obj_create(card);
        lv_obj_set_size(bar, 150, 8);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 50, -1);
        lv_obj_set_style_bg_color(bar, lv_color_hex(M_OUTLINE), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        /* relleno proportional */
        {
            lv_obj_t *fill = lv_obj_create(bar);
            int w = (pct * 148) / 100;
            if (w < 2) w = 0;
            lv_obj_set_size(fill, w, 8);
            lv_obj_set_pos(fill, 0, 0);
            lv_obj_set_style_bg_color(fill, lv_color_hex(
                pct > 70 ? M_RED : (pct > 30 ? M_AMBER : M_PRIMARY)), 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(fill, 4, 0);
            lv_obj_set_style_border_width(fill, 0, 0);
            lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        }

        /* etiqueta de % */
        pct_lbl = lv_label_create(card);
        lv_obj_set_style_text_font(pct_lbl, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(pct_lbl, lv_color_hex(M_TEXT2), 0);
        lv_obj_align(pct_lbl, LV_ALIGN_BOTTOM_RIGHT, 0, -1);
        {
            char s[12];
            snprintf(s, sizeof(s), "%d%%", pct);
            lv_label_set_text(pct_lbl, s);
        }
    }

    {
        char s[64];
        snprintf(s, sizeof(s), "%d tareas  ·  %d app%s%s",
                 n, elfs, elfs == 1 ? "" : "s", t->pausa ? "   [PAUSA]" : "");
        lv_label_set_text(t->pie, s);
    }
    lvgl_unlock();

    guardar_muestra(t, n, mt_now);
    t->ms_ultimo = (unsigned long)millis();
}

/* ---- detalle (activity) -------------------------------------------------- */

static const struct taskmon_row *buscar_tid(const struct tareas_app *t, int tid)
{
    int i;
    for (i = 0; i < t->n_filas; i++)
    {
        if (t->filas[i].tid == tid) return &t->filas[i];
    }
    return NULL;
}

static void abrir_detalle(struct tareas_app *t, int tid)
{
    const struct taskmon_row *f = buscar_tid(t, tid);
    char txt[320];
    int pct;

    if (NULL == f) return;
    t->sel_tid = tid;
    t->pantalla = P_DETALLE;
    t->confirm_tid = -1;
    pct = cpu_pct(t, 0, tid, mtime64_public());
    /* descartar el TAP que acaba de abrir el detalle: si no, un evento
     * residual puede caer sobre el panel recien creado y dibujar raro */
    ui_clear_events(t->g);

    snprintf(txt, sizeof(txt),
             "TID       %d\n"
             "NOMBRE    %s\n"
             "ESTADO    %s\n"
             "PRIORIDAD %d\n"
             "PADRE     %d\n"
             "NUCLEO    core %d\n"
             "CPU       %d%%\n"
             "CPU acum. %lu ms\n"
             "PILA      %lu / %lu B\n"
             "TIPO      %s",
             f->tid, f->name, estado_largo(f->state),
             f->prio, f->parent, f->cpu, pct,
             f->cpu_ticks, f->stk_peak, f->stk_total,
             f->elf_task ? "APP ELF" : "kernel");

    lvgl_lock();
    if (NULL == t->detalle)
    {
        t->detalle = lv_obj_create(ui_root(t->g));
        lv_obj_set_size(t->detalle, 320, 480);
        lv_obj_set_pos(t->detalle, 0, 0);
        lv_obj_set_style_bg_color(t->detalle, lv_color_hex(M_BG), 0);
        lv_obj_set_style_bg_opa(t->detalle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(t->detalle, 0, 0);
        pad_all(t->detalle, 0);
        lv_obj_clear_flag(t->detalle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(t->detalle, LV_OBJ_FLAG_CLICKABLE);
        /* el panel debe dejar pasar los eventos al fondo de gestos, si no
         * el swipe de "volver" no llega y el detalle parece pegado/raro */
        lv_obj_add_flag(t->detalle, LV_OBJ_FLAG_EVENT_BUBBLE);

        /* app bar del detalle */
        {
            lv_obj_t *bar = lv_obj_create(t->detalle);
            lv_obj_set_size(bar, 320, 56);
            lv_obj_set_pos(bar, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(M_SURFACE2), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_radius(bar, 0, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

            t->det_nom = lv_label_create(bar);
            lv_obj_set_style_text_font(t->det_nom, &lv_font_unscii_16, 0);
            lv_obj_set_style_text_color(t->det_nom, lv_color_hex(M_ONPRIM), 0);
            lv_obj_align(t->det_nom, LV_ALIGN_LEFT_MID, 12, 0);
        }

        t->det_txt = lv_label_create(t->detalle);
        lv_obj_set_style_text_font(t->det_txt, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(t->det_txt, lv_color_hex(M_TEXT), 0);
        lv_obj_set_pos(t->det_txt, 12, 68);
        lv_obj_set_width(t->det_txt, 296);

        t->det_bar = lv_obj_create(t->detalle);
        lv_obj_set_size(t->det_bar, 296, 10);
        lv_obj_set_pos(t->det_bar, 12, 268);
        lv_obj_set_style_bg_color(t->det_bar, lv_color_hex(M_OUTLINE), 0);
        lv_obj_set_style_bg_opa(t->det_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(t->det_bar, 5, 0);
        lv_obj_set_style_border_width(t->det_bar, 0, 0);
        lv_obj_clear_flag(t->det_bar, LV_OBJ_FLAG_SCROLLABLE);

        {
            lv_obj_t *hint = lv_label_create(t->detalle);
            lv_obj_set_style_text_font(hint, &lv_font_unscii_8, 0);
            lv_obj_set_style_text_color(hint, lv_color_hex(M_TEXT2), 0);
            lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 12, -12);
            lv_label_set_text(hint, "SWIPE UP · VOLVER");
        }
    }

    lv_label_set_text(t->det_nom, f->name);
    lv_label_set_text(t->det_txt, txt);
    /* relleno proporcional de la barra del detalle */
    {
        int w = ((pct > 100 ? 100 : pct) * 292) / 100;
        lv_obj_t *fill;
        lv_obj_clean(t->det_bar);
        fill = lv_obj_create(t->det_bar);
        lv_obj_set_size(fill, w < 2 ? 0 : w, 10);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(M_PRIMARY), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(fill, 5, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    }
    /* Sin fade: las demas apps (musica/settings) muestran el panel directo.
     * lv_obj_fade_in dejaba el panel medio transparente ("difuso") unos
     * milisegundos y ademas acumulaba animaciones al reabrir el detalle. */
    lv_obj_clear_flag(t->detalle, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();

    if (NULL == t->btn_matar)
    {
        t->btn_matar = ui_button(t->g, ID_MATAR, "MATAR", 12, 390, 140, 48, M_RED);
    }
    else
    {
        lvgl_lock();
        lv_obj_clear_flag(t->btn_matar, LV_OBJ_FLAG_HIDDEN);
        lvgl_unlock();
    }
}

static void cerrar_detalle(struct tareas_app *t)
{
    lvgl_lock();
    if (NULL != t->detalle)
    {
        lv_obj_add_flag(t->detalle, LV_OBJ_FLAG_HIDDEN);
    }
    if (NULL != t->btn_matar)
    {
        lv_obj_add_flag(t->btn_matar, LV_OBJ_FLAG_HIDDEN);
    }
    if (NULL != t->btn_confirm)
    {
        lv_obj_add_flag(t->btn_confirm, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
    t->pantalla = P_LISTA;
    t->sel_tid = -1;
    t->confirm_tid = -1;
    ui_clear_events(t->g);
    pintar(t);
}

/* ---- MATAR con confirmacion --------------------------------------------- */

static void pulsar_matar(struct tareas_app *t)
{
    if (t->sel_tid < 0) return;

    /* no matar las tareas criticas del sistema */
    if (t->sel_tid == 0 || t->sel_tid == 1)
    {
        lvgl_lock();
        lv_label_set_text(t->det_txt, "No se puede matar el idle del sistema");
        lvgl_unlock();
        return;
    }

    if (t->confirm_tid != t->sel_tid)
    {
        /* primer toque: pedir confirmacion */
        t->confirm_tid = t->sel_tid;
        if (NULL == t->btn_confirm)
        {
            t->btn_confirm = ui_button(t->g, ID_CONFIRM, "CONFIRMAR",
                                       168, 390, 140, 48, M_RED);
        }
        else
        {
            lvgl_lock();
            lv_obj_clear_flag(t->btn_confirm, LV_OBJ_FLAG_HIDDEN);
            lvgl_unlock();
        }
        return;
    }

    /* segundo toque: matar */
    {
        int objetivo = t->sel_tid;
        int slot;
        (void)kill(objetivo);
        slot = appmgr_find_tid(objetivo);
        if (slot >= 0) appmgr_unregister(slot);
        cerrar_detalle(t);
    }
}

/* ---- interfaz ------------------------------------------------------------ */

static void crear(struct tareas_app *t)
{
    lv_obj_t *raiz = ui_root(t->g);

    lvgl_lock();
    lv_obj_set_style_bg_color(raiz, lv_color_hex(M_BG), 0);
    lv_obj_set_style_bg_opa(raiz, LV_OPA_COVER, 0);

    /* app bar */
    {
        lv_obj_t *bar = lv_obj_create(raiz);
        lv_obj_set_size(bar, 320, 56);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(M_SURFACE2), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        {
            lv_obj_t *ttl = lv_label_create(bar);
            lv_obj_set_style_text_font(ttl, &lv_font_unscii_16, 0);
            lv_obj_set_style_text_color(ttl, lv_color_hex(M_ONPRIM), 0);
            lv_obj_align(ttl, LV_ALIGN_LEFT_MID, 12, 0);
            lv_label_set_text(ttl, "TAREAS");
        }
    }

    t->lista = lv_obj_create(raiz);
    lv_obj_set_size(t->lista, CARD_W, LIST_BOTTOM - CARD_Y);
    lv_obj_set_pos(t->lista, CARD_X, CARD_Y);
    lv_obj_set_style_bg_opa(t->lista, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(t->lista, 0, 0);
    pad_all(t->lista, 0);
    lv_obj_set_style_pad_row(t->lista, 0, 0);
    lv_obj_add_flag(t->lista, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(t->lista, LV_DIR_VER);
    lv_obj_add_flag(t->lista, LV_OBJ_FLAG_EVENT_BUBBLE);

    t->pie = lv_label_create(raiz);
    lv_obj_set_style_text_font(t->pie, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(t->pie, lv_color_hex(M_PRIMARY), 0);
    lv_obj_align(t->pie, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_label_set_text(t->pie, "");

    lvgl_unlock();

    ui_button(t->g, ID_SALIR, "X", 258, 8, 52, 40, M_RED);
    ui_button(t->g, ID_PAUSA, "II", 258, 404, 52, 40, M_SURFACE2);
}

static void ir_fondo(struct tareas_app *t, int *fuera);

static void procesar(struct tareas_app *t, ui_event_t *ev, int *fuera)
{
    if (UI_EVENT_BUTTON == ev->tipo)
    {
        if (ev->id == ID_SALIR) { *fuera = 1; return; }
        if (ev->id == ID_PAUSA) { t->pausa = !t->pausa; pintar(t); return; }
        if (ev->id == ID_MATAR) { pulsar_matar(t); return; }
        if (ev->id == ID_CONFIRM) { pulsar_matar(t); return; }
        return;
    }

    if (UI_EVENT_SWIPE == ev->tipo)
    {
        if (UI_EDGE_BOTTOM == ev->edge && UI_DIR_UP == ev->dir)
        {
            if (P_DETALLE == t->pantalla) cerrar_detalle(t);
            else ir_fondo(t, fuera);
        }
        return;
    }

    if (UI_EVENT_TAP == ev->tipo && P_LISTA == t->pantalla)
    {
        int scroll, content_y, idx;
        if (ev->x1 >= 258 && ev->y1 < 56) return;
        if (ev->x1 >= 258 && ev->y1 > 404) return;
        lvgl_lock();
        scroll = (int)lv_obj_get_scroll_top(t->lista);
        lvgl_unlock();
        content_y = ev->y1 - CARD_Y + scroll;
        idx = content_y / ROW_H;
        if (idx >= 0 && idx < t->n_filas)
        {
            abrir_detalle(t, t->filas[idx].tid);
        }
    }
}

static void ir_fondo(struct tareas_app *t, int *fuera)
{
    int slot;
    slot = appmgr_find_tid(gettid());
    if (slot < 0) { *fuera = 1; return; }
    ui_show(t->g, 0);
    appmgr_set_state(slot, APP_BACKGROUND);
    for (;;)
    {
        int st = appmgr_get_state(slot);
        ui_event_t ev;
        if (st == APP_RUNNING) { ui_show(t->g, 1); ui_clear_events(t->g); pintar(t); return; }
        if (st == APP_CLOSING || st == APP_FREE) { *fuera = 1; return; }
        while (ui_poll(t->g, &ev)) { }
        sleep(50);
    }
}

static void salir(struct tareas_app *t)
{
    int slot = appmgr_find_tid(gettid());
    if (slot >= 0) appmgr_unregister(slot);
    ui_destroy(t->g);
    psram_free(t);
}

int main(int argc, char **argv)
{
    struct tareas_app *t;
    int fuera = 0;
    int slot;
    (void)argc; (void)argv;

    t = (struct tareas_app *)psram_malloc(sizeof(*t));
    if (NULL == t) return 1;
    memset(t, 0, sizeof(*t));
    /* Igual que musica/settings: sin chrome compartido (ni flecha ni banner
     * del lib/gestos), porque esta app trae su propia app bar. */
    t->g = ui_create_ex(UI_CHROME_NONE);
    t->pantalla = P_LISTA;
    t->sel_tid = -1;
    t->confirm_tid = -1;

    slot = appmgr_register("Tareas", "/apps/tareas/app.elf",
                           M_PRIMARY, gettid());
    t->slot = slot;
    if (slot >= 0) appmgr_take_foreground(slot);

    crear(t);
    pintar(t);

    while (!fuera)
    {
        ui_event_t ev;
        if (ui_poll(t->g, &ev))
        {
            procesar(t, &ev, &fuera);
        }
        else if (!t->pausa && P_LISTA == t->pantalla)
        {
            unsigned long ahora = (unsigned long)millis();
            if (t->ms_ultimo != 0UL && (ahora - t->ms_ultimo) >= 1000UL)
            {
                pintar(t);
            }
        }
        else if (appmgr_get_state(slot) == APP_CLOSING)
        {
            fuera = 1;
        }
        sleep(20);
    }

    salir(t);
    exit(0);
    return 0;
}
