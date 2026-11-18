/**
 * @file micdemo.c
 *
 * App educativa: COMO USAR EL MICROFONO, GRABAR Y REPRODUCIR en Xinu (ESP32-P4).
 *
 * Resumen de lo que hace esta app:
 *   1. Al pulsar "GRABAR" captura audio del microfono durante 10 segundos.
 *   2. Guarda las muestras en un buffer en PSRAM (memoria externa grande).
 *   3. Al terminar los 10 s reproduce automaticamente ese buffer por el altavoz
 *      enviandolo al dispositivo de audio (I2S -> codec ES8311).
 *   4. Permite ajustar el VOLUMEN de salida y la GANANCIA del microfono.
 *   5. Gesto para salir: deslizar hacia arriba desde el BORDE INFERIOR.
 *
 * Dispositivo: /dev/audio0 (driver de audio del kernel, codec ES8311 por I2S).
 * Formato FIJO del dispositivo (no hay que negociarlo): 48000 Hz, estereo
 * (2 canales), 16 bits con signo little-endian -> 4 bytes por trama (L,R)
 * = 192000 bytes/s = 192 bytes/ms.
 *
 * La app esta muy comentada a proposito para que sirva de guia.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <kernel.h>
#include <thread.h>
#include <memory.h>
#include <timer.h>
#include <device.h>
#include <audio.h>
#include <lvgl.h>
#include <appmgr.h>
#include "gestos.h"

/* Las apps ELF declaran exit() (kill del hilo actual) como en el resto de apps. */
void exit(int codigo);

/* -------------------------------------------------------------------------- */
/*  CONFIGURACION / CONSTANTES                                                 */
/* -------------------------------------------------------------------------- */

#define APP_NAME  "MicDemo"
#define APP_PATH  "/apps/micdemo/app.elf"
#define APP_COLOR 0x3a86ff

/*
 * Duracion maxima de grabacion: 10 segundos.
 * Bytes por milisegundo = 48000 muestras/s * 2 canales * 2 bytes / 1000 = 192.
 * 10 s = 10000 ms * 192 = 1.920.000 bytes (~1.83 MB), cabe holgado en PSRAM.
 */
#define REC_MS       10000U
#define BYTES_PER_MS 192U
#define REC_BYTES    (REC_MS * BYTES_PER_MS)

/*
 * Leemos/escribimos en bloques de ~50 ms (9600 bytes = 2400 tramas) para:
 *   - poder actualizar el contador de la interfaz mientras grabamos,
 *   - no bloquear demasiado en una sola operacion,
 *   - y seguir sondeando gestos/eventos.
 */
#define CHUNK_BYTES  9600U

/* Ganancia del microfono (AUDIO_CTRL_MIC_GAIN, 0..24 dB) y volumen inicial. */
#define MIC_GAIN_DEF 18
#define VOL_DEF      70

/* IDs de botones (gestos/ui los reporta en ev.tipo==UI_EVENT_BUTTON, ev.id). */
#define ID_BTN_REC    1
#define ID_BTN_VOL_DN 2
#define ID_BTN_VOL_UP 3
#define ID_BTN_MIC_DN 4
#define ID_BTN_MIC_UP 5

/* Estados simples de la app. */
enum { ST_IDLE = 0, ST_RECORDING, ST_PLAYING };

/* -------------------------------------------------------------------------- */
/*  ESTADO DE LA APP (todo en PSRAM, sin estaticas escribibles de fichero)     */
/* -------------------------------------------------------------------------- */

struct micdemo {
    ui_input_t *ui;       /* handle de la libreria de gestos/input */
    int         slot;     /* ranura en appmgr (-1 si no registrado) */
    int         audiofd;  /* descriptor de /dev/audio0 (o SYSERR) */
    uint8_t    *pcm;      /* buffer PCM crudo en PSRAM (REC_BYTES) */
    uint32_t    len;      /* bytes validos grabados en pcm */
    int         estado;   /* ST_IDLE / ST_RECORDING / ST_PLAYING */
    int         vol;      /* volumen de salida 0..100 */
    int         mic_gain; /* ganancia microfono 0..24 dB */

    /* Etiquetas LVGL que actualizamos (se tocan siempre bajo lvgl_lock). */
    lv_obj_t   *lbl_estado;
    lv_obj_t   *lbl_info;
    lv_obj_t   *lbl_vol;
    lv_obj_t   *lbl_mic;
};

/* Ir a segundo plano (gesto borde inferior hacia arriba), como el resto de apps:
 * oculta la UI y cede al launcher; si el launcher la retoma vuelve a mostrar,
 * si la cierra o se elimina hace limpieza y sale del hilo. */
static void ir_fondo(struct micdemo *m)
{
    ui_event_t ev;
    int st;

    if (m->slot < 0) return;
    ui_show(m->ui, 0);
    appmgr_set_state(m->slot, APP_BACKGROUND);
    for (;;) {
        st = appmgr_get_state(m->slot);
        if (APP_RUNNING == st) {
            ui_show(m->ui, 1);
            return;
        }
        if (APP_CLOSING == st || APP_FREE == st) {
            if (m->audiofd != SYSERR) {
                control(m->audiofd, AUDIO_CTRL_IN, 0, 0);
                control(m->audiofd, AUDIO_CTRL_STREAM, 0, 0);
                control(m->audiofd, AUDIO_CTRL_OUT, 0, 0);
            }
            ui_destroy(m->ui);
            if (m->pcm) psram_free(m->pcm);
            psram_free(m);
            exit(0);
        }
        while (ui_poll(m->ui, &ev)) { }
        sleep(50);
    }
}

/* -------------------------------------------------------------------------- */
/*  AYUDAS DE INTERFAZ (LVGL: crear/actualizar objetos bajo lvgl_lock)         */
/* -------------------------------------------------------------------------- */

/* Crea una etiqueta de texto en (x,y) con color RGB. Llamar con lvgl_lock ya
 * tomado, o envolver la creacion; aqui se asume llamada dentro de un bloque
 * con lock cuando se construye la UI. */
static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, int x, int y, uint32_t rgb)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    return l;
}

static void set_text(lv_obj_t *lbl, const char *txt)
{
    if (lbl == NULL) return;
    lvgl_lock();
    lv_label_set_text(lbl, txt);
    lvgl_unlock();
}

static void pintar_vol(struct micdemo *m)
{
    char b[24];
    sprintf(b, "Vol: %d%%", m->vol);
    set_text(m->lbl_vol, b);
}

static void pintar_mic(struct micdemo *m)
{
    char b[24];
    sprintf(b, "Mic: +%d dB", m->mic_gain);
    set_text(m->lbl_mic, b);
}

/* Construye la pantalla (ajustada a 320x480, sin chrome del sistema). */
static void dibujar_ui(struct micdemo *m)
{
    char b[64];
    lv_obj_t *root;

    lvgl_lock();
    root = ui_root(m->ui);

    /* Textos cortos para caber en el ancho de 320 px. */
    mk_label(root, "MIC DEMO (microfono)", 8, 8, 0xffffff);
    mk_label(root, "1) Ajusta vol/micro", 8, 30, 0xb0c4de);
    mk_label(root, "2) GRABAR: 10 s y reproduce", 8, 48, 0xb0c4de);
    mk_label(root, "Salir: swipe ARRIBA", 8, 66, 0x8899aa);
    mk_label(root, "desde BORDE INFERIOR", 8, 84, 0x8899aa);
    mk_label(root, "PCM 48kHz estereo 16-bit", 8, 102, 0x778899);

    m->lbl_estado = mk_label(root, "Estado: listo", 8, 126, 0x7CFC00);
    sprintf(b, "Buf: 10s (%u B) en PSRAM", (unsigned)REC_BYTES);
    m->lbl_info = mk_label(root, b, 8, 146, 0xcccccc);

    /* Controles de VOLUMEN de salida (altavoz/DAC, 0..100 %). */
    m->lbl_vol = mk_label(root, "", 8, 172, 0xffffff);
    lv_obj_set_style_text_font(m->lbl_vol, &lv_font_unscii_8, 0);
    ui_button(m->ui, ID_BTN_VOL_DN, "VOL-",  8, 192, 70, 36, 0x334455);
    ui_button(m->ui, ID_BTN_VOL_UP, "VOL+", 84, 192, 70, 36, 0x334455);

    /* Controles de GANANCIA del microfono (sensibilidad ADC/codec, 0..24 dB).
     * Mas ganancia = capta voz mas baja, pero tambien mas ruido si es excesiva. */
    m->lbl_mic = mk_label(root, "", 170, 172, 0xffffff);
    lv_obj_set_style_text_font(m->lbl_mic, &lv_font_unscii_8, 0);
    ui_button(m->ui, ID_BTN_MIC_DN, "MIC-", 170, 192, 70, 36, 0x445533);
    ui_button(m->ui, ID_BTN_MIC_UP, "MIC+", 246, 192, 70, 36, 0x445533);

    /* Boton principal de grabacion. */
    ui_button(m->ui, ID_BTN_REC, "GRABAR (10 s)", 40, 250, 240, 60, 0xcc3333);
    lvgl_unlock();

    pintar_vol(m);
    pintar_mic(m);
}

/* -------------------------------------------------------------------------- */
/*  AUDIO: CONTROL, GRABACION Y REPRODUCCION                                   */
/* -------------------------------------------------------------------------- */
/*
 * API del dispositivo /dev/audio0 (resumen practico):
 *  - open/getdev("/dev/audio0"): obtiene el descriptor del dispositivo.
 *  - control(fd, cmd, a, b): configura hardware/ruta de audio. Comandos usados:
 *      AUDIO_CTRL_VOLUME, vol, 0   -> volumen maestro de salida 0..100 %
 *      AUDIO_CTRL_MIC_GAIN, db, 0  -> ganancia del microfono 0..24 dB
 *      AUDIO_CTRL_IN,  1, 0        -> ENCIENDE captura (I2S RX + ADC/mic)
 *      AUDIO_CTRL_IN,  0, 0        -> APAGA captura
 *      AUDIO_CTRL_OUT, 1, 0        -> ENCIENDE salida (DAC/altavoz)
 *      AUDIO_CTRL_OUT, 0, 0        -> APAGA salida
 *      AUDIO_CTRL_STREAM, 1, 0     -> monta/activa stream I2S de reproduccion
 *      AUDIO_CTRL_STREAM, 0, 0     -> desmonta stream I2S de reproduccion
 *      AUDIO_CTRL_STREAM_WAIT,0,0  -> espera a que se reproduzca lo encolado
 *  - read(fd, buf, n): lee muestras del microfono. NO bloquea hasta llenar n:
 *      devuelve las tramas ya capturadas y 0 si aun no hay un trozo listo.
 *  - write(fd, buf, n): envia muestras al altavoz. Puede escribir menos de n
 *      si el anillo interno se llena (hay que iterar hasta enviar todo).
 *
 * Formato de las muestras (buffer): PCM intercalado L,R, 16-bit con signo
 * (little-endian), 48 kHz. Es el mismo formato que graba y que reproduce, asi
 * que el buffer se pasa directo entre read() y write() sin conversion.
 */

static void aplicar_vol(struct micdemo *m)
{
    control(m->audiofd, AUDIO_CTRL_VOLUME, (long)m->vol, 0);
}

static void aplicar_mic(struct micdemo *m)
{
    control(m->audiofd, AUDIO_CTRL_MIC_GAIN, (long)m->mic_gain, 0);
}

/* Resultado de sondeo durante operaciones bloqueantes: 0=seguir, 1=ir a fondo,
 * 2=cerrar (lo pidio el gestor). */
#define OP_CONTINUE 0
#define OP_FONDO    1
#define OP_CERRAR   2

static bool es_gesto_fondo(ui_event_t *ev)
{
    return (ev->tipo == UI_EVENT_SWIPE && ev->edge == UI_EDGE_BOTTOM && ev->dir == UI_DIR_UP);
}

static int sondear_control(struct micdemo *m)
{
    ui_event_t ev;
    if (m->slot >= 0 && appmgr_get_state(m->slot) == APP_CLOSING)
        return OP_CERRAR;
    while (ui_poll(m->ui, &ev)) {
        if (m->slot >= 0 && appmgr_get_state(m->slot) == APP_CLOSING)
            return OP_CERRAR;
        if (es_gesto_fondo(&ev))
            return OP_FONDO;
    }
    return OP_CONTINUE;
}

/*
 * GRABAR 10 SEGUNDOS (paso a paso):
 *  1) Configurar ganancia del microfono.
 *  2) Arrancar captura: AUDIO_CTRL_IN = 1 (enciende ruta de entrada/mic).
 *  3) Leer en bucle con read(): copia al buffer las tramas que el motor ya
 *     capturo. read() devuelve 0 si no hay datos listos -> dormir unos ms y
 *     reintentar (no quemar CPU). Si devuelve >0, acumular en m->len.
 *  4) Repetir hasta acumular REC_BYTES o hasta cumplir 10 s / pedir salir.
 *  5) Parar captura: AUDIO_CTRL_IN = 0. m->pcm queda con el PCM crudo grabado.
 * Devuelve OP_CONTINUE al terminar normal, u OP_FONDO/OP_CERRAR si se interrumpe.
 */
static int grabar_10s(struct micdemo *m)
{
    uint32_t t0;
    int op;

    m->estado = ST_RECORDING;
    m->len = 0;
    set_text(m->lbl_estado, "Estado: GRABANDO...");

    aplicar_mic(m);                              /* (1) sensibilidad */
    control(m->audiofd, AUDIO_CTRL_IN, 1, 0);   /* (2) encender micro */

    t0 = (uint32_t)millis();

    while (m->len < REC_BYTES) {                 /* (3)(4) bucle de captura */
        uint32_t ahora = (uint32_t)millis();
        uint32_t trans = ahora - t0;
        char b[48];
        int n;

        op = sondear_control(m);
        if (op != OP_CONTINUE) {
            control(m->audiofd, AUDIO_CTRL_IN, 0, 0);
            m->estado = ST_IDLE;
            set_text(m->lbl_estado, "Estado: listo");
            return op;
        }

        if (trans >= REC_MS) break; /* 10 segundos cumplidos */

        sprintf(b, "Grabando: %u.%u / 10.0 s",
                trans / 1000U, (trans % 1000U) / 100U);
        set_text(m->lbl_info, b);

        {
            uint32_t pedir = REC_BYTES - m->len;
            if (pedir > CHUNK_BYTES) pedir = CHUNK_BYTES;
            n = read(m->audiofd, m->pcm + m->len, (int)pedir);
        }
        if (n > 0) {
            m->len += (uint32_t)n;
        } else {
            sleep(5); /* aun no hay un trozo listo: ceder CPU y reintentar */
        }
    }

    control(m->audiofd, AUDIO_CTRL_IN, 0, 0);    /* (5) apagar micro */

    {
        char b[64];
        sprintf(b, "Grabado: %u ms. Reproduciendo...",
                (unsigned)(m->len / BYTES_PER_MS));
        set_text(m->lbl_info, b);
    }
    return OP_CONTINUE;
}

/*
 * REPRODUCIR el buffer m->pcm por el altavoz via I2S (paso a paso):
 *  1) Encender salida (DAC/altavoz): AUDIO_CTRL_OUT = 1.
 *  2) Fijar volumen de salida.
 *  3) Activar stream I2S de reproduccion: AUDIO_CTRL_STREAM = 1.
 *  4) Escribir el buffer con write() en bucle hasta enviar m->len bytes.
 *     write() puede aceptar menos de lo pedido si el anillo se llena: hay que
 *     iterar. "Meter el buffer en el I2S" es esto: write() empuja el PCM al
 *     driver, que lo pone en el stream I2S -> codec ES8311 -> altavoz.
 *     El formato ya es el correcto (48 kHz, estereo, 16-bit), sin conversion.
 *  5) Esperar a que se reproduzca lo encolado: AUDIO_CTRL_STREAM_WAIT.
 *  6) Apagar stream y salida.
 * Devuelve OP_CONTINUE al terminar normal, u OP_FONDO/OP_CERRAR si se interrumpe.
 */
static int reproducir(struct micdemo *m)
{
    uint32_t enviado = 0;
    int op;

    if (m->len == 0) {
        set_text(m->lbl_estado, "Estado: nada grabado");
        m->estado = ST_IDLE;
        return OP_CONTINUE;
    }

    m->estado = ST_PLAYING;
    set_text(m->lbl_estado, "Estado: REPRODUCIENDO...");

    control(m->audiofd, AUDIO_CTRL_OUT, 1, 0);   /* (1) encender salida */
    aplicar_vol(m);                               /* (2) volumen */
    control(m->audiofd, AUDIO_CTRL_STREAM, 1, 0);/* (3) activar stream I2S */

    while (enviado < m->len) {                   /* (4) enviar todo el PCM */
        uint32_t pedir = m->len - enviado;
        int w;

        if (pedir > CHUNK_BYTES) pedir = CHUNK_BYTES;

        op = sondear_control(m);
        if (op != OP_CONTINUE) {
            control(m->audiofd, AUDIO_CTRL_STREAM, 0, 0);
            control(m->audiofd, AUDIO_CTRL_OUT, 0, 0);
            m->estado = ST_IDLE;
            set_text(m->lbl_estado, "Estado: listo");
            return op;
        }

        w = write(m->audiofd, m->pcm + enviado, (int)pedir);
        if (w > 0) {
            enviado += (uint32_t)w;
        } else {
            sleep(5); /* anillo lleno: esperar hueco y reintentar */
        }
    }

    control(m->audiofd, AUDIO_CTRL_STREAM_WAIT, 0, 0); /* (5) drenar */
    control(m->audiofd, AUDIO_CTRL_STREAM, 0, 0);      /* (6) parar */
    control(m->audiofd, AUDIO_CTRL_OUT, 0, 0);

    set_text(m->lbl_estado, "Estado: listo");
    m->estado = ST_IDLE;
    return OP_CONTINUE;
}

/* -------------------------------------------------------------------------- */
/*  MAIN                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    struct micdemo *m;
    ui_event_t ev;
    int op;

    (void)argc; (void)argv;

    /* Estado de la app en PSRAM. */
    m = (struct micdemo *)psram_malloc(sizeof(struct micdemo));
    if (m == NULL) return 1;
    memset(m, 0, sizeof(*m));
    m->slot = -1;
    m->estado = ST_IDLE;
    m->vol = VOL_DEF;
    m->mic_gain = MIC_GAIN_DEF;

    /* Buffer para 10 s de PCM crudo en PSRAM (se reproducira tal cual por I2S). */
    m->pcm = (uint8_t *)psram_malloc(REC_BYTES);
    if (m->pcm == NULL) { psram_free(m); return 1; }

    /* Crear UI sin chrome y registrar la app en el gestor (launcher). */
    m->ui = ui_create_ex(UI_CHROME_NONE);
    if (m->ui == NULL) { psram_free(m->pcm); psram_free(m); return 1; }
    m->slot = appmgr_register(APP_NAME, APP_PATH, APP_COLOR, gettid());
    dibujar_ui(m);

    /* Abrir el dispositivo de audio: permite read (grabar), write (reproducir)
     * y control (volumen, ganancia de micro, encender/apagar rutas y stream). */
    m->audiofd = getdev("/dev/audio0");
    if (m->audiofd == SYSERR) {
        set_text(m->lbl_estado, "Error: no /dev/audio0");
    } else {
        aplicar_vol(m);
        aplicar_mic(m);
    }

    /* Bucle principal: atiende gestos/botones. Gesto borde inferior hacia arriba
     * = ir a fondo (volver al launcher) de forma cooperativa, como otras apps. */
    for (;;) {
        if (m->slot >= 0 && appmgr_get_state(m->slot) == APP_CLOSING) {
            break;
        }

        while (ui_poll(m->ui, &ev)) {
            if (es_gesto_fondo(&ev)) {
                ir_fondo(m);
                continue;
            }

            if (ev.tipo == UI_EVENT_BUTTON && m->estado == ST_IDLE && m->audiofd != SYSERR) {
                if (ev.id == ID_BTN_REC) {
                    /* Flujo pedido: grabar 10 s y, al terminar, reproducir auto. */
                    op = grabar_10s(m);
                    if (op == OP_FONDO) { ir_fondo(m); continue; }
                    if (op == OP_CERRAR) goto salir;

                    if (m->len > 0 && !(m->slot >= 0 && appmgr_get_state(m->slot) == APP_CLOSING)) {
                        op = reproducir(m);
                        if (op == OP_FONDO) { ir_fondo(m); continue; }
                        if (op == OP_CERRAR) goto salir;
                    }
                } else if (ev.id == ID_BTN_VOL_DN) {
                    if (m->vol >= 10) m->vol -= 10; else m->vol = 0;
                    aplicar_vol(m); pintar_vol(m);
                } else if (ev.id == ID_BTN_VOL_UP) {
                    if (m->vol <= 90) m->vol += 10; else m->vol = 100;
                    aplicar_vol(m); pintar_vol(m);
                } else if (ev.id == ID_BTN_MIC_DN) {
                    if (m->mic_gain >= 2) m->mic_gain -= 2; else m->mic_gain = 0;
                    aplicar_mic(m); pintar_mic(m);
                } else if (ev.id == ID_BTN_MIC_UP) {
                    if (m->mic_gain <= 22) m->mic_gain += 2; else m->mic_gain = 24;
                    aplicar_mic(m); pintar_mic(m);
                }
            }
        }

        sleep(15); /* ceder CPU en reposo */
    }

salir:
    /* Limpieza: apagar rutas/stream de audio y liberar recursos. */
    if (m->audiofd != SYSERR) {
        control(m->audiofd, AUDIO_CTRL_IN, 0, 0);
        control(m->audiofd, AUDIO_CTRL_STREAM, 0, 0);
        control(m->audiofd, AUDIO_CTRL_OUT, 0, 0);
    }
    if (m->slot >= 0) appmgr_unregister(m->slot);
    ui_destroy(m->ui);
    if (m->pcm) psram_free(m->pcm);
    psram_free(m);
    return 0;
}
