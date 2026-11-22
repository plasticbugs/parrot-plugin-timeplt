// audio.c -- the console's sound, from the PSG bytes the kernels write.
//
// Each byte comes with the console time it reaches the chip (model.c: the
// frame kernel's from the list's own cycle costs, the screen kernel's when
// the plugin publishes them). Each TV frame, the samples up to that frame's
// start are made, the bytes applied at their times, and the samples go to
// SDL's audio device (live) and to the WAV (--wav). Live, the device sets
// the pace: the simulator waits while more than a few frames of sound are
// queued, so a frame lasts as long as on the console, 1/59.94 s.
#include "sim.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE 65536                         // PSG bytes waiting for their time
#define AHEAD_FRAMES 3                      // live: sound queued before the simulator waits

static psg_t psg;
static struct { double t; uint8_t b; } q[QUEUE];
static unsigned qh, qt;                     // head (oldest), tail
static int64_t samples;                     // made so far
static SDL_AudioStream *stream;
static FILE *wav;
static uint32_t wav_bytes;

static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void wav_header(void) {
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + wav_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    h[20] = 1; h[21] = 0;                   // PCM
    h[22] = 1; h[23] = 0;                   // mono
    le32(h + 24, SIM_RATE);
    le32(h + 28, SIM_RATE * 2);
    h[32] = 2; h[33] = 0;
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    le32(h + 40, wav_bytes);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, wav);
    fseek(wav, 0, SEEK_END);
}

void audio_open(void) {
    psg_init(&psg, SIM_RATE);
    if (opt.wav) {
        wav = fopen(opt.wav, "wb");
        if (!wav) { perror(opt.wav); exit(2); }
        wav_header();
    }
    if (opt.mute || !SDL_WasInit(SDL_INIT_AUDIO)) return;
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, SIM_RATE};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
}

void audio_write(double t_us, uint8_t b) {
    if (qt - qh == QUEUE) {                 // (a plugin that floods the chip: now, then)
        psg_write(&psg, q[qh % QUEUE].b);
        qh++;
    }
    q[qt % QUEUE].t = t_us;
    q[qt % QUEUE].b = b;
    qt++;
}

// n samples from start_us: the bytes applied at their sample, the samples to the WAV and speakers
static void render(double start_us, int n) {
    static int16_t buf[4096];
    const double step = 1e6 / SIM_RATE;
    int made = 0;
    while (made < n) {
        int upto = n;                       // the samples before the next byte's time, then the byte
        if (qh != qt) {
            const double at = (q[qh % QUEUE].t - start_us) / step;
            if (at < made + 1) {
                psg_write(&psg, q[qh % QUEUE].b);
                qh++;
                continue;
            }
            if (at < upto) upto = (int)at;
        }
        psg_render(&psg, buf + made, upto - made);
        made = upto;
    }
    if (wav) {
        fwrite(buf, 2, (size_t)n, wav);     // (little-endian on every machine this builds for)
        wav_bytes += (uint32_t)n * 2;
    }
    if (stream) {
        if (opt.headless) SDL_ClearAudioStream(stream);   // the dummy device: nobody listens
        else SDL_PutAudioStreamData(stream, buf, n * 2);
    }
}

void audio_frame(uint64_t t_us) {
    // sample k is at k / SIM_RATE seconds of console time
    const int64_t upto = (int64_t)((double)t_us * SIM_RATE / 1e6);
    while (samples < upto) {                // (in pieces: the gap before a slow start is long)
        const int k = upto - samples < 4096 ? (int)(upto - samples) : 4096;
        render((double)samples * 1e6 / SIM_RATE, k);
        samples += k;
    }
}

bool audio_live(void) { return stream && !opt.headless; }

void audio_pace(void) {
    const int ahead = (int)(AHEAD_FRAMES * SIM_RATE / 59.94) * 2;
    while (SDL_GetAudioStreamQueued(stream) > ahead) SDL_Delay(1);
}

void audio_close(void) {
    if (wav) {
        wav_header();
        fclose(wav);
        wav = NULL;
    }
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
}
