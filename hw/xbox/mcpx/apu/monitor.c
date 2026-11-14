/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "apu_int.h"
#include "qemu/error-report.h"

static int monitor_wanted_channels(void)
{
    return g_config.audio.channels == CONFIG_AUDIO_CHANNELS_SURROUND ? 6 : 2;
}

/* Open the output stream with 2 or 6 channels; SDL mixes 6 down when the
 * sound device has fewer speakers. */
static void monitor_open(MCPXAPUState *d, int channels)
{
    SDL_AudioSpec spec = {
        .freq = 48000,
        .format = SDL_AUDIO_S16LE,
        .channels = channels,
    };

    if (d->monitor.stream) {
        SDL_DestroyAudioStream(d->monitor.stream);
    }
    d->monitor.channels = channels;
    d->monitor.stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (d->monitor.stream == NULL && channels != 2) {
        warn_report("audio: no %d channel output (%s); using stereo",
                    channels, SDL_GetError());
        spec.channels = d->monitor.channels = 2;
        d->monitor.stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (d->monitor.stream == NULL) {
        return;
    }

    SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(d->monitor.stream);

    SDL_AudioSpec dev_spec;
    int dev_buf_frames = 0;
    int dev_drain_bytes = 0;
    if (SDL_GetAudioDeviceFormat(dev, &dev_spec, &dev_buf_frames)) {
        dev_drain_bytes = dev_buf_frames * spec.channels *
                          SDL_AUDIO_BYTESIZE(spec.format) *
                          spec.freq / dev_spec.freq;
    }
    int frame_bytes = 256 * spec.channels * SDL_AUDIO_BYTESIZE(spec.format);
    int drain = MAX(dev_drain_bytes, frame_bytes);
    d->monitor.queued_bytes_low = drain;
    d->monitor.queued_bytes_high = 3 * drain;

    SDL_ResumeAudioDevice(dev);
}

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp)
{
    d->monitor.stream = NULL;
    d->monitor.channels = 2;

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        error_setg(errp, "SDL_Init failed: %s", SDL_GetError());
        return;
    }

    monitor_open(d, monitor_wanted_channels());
    if (d->monitor.stream == NULL) {
        error_setg(errp, "SDL_OpenAudioDeviceStream failed: %s",
                   SDL_GetError());
        return;
    }
}

void mcpx_apu_monitor_finalize(MCPXAPUState *d)
{
    if (d->monitor.stream) {
        SDL_DestroyAudioStream(d->monitor.stream);
    }
}

/* xemu: 0..1, lowered by the UI for a moment (ui/xui/disc-refresh.cc mutes
 * the dashboard's menu sounds while it moves in the menu) */
float xemu_audio_duck = 1.0f;

void mcpx_apu_monitor_frame(MCPXAPUState *d)
{
    if ((d->ep_frame_div + 1) % 8) {
        return;
    }

    if (d->monitor.stream) {
        float vu = pow(fmax(0.0, fmin(g_config.audio.volume_limit, 1.0)), M_E);
        SDL_SetAudioStreamGain(d->monitor.stream, vu);

        /* The duck goes on the samples as they are made, not on the stream's
         * gain: SDL applies the gain when a sample leaves its queue, and the
         * queue can run a second behind, so a click made while muted played
         * after the sound came back. Ramped over the frame: no pop. */
        static float last_duck = 1.0f;
        float duck;
        __atomic_load(&xemu_audio_duck, &duck, __ATOMIC_RELAXED);
        duck = fmaxf(0.0f, fminf(duck, 1.0f));
        if (duck < 1.0f || last_duck < 1.0f) {
            for (int i = 0; i < 256; i++) {
                float g = last_duck + (duck - last_duck) * (i + 1) / 256.0f;
                for (int c = 0; c < 2; c++) {
                    d->monitor.frame_buf[i][c] =
                        (int16_t)(d->monitor.frame_buf[i][c] * g);
                }
                for (int c = 0; c < 6; c++) {
                    d->monitor.frame_buf51[i][c] =
                        (int16_t)(d->monitor.frame_buf51[i][c] * g);
                }
            }
        }
        last_duck = duck;
        if (d->monitor.channels == 6) {
            /* Fast sound mixed 6 channels itself; the DSP paths are stereo,
             * so they play on the front pair */
            if (d->monitor.point != MCPX_APU_DEBUG_MON_VP) {
                for (int i = 0; i < 256; i++) {
                    d->monitor.frame_buf51[i][0] = d->monitor.frame_buf[i][0];
                    d->monitor.frame_buf51[i][1] = d->monitor.frame_buf[i][1];
                }
            }
            SDL_PutAudioStreamData(d->monitor.stream, d->monitor.frame_buf51,
                                   sizeof(d->monitor.frame_buf51));
        } else {
            SDL_PutAudioStreamData(d->monitor.stream, d->monitor.frame_buf,
                                   sizeof(d->monitor.frame_buf));
        }
    }

    memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    memset(d->monitor.frame_buf51, 0, sizeof(d->monitor.frame_buf51));

    /* Stereo / 5.1 changed in the menu: reopen between frames */
    if (d->monitor.stream &&
        monitor_wanted_channels() != d->monitor.channels) {
        monitor_open(d, monitor_wanted_channels());
    }
}
