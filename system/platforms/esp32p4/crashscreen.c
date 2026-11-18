/**
 * @file crashscreen.c
 *
 * Pantalla de fallo del kernel (estilo "pantalla azul") dibujada
 * directamente sobre el LCD cuando ocurre una excepción no manejada.
 * No usa LVGL (el estado puede estar corrupto) y dibuja con las primitivas
 * del panel (device/lcd-esp32p4). Tras mostrar la información espera una
 * pulsación táctil (o cualquier toque) para reiniciar el SoC.
 */
/* Embedded Xinu, ESP32-P4. */

#include <kernel.h>
#include <stddef.h>
#include <stdint.h>
#include <riscv.h>
#include <trap.h>
#include <device.h>
#include <lcd.h>
#include <touch.h>

/* Primitivas directas del panel (device/lcd-esp32p4/st7796.c). */
extern void lcdPanelFill(uint16_t color);
extern void lcdPanelFillRect(int x, int y, int w, int h, uint16_t color);
extern void lcdPanelText(const char *s, int x, int y, uint16_t fg, uint16_t bg);
extern void lcdPanelBacklight(bool on);

/* Reloj de bits SPI del panel (lcdSpi.c), por si hiciera falta asegurar. */
extern uint32_t lcdSpiClockHz(void);

/* Colores (RGB565). Azul tipo BSOD moderno y blanco. */
#define CRASH_BG     0x03DA  /* aprox. (0,120,215) */
#define CRASH_FG     0xFFFF  /* blanco */
#define CRASH_BTN    0xFFFF  /* borde/relleno botón */
#define CRASH_BTNFG  0x03DA  /* texto botón */

static void hex32(char *out, uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;

    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 8; i++)
    {
        out[2 + i] = h[(v >> ((7 - i) * 4)) & 0xF];
    }
    out[10] = '\0';
}

static void line_pair(int y, const char *label, uint32_t v, uint16_t fg, uint16_t bg)
{
    char buf[11];

    lcdPanelText(label, 16, y, fg, bg);
    hex32(buf, v);
    lcdPanelText(buf, 152, y, fg, bg);
}

static const char *cause_name(uint32_t cause)
{
    switch (cause)
    {
    case 0:  return "Inst. desalineada";
    case 1:  return "Fallo acceso inst.";
    case 2:  return "Inst. ilegal";
    case 3:  return "Breakpoint";
    case 4:  return "Carga desalineada";
    case 5:  return "Fallo acceso carga";
    case 6:  return "Store desalineado";
    case 7:  return "Fallo acceso store";
    case 8:  return "ecall U";
    case 9:  return "ecall S";
    case 11: return "ecall M";
    case 12: return "Fallo pag. inst.";
    case 13: return "Fallo pag. carga";
    case 15: return "Fallo pag. store";
    default: return "Desconocida";
    }
}

static void do_reset(void)
{
    volatile uint32_t *reg = (volatile uint32_t *)0x50111014UL;

    *reg = 0x2000UL; /* HPCORE0_SW_RESET */
    for (;;)
    {
        asm volatile("wfi");
    }
}

static void delay_crash(volatile uint32_t loops)
{
    while (loops--)
    {
        asm volatile("nop");
    }
}

/**
 * Muestra la pantalla de fallo y espera el reinicio.
 * No retorna. Pensada para llamarse con interrupciones deshabilitadas.
 */
void crash_screen(uint32_t cause, trap_frame_t *frame) __attribute__((noreturn));
void crash_screen(uint32_t cause, trap_frame_t *frame)
{
    uint32_t mtval = (uint32_t)read_csr(mtval);
    uint32_t mepc  = frame ? frame->mepc : (uint32_t)read_csr(mepc);
    char h[11];
    int y;
    int btnx = 40, btny = 410, btnw = 240, btnh = 44;

    /* Mejor esfuerzo: encender backlight y pintar sin pasar por LVGL/locks. */
    lcdPanelBacklight(true);
    lcdPanelFill(CRASH_BG);

    lcdPanelText(":(", 16, 24, CRASH_FG, CRASH_BG);
    lcdPanelText("XINU tuvo un problema", 16, 56, CRASH_FG, CRASH_BG);
    lcdPanelText("y necesita reiniciarse.", 16, 72, CRASH_FG, CRASH_BG);

    y = 112;
    lcdPanelText("Causa (mcause):", 16, y, CRASH_FG, CRASH_BG);
    hex32(h, cause);
    lcdPanelText(h, 152, y, CRASH_FG, CRASH_BG);
    lcdPanelText(cause_name(cause), 16, y + 16, CRASH_FG, CRASH_BG);

    y = 160;
    line_pair(y,      "PC (mepc):",   mepc,               CRASH_FG, CRASH_BG);
    line_pair(y + 16, "mtval:",       mtval,              CRASH_FG, CRASH_BG);
    if (frame)
    {
        line_pair(y + 32, "ra:",        frame->ra,          CRASH_FG, CRASH_BG);
        line_pair(y + 48, "a0:",        frame->a0,          CRASH_FG, CRASH_BG);
        line_pair(y + 64, "a1:",        frame->a1,          CRASH_FG, CRASH_BG);
        line_pair(y + 80, "a2:",        frame->a2,          CRASH_FG, CRASH_BG);
        line_pair(y + 96, "a3:",        frame->a3,          CRASH_FG, CRASH_BG);
        line_pair(y + 112,"a4:",        frame->a4,          CRASH_FG, CRASH_BG);
        line_pair(y + 128,"a5:",        frame->a5,          CRASH_FG, CRASH_BG);
        line_pair(y + 144,"a6:",        frame->a6,          CRASH_FG, CRASH_BG);
        line_pair(y + 160,"a7:",        frame->a7,          CRASH_FG, CRASH_BG);
        line_pair(y + 176,"mstatus:",   frame->mstatus,     CRASH_FG, CRASH_BG);
    }

    /* Boton de reinicio */
    lcdPanelFillRect(btnx, btny, btnw, btnh, CRASH_BTN);
    lcdPanelText("REINICIAR", btnx + 72, btny + 14, CRASH_BTNFG, CRASH_BTN);
    lcdPanelText("Toca el boton o la pantalla", 16, 380, CRASH_FG, CRASH_BG);
    lcdPanelText("para reiniciar el sistema.", 16, 396, CRASH_FG, CRASH_BG);

    /* Espera toque (best-effort). Cualquier toque reinicia. */
    for (;;)
    {
        touch_point_t pt;
        int n;

        n = control(TOUCH0, TOUCH_CTRL_READ, (long)&pt, 1);
        if (n > 0)
        {
            /* Acepta toque dentro del boton o en cualquier parte. */
            (void)btnx; (void)btny; (void)btnw; (void)btnh;
            delay_crash(200000); /* pequeno debounce */
            do_reset();
        }
        delay_crash(50000);
    }
}
