/**
 * @file ui.h
 *
 * FASE 3 -- Biblioteca de UI profesional sobre gfx (sin LVGL).
 * Dibujo inmediato, sin objetos retenidos: cada funcion pinta y se olvida.
 * Todo el estado esta en los handles de PSRAM; sin estaticas escribibles.
 */
#ifndef APPLIB_UI_H
#define APPLIB_UI_H

#include <kernel.h>
#include <gfx.h>

/* ---- paleta Material Design oscuro ---- */
/* Superficies gris-neutro (sin componente roja/rosada), un unico acento azul.
 * Referencia: Material dark  #121212 / #1E1E1E / #2C2C2C / Primary #5B8DEF. */
#define UI_BG        0x0810     /* #121212 aprox            */
#define UI_SURFACE   0x1C18     /* #1E1E1E                  */
#define UI_SURFACE2  0x318C     /* #2C2C2C gris neutro       */
#define UI_BORDER    0x4208     /* #3C3C3C                  */
#define UI_PRIMARY   0x2D7F     /* azul acento (Material)    */
#define UI_TEXT      0xFFFF     /* blanco puro               */
#define UI_TEXT_DIM  0xBDB6     /* gris claro NEUTRO (sin rosa) */
#define UI_GREEN     0x07E0
#define UI_RED       0xF800     /* solo boton X del conmutador */
#define UI_AMBER     0xFD20
#define UI_CYAN      0x07FF
#define UI_WIFI_ON   0x2D7F     /* azul: gris -> azul al conectar */
#define UI_OFF       0x5A6B     /* gris azulado apagado */

/* Gesto de "ir al launcher" comun a TODAS las apps: swipe vertical.
 * Acepta UP y DOWN: el panel FT6336 reporta Y invertido, asi que un dedo que
 * el usuario percibe como "abajo" puede llegar como UP (medido en HW).
 * La app NO muere: pasa a APP_BACKGROUND y sigue viva; el launcher la muestra
 * en la ventana de apps en segundo plano, con X para matarla. */
#define ui_quit_event(ev) \
    ((ev)->tipo == UI_EVENT_SWIPE && \
     ((ev)->dir == UI_DIR_UP || (ev)->dir == UI_DIR_DOWN))

/* Pone la app en segundo plano y espera a que el launcher la reanude.
 * Devuelve 1 si el launcher la cerno (APP_CLOSING/FREE) -> la app debe salir;
 * devuelve 0 si fue reanudada (APP_RUNNING) -> la app sigue con su bucle. */
int ui_background(int slot);

/* ---- primitivas ---- */
void ui_fill(unsigned color);
void ui_rect(int x, int y, int w, int h, unsigned color);
void ui_rrect(int x, int y, int w, int h, int r, unsigned color);
void ui_shadow(int x, int y, int w, int h, int r);
void ui_line(int x1, int y1, int x2, int y2, unsigned color);
void ui_circle(int cx, int cy, int r, unsigned color, int fill);
void ui_text(const char *s, int x, int y, unsigned fg, unsigned bg);
void ui_text_center(const char *s, int cx, int y, unsigned fg, unsigned bg);

/* ---- iconos vectoriales: dibujan dentro de una celda (x,y,w,h) ---- */
/* kind: 0=calc 1=tresenraya 2=tetris 3=archivos 4=ajustes
 *       5=musica 6=reloj/tareas 7=info 8=foto 9=terminal */
void ui_icon(int kind, int x, int y, int w, int h, unsigned color);

/* ---- barra de estado profesional ----
 * Muestra: rallitas WiFi (rojo sin enlace / verde con enlace),
 * IP compacta, nivel de bateria.  Todo en una franja de 46px arriba.
 */
typedef struct ui_status ui_status_t;
ui_status_t *ui_status_create(void);
void ui_status_draw(ui_status_t *s);
void ui_status_destroy(ui_status_t *s);

#endif /* APPLIB_UI_H */
