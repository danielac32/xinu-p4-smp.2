/**
 * @file estado.h
 *
 * FASE 3 -- Barra de estado para apps ELF, SIN LVGL (dibujo con GFX_CTRL_*).
 * Muestra IP, enlace WiFi, senal y bateria.  Estado en handle de PSRAM.
 */
#ifndef APPLIB_ESTADO_H
#define APPLIB_ESTADO_H

#include <kernel.h>

typedef struct estado estado_t;
typedef estado_t statusbar_t;

#define ES_CAJA    0x2A1E10
#define ES_LATON   0xB08D57
#define ES_PAPEL   0xE8D8B0
#define ES_VERDE   0x9BD14F
#define ES_AMBAR   0xFFB302
#define ES_ROJO    0xC0392B
#define ES_APAGADO 0x4A3B28

#define ES_ALTO    62
#define ES_DATO_MS  1000U
#define ES_RSSI_MS  10000U

/* Compat */
#define SB_COLOR_BOX    ES_CAJA
#define SB_COLOR_BRASS  ES_LATON
#define SB_COLOR_TEXT   ES_PAPEL
#define SB_COLOR_OK     ES_VERDE
#define SB_COLOR_WARN   ES_AMBAR
#define SB_COLOR_BAD    ES_ROJO
#define SB_COLOR_OFF    ES_APAGADO
#define SB_HEIGHT       ES_ALTO

statusbar_t *statusbar_create(int x, int y, int w);
void statusbar_refresh(statusbar_t *sb);
void statusbar_destroy(statusbar_t *sb);

estado_t *estado_nueva(int x, int y, int w);
void estado_refrescar(estado_t *e);
void estado_cerrar(estado_t *e);

#endif /* APPLIB_ESTADO_H */
