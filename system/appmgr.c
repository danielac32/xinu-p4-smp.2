/**
 * @file appmgr.c
 *
 * Tabla fija de apps para el launcher.
 *
 * SMP: la tabla la leen/escriben dos cores -- el launcher y su vigia en
 * core1, las apps ELF (app_runner) en core0, y ademas todas las funciones
 * appmgr_* estan exportadas a los ELF (system/exports.c).  Antes el acceso
 * era "cooperativo" sin lock: dos cores podian dejar dos slots en RUNNING,
 * o una app escribir APP_BACKGROUND justo cuando el launcher la promovia a
 * RUNNING.  Todas las funciones toman ahora el BKL (disable/restore), que
 * es seguro aqui porque ninguna duerme.  Con esto take_foreground() es
 * atomico: el pase RUNNING->BACKGROUND de todos y la promocion del slot
 * ocurren sin que otro core pueda intercalarse.
 */
#include <kernel.h>
#include <interrupt.h>
#include <string.h>
#include <thread.h>
#include <appmgr.h>

static struct app_entry table[APPMGR_MAX];

struct app_entry *appmgr_table(void)
{
    return table;
}

int appmgr_count(void)
{
    return APPMGR_MAX;
}

int appmgr_find_path(const char *path)
{
    int i, found = SYSERR;
    irqmask im;

    if (NULL == path)
    {
        return SYSERR;
    }
    im = disable();
    for (i = 0; i < APPMGR_MAX; i++)
    {
        if (table[i].state != APP_FREE && 0 == strcmp(table[i].path, path))
        {
            found = i;
            break;
        }
    }
    restore(im);
    return found;
}

int appmgr_find_tid(tid_typ tid)
{
    int i, found = SYSERR;
    irqmask im;

    im = disable();
    for (i = 0; i < APPMGR_MAX; i++)
    {
        if (table[i].state != APP_FREE && table[i].tid == tid)
        {
            found = i;
            break;
        }
    }
    restore(im);
    return found;
}

int appmgr_register(const char *name, const char *path, uint32 color, tid_typ tid)
{
    int i, slot = SYSERR;
    irqmask im;

    im = disable();
    /* reutiliza la ranura existente de esa ruta, o busca una libre */
    i = appmgr_find_path(path);   /* anida el BKL (nest++), sin problema */
    if (i >= 0)
    {
        slot = i;
    }
    else
    {
        for (i = 0; i < APPMGR_MAX; i++)
        {
            if (APP_FREE == table[i].state)
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        restore(im);
        return SYSERR;
    }

    memset(&table[slot], 0, sizeof(table[slot]));
    if (name)
    {
        strncpy(table[slot].name, name, sizeof(table[slot].name) - 1);
    }
    if (path)
    {
        strncpy(table[slot].path, path, sizeof(table[slot].path) - 1);
    }
    table[slot].color = color;
    table[slot].tid = tid;
    table[slot].state = APP_RUNNING;
    table[slot].info = 0;
    restore(im);
    return slot;
}

int appmgr_unregister(int slot)
{
    irqmask im;

    if (slot < 0 || slot >= APPMGR_MAX)
    {
        return SYSERR;
    }
    im = disable();
    memset(&table[slot], 0, sizeof(table[slot]));
    table[slot].state = APP_FREE;
    restore(im);
    return OK;
}

int appmgr_get_state(int slot)
{
    int state;
    irqmask im;

    if (slot < 0 || slot >= APPMGR_MAX)
    {
        return APP_FREE;
    }
    im = disable();
    state = table[slot].state;
    restore(im);
    return state;
}

void appmgr_set_state(int slot, int state)
{
    irqmask im;

    if (slot < 0 || slot >= APPMGR_MAX)
    {
        return;
    }
    im = disable();
    table[slot].state = state;
    restore(im);
}

void appmgr_request_close(int slot)
{
    irqmask im;

    if (slot >= 0 && slot < APPMGR_MAX)
    {
        im = disable();
        table[slot].state = APP_CLOSING;
        restore(im);
    }
}

int appmgr_take_foreground(int slot)
{
    int i;
    irqmask im;

    if (slot < 0 || slot >= APPMGR_MAX)
    {
        return SYSERR;
    }
    im = disable();
    for (i = 0; i < APPMGR_MAX; i++)
    {
        if (APP_RUNNING == table[i].state)
        {
            table[i].state = APP_BACKGROUND;
        }
    }
    table[slot].state = APP_RUNNING;
    restore(im);
    return OK;
}

void appmgr_set_info(int slot, int info)
{
    irqmask im;

    if (slot >= 0 && slot < APPMGR_MAX)
    {
        im = disable();
        table[slot].info = info;
        restore(im);
    }
}

int appmgr_get_info(int slot)
{
    int info;
    irqmask im;

    if (slot < 0 || slot >= APPMGR_MAX)
    {
        return 0;
    }
    im = disable();
    info = table[slot].info;
    restore(im);
    return info;
}
