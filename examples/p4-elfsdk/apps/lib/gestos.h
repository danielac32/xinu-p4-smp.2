/**
 * @file gestos.h
 *
 * FASE 3 -- Input/gesture library para apps ELF, SIN LVGL.
 *
 * La app lee el tactile en SU hebra con control(TOUCH0, TOUCH_CTRL_READ,...)
 * y llama ui_poll(): no hay hebra lvglui ni callbacks.  Los eventos se
 * apilan en un anillo SPSC dentro del handle de PSRAM; la API y los nombres
 * se conservan para que las apps migradas cambien lo menos posible.
 *
 * ELF rules: solo simbolos exportados por exports.c; NADA de estaticas
 * escribibles de fichero (el cargador no aplica reloc LO12_S locales); todo
 * el estado vive en el handle.  exit() mata la hebra: llamarlo solo desde
 * el bucle de main.
 */
#ifndef APPLIB_GESTOS_H
#define APPLIB_GESTOS_H

/* Sin LVGL: solo types basicos del kernel. */
#include <kernel.h>

typedef struct gestos gestos_t;
typedef struct gestos ui_input_t;

#define UI_EVENT_NONE       0
#define UI_EVENT_SWIPE      1
#define UI_EVENT_TAP        2
#define UI_EVENT_LONG_PRESS 3
#define UI_EVENT_BUTTON     4

#define UI_DIR_UP    0
#define UI_DIR_DOWN  1
#define UI_DIR_LEFT  2
#define UI_DIR_RIGHT 3
#define UI_DIR_NONE  4

#define UI_EDGE_NONE   0
#define UI_EDGE_TOP    1
#define UI_EDGE_BOTTOM 2
#define UI_EDGE_LEFT   3
#define UI_EDGE_RIGHT  4

typedef struct
{
    int tipo;                   /* UI_EVENT_* */
    int dir;                    /* UI_DIR_*   */
    int id;                     /* UI_EVENT_BUTTON id */
    int x1, y1, x2, y2;
    int dx, dy;
    int edge;                   /* UI_EDGE_* donde empezo */
} ui_event_t;
typedef ui_event_t gev_t;

/* Adornos: el unico que tiene sentido sin LVGL es la flecha de gesto,
 * dibujada con GFX_CTRL_RECT sobre el panel. */
#define UI_CHROME_NONE   0x00
#define UI_CHROME_ARROW  0x02
#define UI_CHROME_ALL    UI_CHROME_ARROW

ui_input_t *ui_create(void);
ui_input_t *ui_create_ex(int chrome);
int ui_poll(ui_input_t *ui, ui_event_t *ev);
void ui_clear_events(ui_input_t *ui);

/* Los botones ahora son solo rectangulos recordados en el handle: la app
 * decide cuando redibuja; ui_button devuelve el id ( antes un lv_obj_t* ). */
int ui_button(ui_input_t *ui, int id, const char *label, int x, int y,
              int w, int h, unsigned int rgb);
void ui_buttons_clear(ui_input_t *ui);
void ui_show(ui_input_t *ui, int on);

const char *ui_dir_name(int dir);
unsigned int ui_dir_color(int dir);
void ui_destroy(ui_input_t *ui);

/* Compatibilidad con nombres viejos */
#define GE_NADA      UI_EVENT_NONE
#define GE_ARRASTRE  UI_EVENT_SWIPE
#define GE_TOQUE     UI_EVENT_TAP
#define GE_LARGA     UI_EVENT_LONG_PRESS
#define GE_BOTON     UI_EVENT_BUTTON
#define GE_ARRIBA UI_DIR_UP
#define GE_ABAJO  UI_DIR_DOWN
#define GE_IZQ    UI_DIR_LEFT
#define GE_DER    UI_DIR_RIGHT
#define GE_SINDIR UI_DIR_NONE
#define GE_PASTILLA UI_CHROME_NONE
#define GE_FLECHA   UI_CHROME_ARROW
#define GE_NOTA     0
#define GE_ADORNOS  UI_CHROME_ALL
#define gestos_nueva()               ui_create()
#define gestos_nueva_partes(p)       ui_create_ex(p)
#define gestos_leer(g, ev)           ui_poll((g), (ev))
#define gestos_boton(g,i,t,x,y,w,h,c) ui_button((g),(i),(t),(x),(y),(w),(h),(c))
#define gestos_botones_limpiar(g)    ui_buttons_clear(g)
#define gestos_show(g,on)            ui_show((g),(on))
#define gestos_dir_texto(d)          ui_dir_name(d)
#define gestos_dir_color(d)          ui_dir_color(d)
#define gestos_cerrar(g)             ui_destroy(g)

#endif /* APPLIB_GESTOS_H */
