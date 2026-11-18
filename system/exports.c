/**
 * @file exports.c
 *
 * La SDK de la que dispone una app en C cargada como ELF: cada entrada es el
 * nombre tal y como lo ve el compilador del app y la direccion REAL de la
 * funcion del kernel.  El enlazador del cargador (system/elfload.c, el cliente
 * es el comando 'elf' de shell/xsh_elf.c) resuelve contra esta tabla los
 * simbolos UND del binario; sin ella un app no tiene ni printf.
 *
 * Dos cosas hay que entender antes de tocar esta lista.
 *
 *   1. LA TABLA ES LA RAIZ QUE EVITA QUE LAS FUNCIONES DESAPAREZCAN.  El kernel
 *      se enlaza con -ffunction-sections y --gc-sections (compile/Makefile), asi
 *      que cualquier funcion a la que nadie llame NO esta en xinu.elf: no es que
 *      no se exporte, es que no existe.  Antes de este fichero 'suspend',
 *      'malloc', 'fl_fopen' y cerca del 60% de LVGL estaban fuera de la imagen.
 *      Referenciarlas aqui las mete.  O sea: esta tabla no describe la ABI, la
 *      DEFINE.  Se comprueba con
 *         nm compile/xinu.elf | grep -c fl_fopen
 *
 *   2. NO PUEDEN ESTAR LOS NOMBRES QUE SON MACRO O 'static inline'.  Ni
 *      lv_scr_act, ni lv_color_hex, ni lv_pct, ni LV_COLOR_*:
 *      en LVGL 8.3 eso se expande en el .c del app, y por eso tampoco hace falta
 *      exportarlo.  Lo mismo en la libc de Xinu ('syscall' es un typedef de int,
 *      y disable()/restore() vienen de system/arch/riscv/interrupt.h).  Por la
 *      misma razon los nombres de abajo no se han copiado de la documentacion:
 *      se han cruzado con 'nm' de los .o del arbol.  El bloque lv_ esta generado
 *      con ese criterio (script en examples/p4-elfsdk/mkexports.py) y solo con
 *      los nombres que se ven desde 'lvgl/lvgl.h' -- las cabeceras PRIVADAS del
 *      arbol de LVGL (src/misc/lv_lru.h y otras) definen funciones que un app no
 *      puede ni declarar, y 'gcc' las rechaza en la tabla.  A proposito se dejan
 *      fuera lv_draw_*, lv_gpu_*, lv_layer_*, lv_refr_* y lv_snapshot_*:
 *      es el interior del renderizador, una app no los llama, y exportarlos
 *      arrastraria el codigo de pintado que ya usa el device por su cuenta.
 *
 * Coste de la busqueda: lineal con strcmp, ~1.100 entradas por los 7-40 simbolos
 * sin resolver de un app normal.  Son decenas de miles de comparaciones UNA vez
 * por carga, tiempo de sobra a 320 MHz; no merece un arbol ni un hash.
 *
 * 'exit' NO esta aqui.  La pone el cargador como funcion propia (elf_exit en
 * system/elfload.c): imprime el codigo y mata la hebra del app con
 * kill(gettid()); como elf_run() le dejo a la hebra los campos
 * elf_img/elf_size, el kill() del kernel suelta el bloque de PSRAM.  Es la
 * unica entrada del enlazado que no apunta a una funcion publica del kernel.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <kernel.h>
#include <device.h>
#include <memory.h>
#include <monitor.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <clock.h>                /* clkcount / mdelay / udelay                    */
#include <timer.h>                /* millis                                          */
#include <espeth.h>               /* espethLinkIsUp                                  */
#include <network.h>              /* netLookup / netaddrsprintf: la IP de ETH0       */
#include <math.h>
#include <gfx.h>                     /* FASE 3: gfxInit/gfxControl (device GFX0)   */
#include <fat_filelib.h>            /* la API fl_* del filelib de FAT              */
#include <mp3.h>                    /* mp3_*: el reproductor del kernel            */
#include <elfload.h>                /* elf_run: el cargador entero                */
#include <appmgr.h>                 /* registro launcher <-> apps                 */
#include <fslock.h>                 /* candado FAT/microSD                        */
#include <exports.h>
#include <taskmon.h>              /* taskmon_get: la tabla de tareas hecha ELF  */

/* CPU% real: ver system/resched.c y system/platforms/esp32p4/riscvclk.c */
extern volatile unsigned long long cpu_cycles[];
extern unsigned long long mtime64_public(void);
#include <wifinet.h>                /* WiFi para las apps: escanear y asociar     */


/* Tres llamadas de hebra que el arbol define en system/ pero nunca declaro en
 * include/: nadie las llamaba y con --gc-sections no hacia falta.  Firmas
 * copiadas de system/suspend.c, system/resume.c y system/chprio.c; si alguno de
 * esos ficheros cambia, esto tiene que cambiar con el. */
extern syscall suspend(tid_typ);
extern syscall resume(tid_typ);
extern syscall chprio(tid_typ, int);

/* La lectura de senal del C6 vive en shell/xsh_wifi_scan.c y no tiene cabecera
 * propia: los mismos prototipos que declaran xsh_wifi.c y p4demoTask.c.  El RPC
 * 294 va por esp_hosted_ctrl_raw_request(), que reserva uid y msg_id en la capa
 * de control, asi que pedirlo con la pila en marcha ya no se come las tramas. */
extern int wifiSignalRead(int32 *rssi, uint32 *channel);
extern int wifiSignalQuality(int32 rssi);

static const struct export exports[] = {

    /* ---------------- Reproductor MP3 (system/mp3.c) ----------------
     * La app no decodifica: pide PCM a 48 kHz ya listo y lo escribe en
     * /dev/audio0.  Va en el kernel porque el texto de un app ELF corre sin
     * cache de instrucciones y minimp3 no lo aguanta.
     */
    { "mp3_abrir", (void *)mp3_abrir },
    { "mp3_cerrar", (void *)mp3_cerrar },
    { "mp3_hz", (void *)mp3_hz },
    { "mp3_canales", (void *)mp3_canales },
    { "mp3_kbps", (void *)mp3_kbps },
    { "mp3_ms_total", (void *)mp3_ms_total },
    { "mp3_ms_actual", (void *)mp3_ms_actual },
    { "mp3_leer", (void *)mp3_leer },
    { "mp3_rebobinar", (void *)mp3_rebobinar },

    /* ---------------- Hilos y planificador ---------------- */
    { "create", (void *)create },
    { "create_on", (void *)create_on },
    { "kill", (void *)kill },
    { "ready", (void *)ready },
    { "suspend", (void *)suspend },
    { "resume", (void *)resume },
    { "yield", (void *)yield },
    { "resched", (void *)resched },
    { "gettid", (void *)gettid },
    { "getprio", (void *)getprio },
    { "chprio", (void *)chprio },

    /* ---------------- Mensajeria entre hebras ---------------- */
    { "send", (void *)send },
    { "receive", (void *)receive },
    { "recvclr", (void *)recvclr },
    { "recvtime", (void *)recvtime },

    /* ---------------- Temporizacion ---------------- */
    { "sleep", (void *)sleep },
    { "unsleep", (void *)unsleep },
    { "millis", (void *)millis },
    { "mdelay", (void *)mdelay },
    { "udelay", (void *)udelay },
    { "clkcount", (void *)clkcount },
    /* CPU% real para apps (monitor de tareas): ciclos mtime acumulados por
     * ctxsw en resched.c y lectura de mtime de 64 bits. */
    { "mtime64_public", (void *)mtime64_public },
    { "cpu_cycles", (void *)cpu_cycles },

    /* ---------------- Sincronizacion ---------------- */
    { "semcreate", (void *)semcreate },
    { "semfree", (void *)semfree },
    { "semcount", (void *)semcount },
    { "wait", (void *)wait },
    { "signal", (void *)signal },
    { "signaln", (void *)signaln },
    { "moncreate", (void *)moncreate },
    { "monfree", (void *)monfree },
    { "moncount", (void *)moncount },
    { "lock", (void *)lock },
    { "unlock", (void *)unlock },


    /* ---------------- Memoria ---------------- */
    { "malloc", (void *)malloc },
    { "free", (void *)free },
    { "calloc", (void *)calloc },
    { "realloc", (void *)realloc },
    { "memget", (void *)memget },
    { "memfree", (void *)memfree },
    { "psram_malloc", (void *)psram_malloc },
    { "psram_free", (void *)psram_free },
    { "psram_calloc", (void *)psram_calloc },
    { "psram_realloc", (void *)psram_realloc },

    /* ---------------- Consola y formateo ---------------- */
    { "printf", (void *)printf },
    { "kprintf", (void *)kprintf },
    { "sprintf", (void *)sprintf },
    { "snprintf", (void *)snprintf },
    { "sscanf", (void *)sscanf },
    { "fprintf", (void *)fprintf },
    { "fputs", (void *)fputs },
    { "fgets", (void *)fgets },
    { "fgetc", (void *)fgetc },
    { "fputc", (void *)fputc },

    /* ---------------- Cadenas y bloques ---------------- */
    { "strlen", (void *)strlen },
    { "strnlen", (void *)strnlen },
    { "strcpy", (void *)strcpy },
    { "strncpy", (void *)strncpy },
    { "strcat", (void *)strcat },
    { "strncat", (void *)strncat },
    { "strcmp", (void *)strcmp },
    { "strncmp", (void *)strncmp },
    { "strchr", (void *)strchr },
    { "strrchr", (void *)strrchr },
    { "strstr", (void *)strstr },
    { "strlcpy", (void *)strlcpy },
    { "memset", (void *)memset },
    { "memcpy", (void *)memcpy },
    { "memmove", (void *)memmove },
    { "memcmp", (void *)memcmp },
    { "memchr", (void *)memchr },
    { "bzero", (void *)bzero },

    /* ---------------- Convertidores y orden ---------------- */
    { "atoi", (void *)atoi },
    { "atol", (void *)atol },
    { "abs", (void *)abs },
    { "labs", (void *)labs },
    { "qsort", (void *)qsort },
    { "rand", (void *)rand },
    { "srand", (void *)srand },

    /* ---------------- Dispositivos (la E/S de Xinu) ---------------- */
    { "open", (void *)open },
    { "close", (void *)close },
    { "read", (void *)read },
    { "write", (void *)write },
    { "control", (void *)control },
    { "getdev", (void *)getdev },
    { "seek", (void *)seek },

    /* ---------------- FAT (el filelib fl_*) ---------------- */
    { "fl_init", (void *)fl_init },
    { "fl_shutdown", (void *)fl_shutdown },
    { "fl_attach_media", (void *)fl_attach_media },
    { "fl_get_fs", (void *)fl_get_fs },
    { "fl_fopen", (void *)fl_fopen },
    { "fl_fclose", (void *)fl_fclose },
    { "fl_fread", (void *)fl_fread },
    { "fl_fwrite", (void *)fl_fwrite },
    { "fl_fseek", (void *)fl_fseek },
    { "fl_ftell", (void *)fl_ftell },
    { "fl_feof", (void *)fl_feof },
    { "fl_fflush", (void *)fl_fflush },
    { "fl_fgetc", (void *)fl_fgetc },
    { "fl_fgets", (void *)fl_fgets },
    { "fl_fputc", (void *)fl_fputc },
    { "fl_fputs", (void *)fl_fputs },
    { "fl_remove", (void *)fl_remove },
    { "fl_opendir", (void *)fl_opendir },
    { "fl_readdir", (void *)fl_readdir },
    { "fl_closedir", (void *)fl_closedir },
    { "fl_createdirectory", (void *)fl_createdirectory },
    { "fl_removedirectory", (void *)fl_removedirectory },
    { "fl_is_dir", (void *)fl_is_dir },
    { "fl_listdirectory", (void *)fl_listdirectory },

    /* ---------------- Matematicas en simple ---------------- */
    { "pow", (void *)pow },
    { "fabsf", (void *)fabsf },
    { "sqrtf", (void *)sqrtf },
    { "logarithm", (void *)logarithm },
    { "frexpf", (void *)frexpf },
    { "numtestf", (void *)numtestf },
    { "floorf", (void *)floorf },
    { "modff", (void *)modff },
    { "powf", (void *)powf },
    { "ldexpf", (void *)ldexpf },
    { "expf", (void *)expf },
    { "sinef", (void *)sinef },
    { "tanf", (void *)tanf },
    { "asinef", (void *)asinef },
    { "atangentf", (void *)atangentf },
    { "atanf", (void *)atanf },
    { "lowpass", (void *)lowpass },

    /* ---------------- Panel y red ---------------- */
    /* FASE 3: API gfx (device GFX0 sobre LCD0, sin LVGL).  Las apps dibujan
     * con control(GFX0, GFX_CTRL_*, ...) o con estos helpers. */
    { "gfxInit", (void *)gfxInit },
    { "gfxControl", (void *)gfxControl },
    { "espethLinkIsUp", (void *)espethLinkIsUp },

    /* ---------------- Estado de red para las apps ----------------
     * La banda de estado de un launcher (IP, enlace y rallitas de senal) se
     * monta con esto: netLookup(getdev("ETH0")) da el netif y netaddrsprintf
     * formatea su campo ip; wifiSignalRead pide el RSSI del AP asociado por el
     * RPC 294 y wifiSignalQuality lo pasa a 0..100.  El enlace no necesita
     * entrada nueva: espethLinkIsUp() ya es SDIO adjunto + STA asociado.
     */
    { "netLookup", (void *)netLookup },
    { "netaddrsprintf", (void *)netaddrsprintf },
    { "wifiSignalRead", (void *)wifiSignalRead },
    { "wifiSignalQuality", (void *)wifiSignalQuality },

    /* ---------------- WiFi de la app de ajustes ----------------
     * Los mismos pasos que `netboot': encender el C6, listar las redes del
     * escaneo y asociarse + DHCP.  La app no habla el RPC ni repite el join:
     * llama a estas tres, que son las que ya usa el arranque.
     */
    { "wifiAsegurar", (void *)wifiAsegurar },
    { "wifiScanFill", (void *)wifiScanFill },
    { "wifiRedConectar", (void *)wifiRedConectar },

    /* ---------------- Cargador de apps ELF ----------------
     * 'elf_run' es el cargador entero: lee el ELF de la microSD, lo coloca
     * en PSRAM, lo enlaza contra esta tabla, le parchea las relocaciones y
     * ejecuta su '_start' en la hebra que llama.  El 'exit' del app mata la
     * hebra (kill) y el kernel suelta la PSRAM; si el app devuelve, la
     * suelta elf_run().  Con esto un app puede lanzar a otro.
     */
    { "elf_run", (void *)elf_run },

    /* ---------------- Gestor de apps (launcher multitarea) ---------------- */
    { "appmgr_table", (void *)appmgr_table },
    { "appmgr_count", (void *)appmgr_count },
    { "appmgr_register", (void *)appmgr_register },
    { "appmgr_unregister", (void *)appmgr_unregister },
    { "appmgr_find_path", (void *)appmgr_find_path },
    { "appmgr_find_tid", (void *)appmgr_find_tid },
    { "appmgr_get_state", (void *)appmgr_get_state },
    { "appmgr_set_state", (void *)appmgr_set_state },
    { "appmgr_request_close", (void *)appmgr_request_close },
    { "appmgr_take_foreground", (void *)appmgr_take_foreground },
    { "appmgr_set_info", (void *)appmgr_set_info },
    { "appmgr_get_info", (void *)appmgr_get_info },


    /* ---------------- Sistema de ficheros (FAT/microSD) ---------------- */
    { "fs_lock", (void *)fs_lock },
    { "fs_unlock", (void *)fs_unlock },

    /* FASE 1: bloque LVGL (986 entradas) eliminado.  La API de dibujo entrara
     * en la Fase 2 como gfx/fbcon sobre LCD0 (control(LCD0, LCD_CTRL_*)). */

};

/** Numero de entradas: lo calcula el compilador, no se mantiene a mano. */
#define NEXPORTS  (sizeof(exports) / sizeof(exports[0]))

/**
 * Direccion del simbolo 'name', o NULL si el kernel no lo exporta.
 *
 * El cargador la llama una vez por cada simbolo UND del app.  Un NULL es abort
 * de carga con el nombre impreso: nunca un salto a 0, que es mcause=5 o
 * mcause=2 en la placa.
 */
void *export_resolve(const char *name)
{
    unsigned int i;

    if (NULL == name || '\0' == name[0])
    {
        return NULL;                /* el simbolo 0 de .symtab no es un externo   */
    }
    for (i = 0; i < NEXPORTS; i++)
    {
        if (strcmp(exports[i].name, name) == 0)
        {
            return (void *)exports[i].addr;
        }
    }
    return NULL;
}

/** Cuantas entradas hay: lo imprime la shell con 'elfexports'. */
unsigned int export_count(void)
{
    return (unsigned int)NEXPORTS;
}

/** Nombre de la entrada i, para el volcado de la tabla; NULL si i se pasa. */
const char *export_name(unsigned int i)
{
    return (i < NEXPORTS) ? exports[i].name : NULL;
}
