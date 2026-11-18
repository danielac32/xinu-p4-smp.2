/**
 * Snake - Material/gfx (sin LVGL). Port fiel de la logica original.
 * Gestos: swipe = direccion, tap = reiniciar, swipe vertical = fondo.
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

#define APP_NAME "Snake"
#define APP_PATH "/apps/snake/app.elf"
#define APP_COLOR 0x4f7d3a

#define COLS 12
#define ROWS 16
#define MAXN (COLS * ROWS)
#define CX 10
#define CY 90
#define CW 25
#define CH 20

#define BID_UP 1
#define BID_DN 2
#define BID_LT 3
#define BID_RT 4

struct cell { short x, y; };

struct state {
    ui_input_t *ui;
    ui_status_t *st;
    struct cell body[MAXN];
    int len, dir, fx, fy, alive, score, slot, tick;
};

static unsigned rnd(unsigned *s)
{
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7fff;
}

static void place_food(struct state *s)
{
    unsigned seed = (unsigned)millis();
    int k, ok;
    for (k = 0; k < 200; k++)
    {
        int x = (int)(rnd(&seed) % COLS), y = (int)(rnd(&seed) % ROWS), i;
        ok = 1;
        for (i = 0; i < s->len; i++)
            if (s->body[i].x == x && s->body[i].y == y) { ok = 0; break; }
        if (ok) { s->fx = x; s->fy = y; return; }
    }
    s->fx = 0; s->fy = 0;
}

static void draw_cell(int c, int r, unsigned color)
{
    ui_rrect(CX + c * CW + 1, CY + r * CH + 1, CW - 2, CH - 2, 3, color);
}

static void draw_msg(struct state *s)
{
    char buf[40];
    if (s->alive)
        snprintf(buf, sizeof(buf), "Puntos: %d", s->score);
    else
        snprintf(buf, sizeof(buf), "Game over %d (tap)", s->score);
    /* limpiar zona del mensaje */
    ui_rect(10, 50, 300, 20, UI_BG);
    ui_text(buf, 10, 52, UI_TEXT, UI_BG);
}

static void draw_grid(struct state *s)
{
    int r, c, i;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            draw_cell(c, r, 0x203018);
    draw_cell(s->fx, s->fy, 0xd94a3a);
    for (i = 0; i < s->len; i++)
        draw_cell(s->body[i].x, s->body[i].y,
                  i == 0 ? 0xc8f07a : APP_COLOR);
}

static void draw_controls(struct state *s)
{
    ui_button(s->ui, BID_LT, "<", 10, 416, 65, 50, 0x3b6b2a);
    ui_button(s->ui, BID_UP, "^", 85, 416, 65, 50, 0x3b6b2a);
    ui_button(s->ui, BID_DN, "v", 160, 416, 65, 50, 0x3b6b2a);
    ui_button(s->ui, BID_RT, ">", 235, 416, 65, 50, 0x3b6b2a);
}

static void draw_full(struct state *s)
{
    ui_buttons_clear(s->ui);
    ui_fill(UI_BG);
    if (s->st) ui_status_draw(s->st);
    ui_text("SNAKE", 12, 52, UI_TEXT, UI_BG);
    draw_grid(s);
    draw_msg(s);
    draw_controls(s);
}

static void reset(struct state *s)
{
    s->len = 3; s->dir = 1; s->alive = 1; s->score = 0; s->tick = 0;
    s->body[0].x = COLS / 2;     s->body[0].y = ROWS / 2;
    s->body[1].x = s->body[0].x - 1; s->body[1].y = s->body[0].y;
    s->body[2].x = s->body[0].x - 2; s->body[2].y = s->body[0].y;
    place_food(s);
    draw_full(s);
}

static void step(struct state *s)
{
    int i, nx = s->body[0].x, ny = s->body[0].y;
    int ox = s->body[0].x, oy = s->body[0].y;
    struct cell tail;

    if (!s->alive) return;
    if (s->dir == 0) ny--;
    else if (s->dir == 1) nx++;
    else if (s->dir == 2) ny++;
    else nx--;

    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS)
    { s->alive = 0; draw_msg(s); return; }
    for (i = 0; i < s->len; i++)
        if (s->body[i].x == nx && s->body[i].y == ny)
        { s->alive = 0; draw_msg(s); return; }

    tail = s->body[s->len - 1];
    for (i = s->len; i > 0; i--) s->body[i] = s->body[i - 1];
    s->body[0].x = nx; s->body[0].y = ny;

    /* redibujado incremental: cabeza vieja -> cuerpo, nueva cabeza, cola/food */
    draw_cell(ox, oy, APP_COLOR);
    draw_cell(nx, ny, 0xc8f07a);
    if (nx == s->fx && ny == s->fy)
    {
        if (s->len < MAXN - 1) s->len++;
        s->score++;
        place_food(s);
        draw_cell(s->fx, s->fy, 0xd94a3a);
    }
    else
    {
        draw_cell(tail.y, tail.x, 0x203018); /* cola vieja se borra */
    }
    draw_msg(s);
}

static void ir_fondo(struct state *s)
{
    ui_event_t ev;
    int st;

    if (s->slot < 0) return;
    appmgr_set_state(s->slot, APP_BACKGROUND);
    for (;;)
    {
        st = appmgr_get_state(s->slot);
        if (st == APP_RUNNING)
        {
            draw_full(s);
            return;
        }
        if (st == APP_CLOSING || st == APP_FREE)
        {
            appmgr_unregister(s->slot);
            ui_status_destroy(s->st);
            ui_destroy(s->ui);
            psram_free(s);
            exit(0);
        }
        while (ui_poll(s->ui, &ev)) { }
        sleep(50);
    }
}

int main(int argc, char *argv[])
{
    struct state *s;
    ui_event_t ev;

    (void)argc; (void)argv;
    s = psram_malloc(sizeof(*s));
    if (!s) exit(1);
    memset(s, 0, sizeof(*s));
    s->slot = -1;

    s->ui = ui_create_ex(UI_CHROME_NONE);
    if (!s->ui) { psram_free(s); exit(1); }
    s->st = ui_status_create();
    s->slot = appmgr_register(APP_NAME, APP_PATH, APP_COLOR, gettid());
    appmgr_take_foreground(s->slot);

    reset(s);

    for (;;)
    {
        if (s->slot >= 0 && appmgr_get_state(s->slot) == APP_CLOSING)
        {
            appmgr_unregister(s->slot);
            ui_status_destroy(s->st);
            ui_destroy(s->ui);
            psram_free(s);
            exit(0);
        }
        while (ui_poll(s->ui, &ev))
        {
            if (ev.tipo == UI_EVENT_SWIPE)
            {
                if ((ev.dir == UI_DIR_UP || ev.dir == UI_DIR_DOWN) &&
                    (ev.edge == UI_EDGE_BOTTOM || ev.edge == UI_EDGE_TOP))
                {
                    ir_fondo(s);
                }
                else if (ev.dir == UI_DIR_UP && s->dir != 2) s->dir = 0;
                else if (ev.dir == UI_DIR_RIGHT && s->dir != 3) s->dir = 1;
                else if (ev.dir == UI_DIR_DOWN && s->dir != 0) s->dir = 2;
                else if (ev.dir == UI_DIR_LEFT && s->dir != 1) s->dir = 3;
            }
            else if (ev.tipo == UI_EVENT_BUTTON)
            {
                if (ev.id == BID_UP && s->dir != 2) s->dir = 0;
                else if (ev.id == BID_RT && s->dir != 3) s->dir = 1;
                else if (ev.id == BID_DN && s->dir != 0) s->dir = 2;
                else if (ev.id == BID_LT && s->dir != 1) s->dir = 3;
            }
            else if (ev.tipo == UI_EVENT_TAP)
            {
                reset(s);
            }
        }
        if (++s->tick >= 6) { s->tick = 0; step(s); }
        sleep(40);
    }
    return 0;
}
