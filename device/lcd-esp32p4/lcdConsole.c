/**
 * @file lcdConsole.c
 *
 * Text console on the ST7796 panel: the characters the kernel prints are
 * mirrored here so the boot is readable on the board's own screen.
 *
 * The console keeps a 40x30 cell grid of the panel's built-in 8x16 font.
 * Every character updates that grid, and only the cell it lands on is
 * redrawn, so the cost of a line is 40 glyph bursts rather than a screen
 * refresh.  Scrolling is the exception: the panel has no read-back here, so
 * the grid is the only memory of what is on screen and a scroll repaints all
 * thirty rows.
 *
 * There is no lock.  kprintf() calls the sink with interrupts disabled, and
 * a monitor can only be taken with interrupts enabled (wait() would otherwise
 * block the caller forever); output from two threads at once can therefore
 * interleave on the screen exactly as it already does on the serial line.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <kernel.h>
#include <device.h>
#include <lcd.h>
#include <string.h>

#include "lcdhw.h"

#define CON_COLS    (LCD_W / LCD_TEXT_W)        /* 40 cells          */
#define CON_ROWS    (LCD_H / LCD_TEXT_H)        /* 30 rows           */
#define CON_TAB     8                           /* tab stops in cells */

/** What is on the panel.  Space-padded, NUL-terminated per row so a row can
 *  be handed to lcdPanelText() as one string. */
static char conGrid[CON_ROWS][CON_COLS + 1];
static int conCol;
static int conRow;

/** TRUE while live mirroring is on; the grid fills either way, drawing
 *  waits.  The panel is a boot display, so this starts FALSE and the shell
 *  paints the recorded log once through lcdConsolePaint(). */
static bool conDraw = FALSE;

/** ANSI (CS1) escape-sequence filter state.  0 = normal text, 1 = last
 *  character was ESC, 2 = inside "ESC [ ... final".  The panel has no VT100
 *  interpreter, so without this the banner's colour codes would be drawn as
 *  literal "[1;31m" text. */
static int conEsc = 0;

/** Repaint one cell of the panel from the grid. */
static void conDrawCell(int row, int col)
{
    char cell[2];

    cell[0] = conGrid[row][col];
    cell[1] = '\0';
    if (cell[0] == ' ')
    {
        lcdPanelFillRect(col * LCD_TEXT_W, row * LCD_TEXT_H,
                         LCD_TEXT_W, LCD_TEXT_H, C_BLACK);
        return;
    }
    /* FASE 2: texto verde (como el fbcon de LK), fondo negro. */
    lcdPanelText(cell, col * LCD_TEXT_W, row * LCD_TEXT_H, C_GREEN, C_BLACK);
}

/** Repaint one row of the panel from the grid.
 *
 * Cells the console never wrote to still hold a NUL, so the row is copied out
 * with anything non-printable turned into a space before it goes to the panel.
 *
 * The trailing spaces are then erased with one run instead of drawn glyph by
 * glyph: boot lines are short, and a glyph costs a window setup plus 256
 * bytes on a 20 MHz bus, so skipping them is what keeps a scroll from
 * taking seconds.
 */
static void conRepaintRow(int row)
{
    char line[CON_COLS + 1];
    int len = CON_COLS;
    int i;
    char ch;

    for (i = 0; i < CON_COLS; i++)
    {
        ch = conGrid[row][i];
        line[i] = ((ch >= ' ' && ch <= '~') ? ch : ' ');
    }

    while (len > 0 && line[len - 1] == ' ')
    {
        len--;
    }
    line[len] = '\0';

    if (len > 0)
    {
        lcdPanelText(line, 0, row * LCD_TEXT_H, C_GREEN, C_BLACK);
    }
    if (len < CON_COLS)
    {
        lcdPanelFillRect(len * LCD_TEXT_W, row * LCD_TEXT_H,
                         (CON_COLS - len) * LCD_TEXT_W, LCD_TEXT_H, C_BLACK);
    }
}

/** Repaint the whole panel from the grid. */
static void conRepaint(void)
{
    int row;

    for (row = 0; row < CON_ROWS; row++)
    {
        conRepaintRow(row);
    }
}

/** Move the text up one row: the grid shifts, the panel is repainted. */
static void conScroll(void)
{
    memmove(&conGrid[0][0], &conGrid[1][0],
            (size_t)(CON_ROWS - 1) * (CON_COLS + 1));
    memset(&conGrid[CON_ROWS - 1][0], ' ', CON_COLS);
    conRow = CON_ROWS - 1;
    /* With mirroring off the panel is a frozen boot display: the grid keeps
     * the last lines so a later lcdConsolePaint() can show them, but the
     * panel itself must not be touched or the shell would redraw all thirty
     * rows on every newline. */
    if (conDraw)
    {
        conRepaint();
    }
}

/** Start a new line, scrolling the console when it is full. */
static void conNewline(void)
{
    conCol = 0;
    if (++conRow < CON_ROWS)
    {
        return;
    }
    conScroll();
}

/** Send a printable character to the end of the console text. */
static void conWrite(char ch)
{
    conGrid[conRow][conCol] = ch;
    if (conDraw)
    {
        conDrawCell(conRow, conCol);
    }

    if (++conCol >= CON_COLS)
    {
        conNewline();
    }
}

/**
 * @ingroup lcd
 *
 * Put one console character.  Callers are the kernel print sinks; the
 * character is always recorded, and only drawn once the panel is up, so the
 * lines printed before LCD init are on screen when it comes.
 *
 * @param c  the character (control codes \r, \n, \t and \b are honoured)
 */
void lcdConsolePutc(int c)
{
    int stop;
    uchar uc = (uchar)c;

    /* Swallow ANSI escape sequences: ESC [ <params> <final 0x40-0x7E>. */
    if (conEsc == 1)
    {
        conEsc = (uc == '[') ? 2 : 0;
        return;
    }
    if (conEsc == 2)
    {
        if (uc >= 0x40 && uc <= 0x7E)
        {
            conEsc = 0;
        }
        return;
    }
    if (uc == 0x1B)
    {
        conEsc = 1;
        return;
    }

    if (uc == '\n')
    {
        conNewline();
        return;
    }

    if ((uchar)c == '\r')
    {
        conCol = 0;
        return;
    }

    if ((uchar)c == '\t')
    {
        stop = ((conCol / CON_TAB) + 1) * CON_TAB;
        while (conCol < stop && conCol < CON_COLS)
        {
            conWrite(' ');
        }
        return;
    }

    if ((uchar)c == '\b')
    {
        if (conCol > 0)
        {
            conCol--;
            conWrite(' ');
            conCol--;
        }
        return;
    }

    if ((uchar)c < ' ' || (uchar)c == 0x7F)
    {
        return;
    }

    conWrite((char)c);
}

/**
 * @ingroup lcd
 *
 * Turn panel drawing on or off.  The console text itself is kept either way.
 *
 * @param on    TRUE to paint the grid on the panel (and repaint it whole, so
 *              what scrolled by while drawing was off comes back)
 */
void lcdConsoleEnable(bool on)
{
    if (!on)
    {
        conDraw = FALSE;
        return;
    }

    /* Repaint with drawing off so a character arriving mid-repaint only
     * updates the grid, then let the live mirror take over. */
    conDraw = FALSE;
    lcdConsolePaint();
    conDraw = TRUE;
}

/**
 * @ingroup lcd
 *
 * Paint what has been recorded so far, once, without touching the live
 * mirroring mode.  The shell calls this at startup so the panel keeps the
 * boot log on screen while the session itself stays on the UART.
 */
void lcdConsolePaint(void)
{
    lcdPanelFill(C_BLACK);
    conRepaint();
}

/**
 * @ingroup lcd
 *
 * @return TRUE while the live mirror of the kernel log is on.
 */
bool lcdConsoleEnabled(void)
{
    return conDraw;
}
