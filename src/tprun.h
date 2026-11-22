// tprun: the cart's loop, a list at a time, shared by the plugin and the
// Mac tests so both run the board the same way. Portable. (Donkey Kong's
// dkrun.c.)
//
// Each list runs the board for the list's time (TPCV_LIST_T of the
// console's T: 16.77 ms, a little more than the board's 16.67 ms frame),
// to a cycle count kept from the start so nothing drifts; TPCV_PCM_N
// samples of the sound go with the list (tpsnd.c), taken TPRUN_LEAD cycles
// behind the board so the AYs' writes for them are always made.
//
// The picture: the board at the last VBLANK in the list's time, copied at
// each VBLANK and kept for the list it belongs to (a run can pass the
// next list's VBLANK too). About one frame in 160 is never shown (60
// frames a second, 59.6 lists). A picture is made only when the console
// has had the last one's bytes (or TPRUN_SKIP lists have gone by): a
// picture that needs more than a list can carry is sent whole over the
// lists it takes, and the game runs on at its own speed meanwhile.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tp.h"
#include "tpcv.h"
#include "tpsnd.h"

#define TPRUN_LEAD 2600                 // main CPU cycles: 0.85 ms, about 5 samples
#ifndef TPRUN_SKIP
#define TPRUN_SKIP 4                    // a picture at least every 4 lists, sent or not
#endif

enum { TPRUN_PCM, TPRUN_TONES, TPRUN_MIX };

typedef struct {
    tpcv_t *cv;
    uint32_t lists;                     // since the start
    uint64_t target;                    // the main CPU cycle this list runs to
    uint32_t pictures;
    uint32_t waited;                    // lists that made no picture (the last one's bytes not all sent)
    uint8_t since;                      // lists since the last picture
    uint8_t sound;                      // TPRUN_PCM (the plugin's), or the tests' others
    int16_t pcm[TPCV_PCM_N];
    tpcv_board_t board[2];              // the board at the VBLANK for this list ([cur]), and one past its end
    bool have[2];
    uint8_t cur;
    const tpcv_board_t *shown;          // the one the last picture was made from (for the tests)
} tprun_t;

// the board started (tp_init and tpay_reset done)
void tprun_start(tprun_t *r, tpcv_t *cv);
// one list: the board for its time, then the list into out; returns its length
size_t tprun_list(tprun_t *r, uint8_t *out);
