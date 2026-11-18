/**
 * Musica -- Material/gfx (sin LVGL).  Port fiel de la logica original:
 * MP3 desde /music, hilo de reproduccion en background (sigue sonando),
 * espectro, lista/reproductor, controles prev/play/next/vol.
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <timer.h>
#include <memory.h>
#include <device.h>
#include <thread.h>
#include <audio.h>
#include <mp3.h>
#define FAT_INLINE inline
#include <fat_filelib.h>
#include <gfx.h>
#include <appmgr.h>
#include "gestos.h"
#include "ui.h"

void exit(int codigo);

#define CARPETA    "/music"
#define MAX_TEMAS  24
#define LARGO_NOM  64
#define TRAMO      1024
#define BANDAS     8

#define P_LISTA 0
#define P_REPRO 1

#define ID_TEMA    1
#define ID_PREV    40
#define ID_PLAY    41
#define ID_NEXT    42
#define ID_SALIR   43
#define ID_VOLVER  44
#define ID_VOL_DN  45
#define ID_VOL_UP  46

#define CMD_NONE   0
#define CMD_PLAY   1
#define CMD_PAUSE  2
#define CMD_RESUME 3
#define CMD_STOP   4

#define C_FONDO  UI_BG
#define C_PANEL  UI_SURFACE
#define C_TEXTO  UI_TEXT
#define C_FILA   UI_SURFACE
#define C_ACT    UI_AMBER
#define C_BTN    UI_SURFACE2
#define C_VERDE  UI_GREEN
#define C_ROJO   UI_RED

#define X_BARRA    20
#define PASO_BARRA 36
#define AN_BARRA   26
#define Y_BASE     320
#define AL_MAX     110
#define AL_MIN     4

struct musica
{
    ui_input_t *g;
    ui_status_t *st;
    int pantalla;
    char nombres[MAX_TEMAS][LARGO_NOM];
    int total;
    volatile int tema;
    volatile int sonando;
    int audio;
    short *pcm;
    short *mix;
    int altura[BANDAS];
    int pico[BANDAS];
    unsigned tic_espectro;
    unsigned tic_reloj;
    int vol;
    int slot;
    volatile int cmd;
    volatile int cmd_track;
    volatile int quit;
    volatile int repro_alive;
    tid_typ repro_tid;
    tid_typ main_tid;
};

static int acaba_mp3(const char *n)
{
    int l = (int)strlen(n);
    if (l < 4) return 0;
    n += l - 4;
    return (n[0]=='.' && (n[1]=='m'||n[1]=='M') && (n[2]=='p'||n[2]=='P') && (n[3]=='3'));
}

static void nombre_corto(char *d, const char *s, int m)
{
    int i, n = (int)strlen(s);
    if (n > 4) n -= 4;
    if (n >= m) n = m - 1;
    for (i = 0; i < n; i++) d[i] = ('.' == s[i]) ? ' ' : s[i];
    d[n] = '\0';
}

static void listado(struct musica *m)
{
    FL_DIR dir; fl_dirent ent; int n = 0;
    m->total = 0;
    if (NULL == fl_opendir(CARPETA, &dir)) { printf("musica: no %s\r\n", CARPETA); return; }
    while (0 == fl_readdir(&dir, &ent))
    {
        if (ent.is_dir || !acaba_mp3(ent.filename)) continue;
        if (n >= MAX_TEMAS) break;
        memcpy(m->nombres[n], ent.filename, LARGO_NOM);
        m->nombres[n][LARGO_NOM - 1] = '\0'; n++;
    }
    fl_closedir(&dir);
    m->total = n;
    printf("musica: %d pistas\r\n", n);
}

static void espectro_calc(struct musica *m, int frames)
{
    short *mix = m->mix; const short *p = m->pcm;
    int nivel[BANDAS]; int i,b,largo,ancho,suma,e,h,pic;
    for (i = 0; i < frames; i++) mix[i] = (short)(((int)p[2*i] + (int)p[2*i+1]) >> 1);
    largo = frames;
    for (b = 0; b < BANDAS; b++) {
        if (largo < 4) { nivel[b]=0; continue; }
        suma=0; for (i=0;i<largo;i++) suma += mix[i]<0 ? -(int)mix[i] : (int)mix[i];
        nivel[b]=suma/largo; ancho=largo>>1;
        for (i=0;i<ancho;i++) mix[i]=(short)(((int)mix[2*i]+(int)mix[2*i+1])>>1);
        largo=ancho;
    }
    for (i=0;i<BANDAS;i++) {
        int k = BANDAS-1-i;
        e = nivel[k]; if (k+1 < BANDAS && nivel[k+1] < e) e -= nivel[k+1];
        pic = m->pico[i]; if (e>pic) pic=e; else pic -= pic>>4; if (pic<64) pic=64; m->pico[i]=pic;
        h = AL_MIN + (int)((long)e * (AL_MAX-AL_MIN) / pic);
        if (h>AL_MAX) h=AL_MAX; if (h < m->altura[i]-3) h = m->altura[i]-3; m->altura[i]=h;
    }
}

static void callar(struct musica *m)
{
    int i;
    if (m->sonando) { control(m->audio, AUDIO_CTRL_STREAM, 0, 0); m->sonando = 0; }
    for (i=0;i<BANDAS;i++){ m->altura[i]=AL_MIN; m->pico[i]=0; }
}

static void sonar(struct musica *m)
{
    if (m->tema < 0 || m->audio < 0 || m->sonando) return;
    if (SYSERR == control(m->audio, AUDIO_CTRL_STREAM, 1, 0)) return;
    m->sonando = 1;
    m->tic_espectro = millis(); m->tic_reloj = m->tic_espectro;
}

static void pausar(struct musica *m)
{
    if (!m->sonando) return;
    control(m->audio, AUDIO_CTRL_STREAM_WAIT, 0, 0);
    control(m->audio, AUDIO_CTRL_STREAM, 0, 0);
    m->sonando = 0;
    { int i; for (i=0;i<BANDAS;i++){ m->altura[i]=AL_MIN; m->pico[i]=0; } }
}

static int cargar(struct musica *m, int i)
{
    char ruta[96];
    if (i < 0 || i >= m->total) return 0;
    snprintf(ruta, sizeof(ruta), "%s/%s", CARPETA, m->nombres[i]);
    if (OK != mp3_abrir(ruta)) { printf("musica: no abre %s\r\n", ruta); m->tema=-1; return 0; }
    m->tema = i;
    printf("musica: %s %dHz %dk\r\n", ruta, mp3_hz(), mp3_kbps());
    return 1;
}

static int entregar(struct musica *m, int bytes)
{
    const char *p = (const char *)m->pcm; int quedan = bytes, r=0;
    while (quedan > 0) {
        int s = write(m->audio, p, (uint)quedan);
        if (s <= 0) { if (++r > 8) return 0; sleep(2); continue; }
        r=0; p+=s; quedan-=s;
    }
    return 1;
}

static void saltar_cmd(struct musica *m, int dest);

static void avanzar(struct musica *m)
{
    unsigned ahora = millis();
    int n = mp3_leer(m->pcm, TRAMO);
    if (n <= 0) {
        callar(m);
        if (m->tema >= 0 && m->tema + 1 < m->total) saltar_cmd(m, m->tema + 1);
        return;
    }
    if (!entregar(m, n * 4)) { pausar(m); return; }
    if (ahora - m->tic_espectro >= 42) { m->tic_espectro = ahora; espectro_calc(m,n); }
    /* info se lee bajo demanda desde main */
    (void)ahora;
}

static void saltar_cmd(struct musica *m, int dest)
{
    if (dest < 0) dest = 0; if (dest >= m->total) return;
    callar(m);
    if (cargar(m,dest)) { sonar(m); }
}

static int repro_task(struct musica *m)
{
    int cmd;
    m->repro_alive = 1;
    while (!m->quit)
    {
        cmd = m->cmd;
        if (CMD_NONE != cmd)
        {
            int tr = m->cmd_track;
            m->cmd = CMD_NONE;
            if (CMD_PLAY == cmd) { callar(m); if (cargar(m, tr)) sonar(m); }
            else if (CMD_PAUSE == cmd) { pausar(m); }
            else if (CMD_RESUME == cmd) { sonar(m); }
            else if (CMD_STOP == cmd) { callar(m); mp3_cerrar(); }
        }
        if (m->quit) break;
        if (m->sonando)
        {
            int k;
            for (k = 0; k < 12 && m->sonando && !m->quit && CMD_NONE == m->cmd; k++)
                avanzar(m);
        }
        else
        {
            sleep(30);
        }
    }
    callar(m);
    mp3_cerrar();
    m->repro_alive = 0;
    send(m->main_tid, 1);
    return 0;
}

/* ---- UI (solo hilo principal) ---- */

static void dibujar_barras(struct musica *m)
{
    int i;
    ui_rrect(8, Y_BASE - AL_MAX - 8, 304, AL_MAX + 16, 8, C_PANEL);
    for (i = 0; i < BANDAS; i++) {
        int h = m->altura[i];
        unsigned c = (h*100/AL_MAX < 70) ? C_VERDE : ((h*100/AL_MAX < 88) ? C_ACT : C_ROJO);
        int x = X_BARRA + i * PASO_BARRA;
        ui_rect(x, Y_BASE - AL_MAX, AN_BARRA, AL_MAX, C_PANEL);
        ui_rrect(x, Y_BASE - h, AN_BARRA, h, 3, c);
    }
}

static void pantalla_lista(struct musica *m)
{
    char rot[48]; int i;
    m->pantalla = P_LISTA;
    ui_buttons_clear(m->g);
    ui_fill(C_FONDO);
    if (m->st) ui_status_draw(m->st);
    snprintf(rot, sizeof(rot), "MUSIC  %d tracks", m->total);
    ui_text(m->total ? rot : "MUSIC", 12, 52, C_TEXTO, C_FONDO);

    for (i = 0; i < m->total && i < MAX_TEMAS; i++) {
        char num[48];
        unsigned c = (i == m->tema) ? UI_PRIMARY : C_FILA;
        nombre_corto(rot, m->nombres[i], 22);
        snprintf(num, sizeof(num), "%02d  %s", i + 1, rot);
        ui_button(m->g, ID_TEMA + i, num, 10, 80 + i * 38, 300, 34, c);
    }
}

static void pantalla_repro(struct musica *m)
{
    char nom[36], t[64];
    unsigned ya = 0, todo = 0;
    m->pantalla = P_REPRO;
    ui_buttons_clear(m->g);
    ui_fill(C_FONDO);
    if (m->st) ui_status_draw(m->st);

    if (m->tema >= 0) {
        nombre_corto(nom, m->nombres[m->tema], 28);
        ya = mp3_ms_actual(); todo = mp3_ms_total();
    } else {
        strcpy(nom, "-");
    }
    ui_text(nom, 12, 52, C_TEXTO, C_FONDO);
    snprintf(t, sizeof(t), "%u:%02u / %u:%02u  %dk",
             ya/60000U,(ya/1000U)%60U,todo/60000U,(todo/1000U)%60U,
             (m->tema>=0)?mp3_kbps():0);
    ui_text(t, 12, 72, UI_TEXT_DIM, C_FONDO);
    snprintf(t, sizeof(t), "track %d/%d  VOL %d", m->tema+1, m->total, m->vol);
    ui_text(t, 12, 92, UI_TEXT_DIM, C_FONDO);

    ui_button(m->g, ID_VOL_DN, "VOL-", 20, 120, 90, 36, C_BTN);
    ui_button(m->g, ID_VOL_UP, "VOL+", 210, 120, 90, 36, C_BTN);

    dibujar_barras(m);

    ui_button(m->g, ID_PREV, "|<", 20, 360, 84, 54, C_BTN);
    ui_button(m->g, ID_PLAY, m->sonando ? "||" : ">", 118, 360, 84, 54, UI_PRIMARY);
    ui_button(m->g, ID_NEXT, ">|", 216, 360, 84, 54, C_BTN);
    ui_text_center("swipe borde arriba = lista", GFX_W/2, 430, UI_TEXT_DIM, C_FONDO);
}

static void pedir(struct musica *m, int cmd, int track)
{
    m->cmd_track = track;
    m->cmd = cmd;
}

static void elegir(struct musica *m, int i)
{
    if (i < 0 || i >= m->total) return;
    pantalla_repro(m);
    pedir(m, CMD_PLAY, i);
}

static void saltar(struct musica *m, int dest)
{
    if (dest < 0) dest = 0; if (dest >= m->total) return;
    pedir(m, CMD_PLAY, dest);
}

static void ir_fondo(struct musica *m, int *fuera)
{
    if (m->slot < 0) { *fuera = 1; return; }
    /* La musica sigue sonando en background (hilo repro activo). */
    if (ui_background(m->slot)) { *fuera = 1; return; }
    if (m->pantalla == P_REPRO) pantalla_repro(m); else pantalla_lista(m);
}

static void procesar(struct musica *m, ui_event_t *ev, int *fuera)
{
    if (ev->tipo == UI_EVENT_SWIPE) {
        if ((ev->dir == UI_DIR_UP || ev->dir == UI_DIR_DOWN) &&
            (ev->edge == UI_EDGE_BOTTOM || ev->edge == UI_EDGE_TOP)) {
            if (P_REPRO == m->pantalla) pantalla_lista(m);
            else ir_fondo(m, fuera);
        }
        return;
    }
    if (ev->tipo != UI_EVENT_BUTTON) return;
    if (P_LISTA == m->pantalla) {
        if (ev->id >= ID_TEMA && ev->id < ID_TEMA + m->total)
            elegir(m, ev->id - ID_TEMA);
        return;
    }
    switch (ev->id) {
    case ID_PREV: saltar(m, m->tema - 1); break;
    case ID_PLAY: pedir(m, m->sonando ? CMD_PAUSE : CMD_RESUME, m->tema); break;
    case ID_NEXT: saltar(m, m->tema + 1); break;
    case ID_VOL_DN:
    case ID_VOL_UP:
        if (ID_VOL_DN == ev->id) m->vol -= 10; else m->vol += 10;
        if (m->vol < 0) m->vol = 0;
        if (m->vol > 100) m->vol = 100;
        if (m->audio >= 0) control(m->audio, AUDIO_CTRL_VOLUME, m->vol, 0);
        pantalla_repro(m);
        break;
    default: break;
    }
}

static void parar_repro(struct musica *m)
{
    if (!m->repro_alive) return;
    m->quit = 1;
    while (m->repro_alive) sleep(20);
    recvclr();
}

static void salir_app(struct musica *m)
{
    if (m->slot >= 0) { appmgr_unregister(m->slot); m->slot = -1; }
    parar_repro(m);
    if (m->pcm) psram_free(m->pcm);
    if (m->mix) psram_free(m->mix);
    ui_status_destroy(m->st);
    if (m->g) ui_destroy(m->g);
}

int main(int argc, char *argv[])
{
    struct musica *m;
    ui_event_t ev;
    int fuera = 0;
    unsigned t_ui = 0;

    (void)argc; (void)argv;
    m = (struct musica *)psram_malloc(sizeof(*m));
    if (!m) exit(1);
    memset(m, 0, sizeof(*m));
    m->tema = -1; m->audio = SYSERR; m->slot = -1; m->repro_tid = SYSERR;
    m->vol = 70; m->main_tid = gettid();

    m->g = ui_create_ex(UI_CHROME_NONE);
    if (!m->g) { psram_free(m); exit(1); }
    m->st = ui_status_create();
    m->slot = appmgr_register("Musica", "/apps/musica/app.elf", 0x9BD14F, gettid());
    appmgr_take_foreground(m->slot);

    m->pcm = (short *)psram_malloc(TRAMO * 2 * (int)sizeof(short));
    m->mix = (short *)psram_malloc(TRAMO * (int)sizeof(short));
    if (!m->pcm || !m->mix) { salir_app(m); psram_free(m); exit(1); }

    m->audio = getdev("/dev/audio0");
    if (m->audio >= 0) control(m->audio, AUDIO_CTRL_VOLUME, 80, 0);
    listado(m);
    pantalla_lista(m);

    m->repro_tid = create((void *)repro_task, 20480, getprio(gettid()), "musrepro", 1, (long)m);
    if (SYSERR != m->repro_tid) ready(m->repro_tid, RESCHED_NO);

    while (!fuera) {
        while (ui_poll(m->g, &ev)) {
            procesar(m, &ev, &fuera);
            if (fuera) break;
        }
        if (!fuera && m->slot >= 0 && APP_CLOSING == appmgr_get_state(m->slot))
            fuera = 1;
        if (!fuera && m->pantalla == P_REPRO) {
            unsigned now = millis();
            if (now - t_ui >= 120) { t_ui = now; pantalla_repro(m); }
        }
        if (!fuera) sleep(15);
    }

    salir_app(m);
    psram_free(m);
    exit(0);
    return 0;
}
