/**
 * @file audioInit.c
 *
 * El device `audio`: trae el codec ES8311 del bus I2C0 a traves del driver
 * crudo (device/audio/es8311.c).  Un codec ausente se informa y no es fatal.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <device.h>
#include <kernel.h>
#include <board.h>
#include <i2c.h>
#include <semaphore.h>
#include <audio.h>

/* Acceso serializado al dispositivo de audio: solo una tarea a la vez. */
semaphore audiolock = SYSERR;

/**
 * @ingroup audio
 *
 * El amplificador de la placa (AMP_POWER en GPIO53) no se toca aqui: su
 * secuencia va con la etapa de salida (audioOut.c), que lo conecta lo ultimo
 * al arrancar un patron y lo desconecta lo primero al pararlo, para que por el
 * altavoz no se oiga ni el arranque del DAC ni su cierre.  El pad queda como
 * lo dejo el reset --alta impedancia, amplificador apagado-- hasta entonces.
 *
 * @param devptr audio device table entry
 * @return OK
 */
devcall audioInit(device *devptr)
{
    (void)devptr;

    audiolock = semcreate(1);
    if (SYSERR == audiolock)
    {
        kprintf("[audio] no se pudo crear audiolock\n");
    }

    if (!es8311_init())
    {
        kprintf("[audio] nada en 0x%02x (codigo %d)\n", ES8311_ADDR0,
                i2c_last_error());
        return OK;
    }

    /* Sin patron montado el DAC no tiene nada que reproducir; dejarlo muteado
     * evita cualquier ruido de fondo de la etapa analoga. */
    es8311_set_mute(true);

    kprintf("[audio] ES8311 en 0x%02x: %u Hz, %u MCLK, muteado\n",
            ES8311_ADDR0, ES8311_SAMPLE_RATE_HZ, ES8311_MCLK_HZ);

    return OK;
}
