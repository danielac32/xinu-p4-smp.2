/**
 * @file fslock.c
 *
 * Candado global del sistema de ficheros.  Se apoya en el candado reentrante
 * de la libreria FAT para evitar mezclar mutex no reentrantes.
 */
#include <kernel.h>
#include <fslock.h>
#include <fat.h>

void fs_lock(void)
{
    fatLibLock();
}

void fs_unlock(void)
{
    fatLibUnlock();
}
