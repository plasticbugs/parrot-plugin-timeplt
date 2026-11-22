// tpsnd.c -- see tpsnd.h.
#include "tpsnd.h"

#include <math.h>
#include <string.h>

#include "tpay.h"
#include "tpcv.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SND_HOT __attribute__((section(".time_critical.tpsnd")))
#else
#define SND_HOT
#endif

// The bins' ticks: TPAY_RATE ticks a second, TPCV_PCM_N samples in a list
// of TPCV_LIST_T of the console's T, TPSND_OVER bins a sample. Kept as a
// whole part and a remainder, so nothing drifts.
#define TICKS_NUM ((uint64_t)TPAY_RATE * TPCV_LIST_T)
#define TICKS_DEN ((uint64_t)TPCV_PCM_N * TPCV_CONSOLE_HZ * TPSND_OVER)
static uint32_t bin_q, bin_r, bin_rem;
static float bin_rate;                  // bins a second

uint32_t tpsnd_samples;

static uint16_t filter_seen = 0xffff;
static float fk[6], fy[6];              // each output's RC filter: its coefficient and its state
static float hp_x, hp_y, hp_a;          // the high-pass (the resting level off)
// DAC levels for 1.0 of MAME's (its 32768). 1900 puts the lists' sound,
// replayed on MAME's ColecoVision, at -24.5 dBFS rms over the scripted
// game (test/tp/soundcmp.sh, tools/wavlevel.py), where four ordinary
// cartridges are -20.5 to -23.9 (their mean -22.4): about as far under
// them as Donkey Kong's. 11% of the samples go into the DAC's soft knee
// (tpcv.c). 1500 is -25.7 with 7.6% in the knee, 2200 -23.8 with 13%; the
// sound's spectrum against MAME's hardly changes (5.5 to 5.7 dB apart).
#ifndef GAIN_LEVELS
#define GAIN_LEVELS 1900.0f
#endif

void tpsnd_init(void) {
    bin_q = (uint32_t)(TICKS_NUM / TICKS_DEN);
    bin_r = (uint32_t)(TICKS_NUM % TICKS_DEN);
    bin_rem = 0;
    bin_rate = (float)((double)TPCV_PCM_N * TPCV_CONSOLE_HZ * TPSND_OVER / TPCV_LIST_T);
    filter_seen = 0xffff;
    memset(fy, 0, sizeof fy);
    hp_x = hp_y = 0;
    hp_a = expf(-2.0f * 3.14159265f * 15.0f / ((float)TPCV_PCM_N * TPCV_CONSOLE_HZ / TPCV_LIST_T));
    tpsnd_samples = 0;
}

static inline int next_bin(void) {
    int n = (int)bin_q;
    bin_rem += bin_r;
    if (bin_rem >= TICKS_DEN) { bin_rem -= (uint32_t)TICKS_DEN; n++; }
    return n;
}

// One sample of the board's sound in MAME's units: the six outputs' bins,
// each times 0.6 through its filter, summed, muted with the amplifier
static SND_HOT float board_sample(float *ch_out) {
    float sum = 0;
    float ch[6] = {0, 0, 0, 0, 0, 0};
    for (int b = 0; b < TPSND_OVER; b++) {
        float o[6];
        tpay_mean(next_bin(), o);
        if (tpay.filter != filter_seen) {
            filter_seen = tpay.filter;
            for (int i = 0; i < 6; i++) fk[i] = tpay_filter_k(i, (uint32_t)bin_rate);
        }
        float s = 0;
        for (int i = 0; i < 6; i++) {
            float x = o[i] * 0.60f;
            fy[i] += (x - fy[i]) * fk[i];
            s += fy[i];
            ch[i] += fy[i];
        }
        if (!tpay.mute) sum += s;
    }
    if (ch_out)
        for (int i = 0; i < 6; i++) ch_out[i] = tpay.mute ? 0 : ch[i] * (1.0f / TPSND_OVER);
    return sum * (1.0f / TPSND_OVER);
}

static inline int16_t to_levels(float x) {
    // the high-pass: a pole at 15 Hz
    hp_y = hp_a * (hp_y + x - hp_x);
    hp_x = x;
    float v = hp_y * GAIN_LEVELS * 64.0f * (float)TPSND_SCALE / 8192.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    return (int16_t)lrintf(v);
}

void SND_HOT tpsnd_pcm(int16_t *out, int n) {
    for (int k = 0; k < n; k++) out[k] = to_levels(board_sample(NULL));
    tpsnd_samples += (uint32_t)n;
}

// ---- tones ----------------------------------------------------------------------------
// a channel's square as heard: its swing (MAME's units, after the 0.6),
// its filter's gain at its pitch
static float heard(int ch, int *period) {
    const tpay_chip_t *c = &tpay.chip[ch / 3];
    int k = ch % 3;
    int p = c->tper[k] < 1 ? 1 : c->tper[k];
    *period = p;
    if ((c->regs[7] >> k) & 1) return 0;            // (tone off)
    int v = c->tvol[k] & 0x0f;
    float swing = (tpay_vol_table[v] - tpay_vol_table[0]) * 0.60f;
    int shift = ch < 3 ? 6 + 2 * ch : 2 * (ch - 3);
    int d = (tpay.filter >> shift) & 3;
    float cap = (d & 1 ? 220e-9f : 0) + (d & 2 ? 47e-9f : 0);
    if (cap > 0) {
        float rc = 1000.0f * 5100.0f / 6100.0f * cap;
        float f = (float)TPAY_RATE / (2.0f * (float)p);
        float w = 2.0f * 3.14159265f * f * rc;
        swing /= sqrtf(1.0f + w * w);
    }
    return swing;
}

// the SN76489's swing at attenuation 0 in MAME's units (as tpsnd's samples
// would have it): the tone a square from 0 to the ladder's 1.0, the DAC's
// 2.40 is 511 levels
#define SN_SWING (1.0f / (2.40f / 511.0f) / GAIN_LEVELS)

static int8_t voice_of[6] = {-1, -1, -1, -1, -1, -1};

static uint8_t atten(float swing) {
    if (swing <= 0) return 15;
    float a = -10.0f * log10f(swing / SN_SWING);    // 2 dB a step
    int ai = (int)lrintf(a);
    return (uint8_t)(ai < 0 ? 0 : ai > 15 ? 15 : ai);
}

void tpsnd_tones(uint8_t psg[11]) {
    float sw[6];
    int per[6];
    for (int ch = 0; ch < 6; ch++) sw[ch] = heard(ch, &per[ch]);
    // the three loudest; a channel keeps its voice while it stays in them
    bool pick[6] = {false};
    for (int n = 0; n < 3; n++) {
        int best = -1;
        for (int ch = 0; ch < 6; ch++)
            if (!pick[ch] && sw[ch] > 0 && (best < 0 || sw[ch] > sw[best])) best = ch;
        if (best >= 0) pick[best] = true;
    }
    int8_t owner[3] = {-1, -1, -1};
    for (int ch = 0; ch < 6; ch++)
        if (pick[ch] && voice_of[ch] >= 0) owner[voice_of[ch]] = (int8_t)ch;
        else voice_of[ch] = -1;
    for (int ch = 0; ch < 6; ch++)
        if (pick[ch] && voice_of[ch] < 0)
            for (int v = 0; v < 3; v++)
                if (owner[v] < 0) { owner[v] = (int8_t)ch; voice_of[ch] = (int8_t)v; break; }
    for (int v = 0; v < 3; v++) {
        int ch = owner[v];
        int p = ch >= 0 ? per[ch] : 1;
        if (p > 1023) p = 1023;
        uint8_t a = ch >= 0 ? atten(sw[ch]) : 15;
        psg[3 * v] = (uint8_t)(0x80 | v << 5 | (p & 15));
        psg[3 * v + 1] = (uint8_t)((p >> 4) & 63);
        psg[3 * v + 2] = (uint8_t)(0x90 | v << 5 | a);
    }
    psg[9] = 0xE4;
    psg[10] = 0xFF;
}

// ---- the mix ----------------------------------------------------------------------------
void SND_HOT tpsnd_mix(int16_t *out, int n, uint8_t psg3[3]) {
    // the loudest unfiltered tone, as a tone
    int best = -1, per[6];
    float sw[6];
    for (int ch = 0; ch < 6; ch++) {
        sw[ch] = heard(ch, &per[ch]);
        int shift = ch < 3 ? 6 + 2 * ch : 2 * (ch - 3);
        if ((tpay.filter >> shift) & 3) continue;
        if (sw[ch] > 0 && (best < 0 || sw[ch] > sw[best])) best = ch;
    }
    int p = best >= 0 ? (per[best] > 1023 ? 1023 : per[best]) : 1;
    psg3[0] = (uint8_t)(0xC0 | (p & 15));
    psg3[1] = (uint8_t)((p >> 4) & 63);
    psg3[2] = (uint8_t)(0xD0 | (best >= 0 ? atten(sw[best]) : 15));
    // the rest as samples
    for (int k = 0; k < n; k++) {
        float ch[6];
        float s = board_sample(ch);
        if (best >= 0) s -= ch[best];
        out[k] = to_levels(s);
    }
    tpsnd_samples += (uint32_t)n;
}
