// tprun.c -- see tprun.h.
#include "tprun.h"

#include <string.h>

static tprun_t *running;

// each VBLANK's board, for this list or (past its end) the next
static void vblank(void) {
    tprun_t *r = running;
    if (!r) return;
    int k = r->cur ^ (tp_time() > r->target - TPRUN_LEAD);
    tpcv_board(&r->board[k]);
    r->have[k] = true;
}

void tprun_start(tprun_t *r, tpcv_t *cv) {
    memset(r, 0, sizeof *r);
    r->cv = cv;
    running = r;
    tp_on_vblank = vblank;
    tpsnd_init();
    r->target = tp_time();
}

size_t tprun_list(tprun_t *r, uint8_t *out) {
    r->lists++;
    r->target = ((uint64_t)r->lists * TPCV_LIST_CYCLES_Q16 >> 16) + TPRUN_LEAD;
    if (r->have[r->cur ^ 1]) r->cur ^= 1;       // the last run passed this list's VBLANK
    tp_run_to(r->target);
    if (r->since < 255) r->since++;
    if (r->have[r->cur]) {
        if (!r->cv->dirty || r->since >= TPRUN_SKIP) {
            tpcv_picture(r->cv, &r->board[r->cur]);
            r->shown = &r->board[r->cur];
            r->pictures++;
            r->since = 0;
        } else {
            r->waited++;
        }
        r->have[r->cur] = false;
    }
    if (r->sound == TPRUN_PCM) {
        tpsnd_pcm(r->pcm, TPCV_PCM_N);
        tpcv_sound_pcm(r->cv, r->pcm, TPCV_PCM_N);
    } else if (r->sound == TPRUN_TONES) {
        uint8_t psg[11];
        tpsnd_pcm(r->pcm, TPCV_PCM_N);          // (the AYs moved on to the list's end)
        tpsnd_tones(psg);
        tpcv_sound_psg(r->cv, psg);
    } else {
        uint8_t psg3[3];
        tpsnd_mix(r->pcm, TPCV_PCM_N, psg3);
        tpcv_sound_mix(r->cv, r->pcm, TPCV_PCM_N, psg3);
    }
    return tpcv_tv_frame(r->cv, out);
}
