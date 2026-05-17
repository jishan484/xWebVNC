#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <string.h>
#include <unistd.h>
#include "webvnc.h"

#define CAPTURE_RATE     44100
#define CAPTURE_CHANNELS 2
#define FRAME_SIZE       1024

#define TARGET_RATE      8000
#define TARGET_CHANNELS  1

#define OUT_FRAME_SIZE   2048   // fixed packet size after downsample
#define SILENCE_THRESHOLD 1    // amplitude threshold for silence

static Websocket *global_websocket;
int app_audio_active = 0;

/**
 * Downsample and convert to 8‑bit unsigned PCM.
 * Returns 1 if all samples are silent, 0 otherwise.
 */
int downsample_and_convert(short *in, int in_frames, unsigned char *out, int *out_samples);
int downsample_and_convert(short *in, int in_frames,
                           unsigned char *out, int *out_samples) {
    double step = (double)CAPTURE_RATE / TARGET_RATE;
    double pos = 0.0;
    int out_index = 0;
    int silent = 1;

    while ((int)pos < in_frames) {
        int i = (int)pos;
        int sample = (in[i * CAPTURE_CHANNELS] + in[i * CAPTURE_CHANNELS + 1]) / 2;
        int val = (sample / 256) + 128;
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        out[out_index++] = (unsigned char)val;

        // inline silence detection
        if (abs(val - 128) > SILENCE_THRESHOLD) {
            silent = 0;
        }
        pos += step;
    }
    *out_samples = out_index;
    return silent;
}

void* audioLoop(void *args) {
    global_websocket = args;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    short buffer[FRAME_SIZE * CAPTURE_CHANNELS];

    // ring buffer for outgoing samples
    unsigned char outbuf[OUT_FRAME_SIZE * 4];
    int writeIndex = 0, readIndex = 0;

    int rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "unable to open capture device: %s\n", snd_strerror(rc));
        return NULL;
    }

    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, CAPTURE_CHANNELS);
    unsigned int rate = CAPTURE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
    snd_pcm_hw_params_set_buffer_size(handle, params, FRAME_SIZE * 2);
    snd_pcm_hw_params_set_period_size(handle, params, FRAME_SIZE, 0);
    rc = snd_pcm_hw_params(handle, params);
    snd_pcm_hw_params_free(params);
    if (rc < 0) {
        fprintf(stderr, "unable to set hw params: %s\n", snd_strerror(rc));
        snd_pcm_close(handle);
        return NULL;
    }

    int count = snd_pcm_poll_descriptors_count(handle);
    struct pollfd pfds[count];
    snd_pcm_poll_descriptors(handle, pfds, count);

    while (app_running_indicator) {
        if(!app_audio_active){
            usleep(50000);
            continue;
        }
        rc = poll(pfds, count, 200);
        if (rc > 0 && app_audio_active) {
            rc = snd_pcm_readi(handle, buffer, FRAME_SIZE);
            if (rc > 0 && app_audio_active) {
                int out_samples;
                unsigned char tmp[FRAME_SIZE];
                int silent = downsample_and_convert(buffer, rc, tmp, &out_samples);

                // push into ring buffer
                for (int i = 0; i < out_samples; i++) {
                    outbuf[writeIndex++ % sizeof(outbuf)] = tmp[i];
                }

                // send fixed-size packets
                while ((writeIndex - readIndex + sizeof(outbuf)) % sizeof(outbuf) >= OUT_FRAME_SIZE && app_audio_active) {
                    if (!silent) {
                        ws_p_sendRaw(global_websocket, 130, "AUD\n",
                                     (char*)outbuf + readIndex, 4, OUT_FRAME_SIZE, -1);
                    }
                    readIndex = (readIndex + OUT_FRAME_SIZE) % sizeof(outbuf);
                }
            }
        }
    }
    
    snd_pcm_close(handle);
    return NULL;
}