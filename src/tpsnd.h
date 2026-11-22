// tpsnd: Time Pilot's sound for the cart (portable). The two AYs (tpay.c)
// are made into one of:
//
//   samples   tpsnd_pcm: the six outputs, each through the board's RC
//             filter, summed as MAME mixes them, at the lists' 5,962 Hz
//             (each sample the mean of TPSND_OVER bins), the resting level
//             taken off (a pole at 15 Hz) and scaled to the DAC's levels
//             (tpcv_sound_pcm: +-32767 is +-511 levels, its soft knee from
//             191). The plugin's way.
//   tones     tpsnd_tones: the AYs' state now as the SN76489's: the three
//             loudest of the six square tones (each weighed by its
//             filter at its pitch) on the PSG's three, their volumes as
//             attenuations. For the comparison (test/tp/soundcmp.sh).
//   a mix     tpsnd_mix: the loudest unfiltered tone on the PSG's tone 2,
//             the rest as samples through tones 0 and 1 parked (a DAC of
//             two). For the comparison.
//
// The game uses no noise and no envelope (the AY log of a game through
// two eras: the mixer at 0x38, the volumes plain), so the three ways
// differ only in the tones and the filters.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef TPSND_OVER
#define TPSND_OVER 2                    // bins a sample (11,925 a second)
#endif
// the samples' scale: SCALE / 8192 of the summed outputs (MAME's 1.0 is
// 32768) in tpcv's units (64 a DAC level). test/tp/soundcmp.sh measures
// the loudness against an ordinary cartridge's.
#ifndef TPSND_SCALE
#define TPSND_SCALE 8192
#endif

void tpsnd_init(void);                  // after tpay_reset
// the next n samples (the AYs' queue must hold the writes for their time)
void tpsnd_pcm(int16_t *out, int n);
// the AYs as they are after the samples so far, as the SN76489's eleven bytes
// (tone 0's two, its attenuation, tone 1's, tone 2's, then the noise's
// control and attenuation)
void tpsnd_tones(uint8_t psg[11]);
// the mix: n samples for a two-tone DAC (in its 0-255 levels' units, 64 a
// level as above), with the tone byte for the one tone played as a tone
void tpsnd_mix(int16_t *out, int n, uint8_t psg3[3]);
extern uint32_t tpsnd_samples;          // made so far
