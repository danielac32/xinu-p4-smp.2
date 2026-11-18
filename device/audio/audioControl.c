/**
 * @file audioControl.c
 *
 * Device-level control del codec, de sus relojes y de la salida: volumen,
 * mute, ganancia del micro, arranque/medida del timing I2S y el patron que el
 * AHB-DMA reproduce en bucle.  Ver include/audio.h para los codigos.
 */
/* Embedded Xinu, Copyright (C) 2026.  All rights reserved. */

#include <device.h>
#include <semaphore.h>
#include <audio.h>

extern semaphore audiolock;

/**
 * @ingroup audio
 *
 * @param devptr audio device table entry
 * @param func   command (AUDIO_CTRL_*)
 * @param arg1   command argument
 * @param arg2   command argument
 * @return OK, un valor, o SYSERR para un comando desconocido
 */
devcall audioControl(device *devptr, int func, long arg1, long arg2)
{
    devcall rc;

    (void)devptr;
    (void)arg2;

    if (SYSERR != audiolock)
    {
        wait(audiolock);
    }

    switch (func)
    {
    case AUDIO_CTRL_VOLUME:
        rc = es8311_set_volume((uint32_t)arg1);
        break;

    case AUDIO_CTRL_GET_VOLUME:
        rc = es8311_get_volume();
        break;

    case AUDIO_CTRL_GET_VOLUME_DB:
        rc = es8311_get_volume_db();
        break;

    case AUDIO_CTRL_MUTE:
        rc = es8311_set_mute(arg1 != 0) ? OK : SYSERR;
        break;

    case AUDIO_CTRL_GET_MUTE:
        rc = es8311_get_mute();
        break;

    case AUDIO_CTRL_MIC_GAIN:
        rc = es8311_set_mic_gain((uint32_t)arg1) ? OK : SYSERR;
        break;

    case AUDIO_CTRL_OUT:
    {
        uint32_t applied;

        if (!audio_out_start((uint32_t)arg1, &applied, NULL))
        {
            rc = SYSERR;
        }
        else
        {
            rc = (devcall)applied;
        }
        break;
    }

    case AUDIO_CTRL_OUT_OFF:
        audio_out_stop();
        rc = OK;
        break;

    case AUDIO_CTRL_OUT_STAT:
        rc = (devcall)audio_out_status();
        break;

    case AUDIO_CTRL_OUT_DESC:
        rc = (devcall)audio_out_dma_desc();
        break;

    case AUDIO_CTRL_OUT_CNT:
        rc = (devcall)audio_out_sync_count();
        break;

    case AUDIO_CTRL_OUT_RING:
        rc = (devcall)audio_out_ring_frames();
        break;

    case AUDIO_CTRL_STREAM:
        if (arg1 != 0)
        {
            rc = audio_out_stream_start() ? OK : SYSERR;
        }
        else
        {
            audio_out_stop();
            rc = OK;
        }
        break;

    case AUDIO_CTRL_STREAM_WAIT:
        audio_out_stream_drain();
        rc = OK;
        break;

    case AUDIO_CTRL_OUT_QUEUE:
        rc = (devcall)audio_out_stream_queued();
        break;

    case AUDIO_CTRL_OUT_CAP:
        rc = (devcall)audio_out_stream_capacity();
        break;

    case AUDIO_CTRL_IN:
        if (arg1 != 0)
        {
            rc = audio_in_start() ? OK : SYSERR;
        }
        else
        {
            audio_in_stop();
            rc = OK;
        }
        break;

    case AUDIO_CTRL_IN_STAT:
        rc = (devcall)audio_in_status();
        break;

    case AUDIO_CTRL_IN_DESC:
        rc = (devcall)audio_in_dma_desc();
        break;

    case AUDIO_CTRL_IN_DROP:
        rc = (devcall)audio_in_dropped();
        break;

    case AUDIO_CTRL_IN_RING:
        rc = (devcall)audio_in_ring_bytes();
        break;

    case AUDIO_CTRL_MIC_PGA:
        rc = es8311_set_mic_pga((uint32_t)arg1) ? OK : SYSERR;
        break;

    case AUDIO_CTRL_MIC_GET:
        rc = es8311_get_mic_pga();
        break;

    case AUDIO_CTRL_MIC_STAT:
        rc = es8311_mic_status();
        break;

    case AUDIO_CTRL_MIC_ADCSV:
        rc = es8311_get_mic_volume();
        break;

    case AUDIO_CTRL_MIC_SCALE:
        rc = es8311_get_mic_scale();
        break;

    case AUDIO_CTRL_STREAM_BASE:
        rc = (devcall)audio_out_stream_base();
        break;

    case AUDIO_CTRL_STREAM_GAPS:
        rc = (devcall)audio_out_stream_gaps();
        break;

    case AUDIO_CTRL_IN_BASE:
        rc = (devcall)audio_in_ring_base();
        break;

    case AUDIO_CTRL_IN_EOF:
        rc = (devcall)audio_in_eof_desc();
        break;

    case AUDIO_CTRL_IN_QUEUE:
        rc = (devcall)audio_in_queued();
        break;

    case AUDIO_CTRL_CLOCKS:
        if (arg1 != 0)
        {
            rc = i2s_clocks_start() ? OK : SYSERR;
        }
        else
        {
            (void)i2s_clocks_stop();
            rc = OK;
        }
        break;

    case AUDIO_CTRL_BCLK:
    {
        uint32_t hz = i2s_bclk_measure((uint32_t)arg1);

        rc = (hz != 0U) ? (devcall)hz : SYSERR;
        break;
    }

    default:
        rc = SYSERR;
        break;
    }

    if (SYSERR != audiolock)
    {
        signal(audiolock);
    }

    return rc;
}
