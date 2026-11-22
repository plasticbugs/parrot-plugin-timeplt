// sndcmp.c -- a sound against MAME's -wavwrite, at the cart's sample rate.
// (Donkey Kong's, its lag found on the high-passed sounds.)
//
//   sndcmp MAME.wav OTHER.wav
//   sndcmp -raw MAME.wav OTHER.wav
//
// -raw: both at 48 kHz as they are, sample for sample (the board's own
// sound against MAME's): the lag that fits best (within 50 ms), the gain,
// the sound to difference ratio and the largest difference.
//
// Both go to mono and to the cart's 5,962.3 samples a second (each sample
// the mean of the 48 kHz samples in its time, as the cart's bins are made;
// a file at the cart's rate already is taken as it is). OTHER is lined up
// with MAME's (the lag that fits best, within 50 ms, to a 48 kHz sample:
// MAME's bins are tried from each of the 48 kHz samples in one of the
// cart's) and scaled by the gain that fits best. Both first go through the
// same high-pass (two poles at 20 Hz): the board's sound carries DC that MAME
// keeps and the cart's DAC can't (it takes it off with a pole at 15 Hz),
// and what is below 20 Hz isn't heard. The report: the sound to
// difference ratio over the whole and over 1 s pieces with sound (median),
// the same below 2 kHz, and the log-spectral distance (dB, frames of 512
// samples with sound, bands to 2.9 kHz, OTHER at MAME's loudness): how far
// the two sounds' spectra are, whatever their phase (square tones can't
// match MAME's waveforms sample for sample, so for them this is the number).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE (100.0 * 3579545.0 / 60036.0)

static double *bin(const double *x, long frames, int rate, int from, int *n);

static double *read_wav(const char *name, int *n, long *raw_n, int *raw_rate, double **raw) {
    FILE *f = fopen(name, "rb");
    if (!f) { fprintf(stderr, "sndcmp: can't read %s\n", name); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t)size);
    if (fread(d, 1, (size_t)size, f) != (size_t)size) exit(2);
    fclose(f);
    int ch = 1, rate = 0;
    long at = 12, data = -1, len = 0;
    while (at + 8 <= size) {
        long n = d[at + 4] | d[at + 5] << 8 | d[at + 6] << 16 | (long)d[at + 7] << 24;
        if (!memcmp(d + at, "fmt ", 4)) { ch = d[at + 10] | d[at + 11] << 8; rate = d[at + 12] | d[at + 13] << 8 | d[at + 14] << 16; }
        if (!memcmp(d + at, "data", 4)) { data = at + 8; len = n; break; }
        at += 8 + n + (n & 1);
    }
    if (data < 0) { fprintf(stderr, "sndcmp: no data in %s\n", name); exit(2); }
    if (data + len > size) len = size - data;
    long frames = len / 2 / ch;
    // the louder channel (MAME's file is stereo with the game in one)
    int pick = 0;
    double e[2] = {0, 0};
    for (long i = 0; i < frames && ch > 1; i += 7)
        for (int c = 0; c < 2; c++) { short v = (short)(d[data + (i * ch + c) * 2] | d[data + (i * ch + c) * 2 + 1] << 8); e[c] += (double)v * v; }
    if (ch > 1 && e[1] > e[0]) pick = 1;
    double *x = malloc(sizeof(double) * (size_t)frames);
    for (long i = 0; i < frames; i++) x[i] = (short)(d[data + (i * ch + pick) * 2] | d[data + (i * ch + pick) * 2 + 1] << 8);
    free(d);
    *raw = x;
    *raw_n = frames;
    *raw_rate = rate;
    if (abs(rate - (int)RATE) <= 1) { *n = (int)frames; return x; }
    return bin(x, frames, rate, 0, n);
}

// box bins at the cart's rate, from sample `from`
static double *bin(const double *x0, long frames, int rate, int from, int *n) {
    const double *x = x0 + from;
    frames -= from;
    int m = (int)(frames / (rate / RATE)) - 1;
    double *y = malloc(sizeof(double) * (size_t)m);
    for (int k = 0; k < m; k++) {
        double a = k * (double)rate / RATE, b = (k + 1) * (double)rate / RATE, s = 0;
        long i0 = (long)a, i1 = (long)b;
        for (long i = i0; i <= i1 && i < frames; i++) {
            double lo = i > a ? i : a, hi = i + 1 < b ? i + 1 : b;
            if (hi > lo) s += x[i] * (hi - lo);
        }
        y[k] = s / (b - a);
    }
    *n = m;
    return y;
}

static void highpass(double *x, int n, double hz) {     // two one-pole high-passes
    double a = exp(-2 * M_PI * hz / RATE);
    for (int p = 0; p < 2; p++) {
        double px = x[0], y = 0;
        for (int i = 0; i < n; i++) { y = a * (y + x[i] - px); px = x[i]; x[i] = y; }
    }
}

static void lowpass(double *x, int n, double hz) {      // three one-pole low-passes
    double a = 1 - exp(-2 * M_PI * hz / RATE);
    for (int p = 0; p < 3; p++) {
        double y = 0;
        for (int i = 0; i < n; i++) x[i] = y += a * (x[i] - y);
    }
}

static double sdr(const double *m, const double *o, int n, double g) {
    double s = 0, e = 0;
    for (int i = 0; i < n; i++) { s += m[i] * m[i]; e += (m[i] - g * o[i]) * (m[i] - g * o[i]); }
    return e > 0 ? 10 * log10(s / e) : 99;
}

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static int raw_cmp(const char *mname, const char *oname) {
    int nm, no, mrate, orate;
    long mn, on;
    double *mr, *orw;
    (void)read_wav(mname, &nm, &mn, &mrate, &mr);
    (void)read_wav(oname, &no, &on, &orate, &orw);
    if (mrate != orate) { fprintf(stderr, "sndcmp: the rates differ (%d, %d)\n", mrate, orate); return 2; }
    long n = mn < on ? mn : on;
    int best_lag = 0;
    double best = -1;
    long lo = mrate, hi = n - 3000;
    if (hi > 30L * mrate) hi = 30L * mrate;
    for (int lag = -2400; lag <= 2400; lag++) {
        double c = 0, oo = 0;
        for (long i = lo; i < hi; i++) { c += mr[i] * orw[i + lag]; oo += orw[i + lag] * orw[i + lag]; }
        double score = oo > 0 ? c / sqrt(oo) : 0;
        if (score > best) { best = score; best_lag = lag; }
    }
    long m = n - 2400 - (best_lag > 0 ? best_lag : 0);
    double sab = 0, sbb = 0, saa = 0;
    for (long i = 2400; i < m; i++) { double a = mr[i], b = orw[i + best_lag]; sab += a * b; sbb += b * b; saa += a * a; }
    double g = sbb > 0 ? sab / sbb : 1, e = 0, maxd = 0;
    for (long i = 2400; i < m; i++) {
        double d = mr[i] - g * orw[i + best_lag];
        e += d * d;
        if (fabs(d) > maxd) maxd = fabs(d);
    }
    printf("48 kHz sample for sample: lag %d samples, gain %.4f; sound to difference %.1f dB; largest difference %.0f (of 32767), rms sound %.0f\n",
           best_lag, g, e > 0 ? 10 * log10(saa / e) : 99.0, maxd, sqrt(saa / (double)(m - 2400)));
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "-raw")) return raw_cmp(argv[2], argv[3]);
    if (argc < 3) { fprintf(stderr, "usage: sndcmp [-raw] MAME.wav OTHER.wav\n"); return 2; }
    int nm, no, mrate, orate;
    long mraw_n, oraw_n;
    double *mraw, *oraw;
    double *m = read_wav(argv[1], &nm, &mraw_n, &mrate, &mraw), *o = read_wav(argv[2], &no, &oraw_n, &orate, &oraw);
    (void)oraw;
    // the lag: OTHER's sample i + lag is MAME's i (MAME's binned from `from`)
    int best_lag = 0, best_from = 0;
    double best = -1;
    // (the search on copies through the high-pass: a DC level, as the
    // ColecoVision's DAC has in MAME's recording, would swamp it)
    int steps = (int)(mrate / RATE + 1);
    double *oh = malloc(sizeof(double) * (size_t)no);
    memcpy(oh, o, sizeof(double) * (size_t)no);
    highpass(oh, no, 20);
    for (int from = 0; from < steps; from++) {
        int nf;
        double *mf = from ? bin(mraw, mraw_n, mrate, from, &nf) : m;
        if (!from) nf = nm;
        double *mh = malloc(sizeof(double) * (size_t)nf);
        memcpy(mh, mf, sizeof(double) * (size_t)nf);
        highpass(mh, nf, 20);
        int lo = (int)RATE, hi = nf < no ? nf : no;
        if (hi > 30 * (int)RATE) hi = 30 * (int)RATE;
        for (int lag = -300; lag <= 300; lag++) {
            double c = 0, oo = 0;
            for (int i = lo; i < hi - 400; i++) { c += mh[i] * oh[i + lag]; oo += oh[i + lag] * oh[i + lag]; }
            double score = oo > 0 ? c / sqrt(oo) : 0;
            if (score > best) { best = score; best_lag = lag; best_from = from; }
        }
        free(mh);
        if (from) free(mf);
    }
    free(oh);
    if (best_from) { free(m); m = bin(mraw, mraw_n, mrate, best_from, &nm); }
    int n = (nm < no - best_lag ? nm : no - best_lag) - 400;
    double *a = malloc(sizeof(double) * (size_t)n), *b = malloc(sizeof(double) * (size_t)n);
    for (int i = 0; i < n; i++) { a[i] = m[i]; b[i] = (i + best_lag >= 0) ? o[i + best_lag] : 0; }
    highpass(a, n, 20);
    highpass(b, n, 20);
    double sab = 0, sbb = 0, saa = 0;
    for (int i = 0; i < n; i++) { sab += a[i] * b[i]; sbb += b[i] * b[i]; saa += a[i] * a[i]; }
    double g = sbb > 0 ? sab / sbb : 1, loud = sbb > 0 ? sqrt(saa / sbb) : 1;
    // 1 s pieces with sound
    int sec = (int)RATE, np = 0;
    double pieces[4096];
    for (int k = 0; k + sec <= n && np < 4096; k += sec) {
        double s = 0;
        for (int i = k; i < k + sec; i++) s += a[i] * a[i];
        if (s / sec > 1e4) pieces[np++] = sdr(a + k, b + k, sec, g);
    }
    if (getenv("SNDCMP_PIECES"))         // each second: its ratio, the lag that fits it best, the loudness ratio
        for (int k = 0; k + sec <= n; k += sec) {
            double s = 0, best_s = -99, r = 0;
            int bl = 0;
            for (int i = k; i < k + sec; i++) { s += a[i] * a[i]; r += b[i] * b[i]; }
            for (int l = -20; l <= 20 && k + l >= 0 && k + sec + l < n; l++) {
                double v = sdr(a + k, b + k + l, sec, g);
                if (v > best_s) { best_s = v; bl = l; }
            }
            printf("  %3d s: %5.1f dB, best %5.1f dB at %+d; level %.0f, other/mame %.2f\n", k / sec, sdr(a + k, b + k, sec, g), best_s, bl,
                   sqrt(s / sec), s > 0 ? g * sqrt(r / s) : 0);
        }
    qsort(pieces, (size_t)np, sizeof(double), cmpd);
    printf("lag %.1f ms, gain %.3f; sound to difference: %.1f dB overall, %.1f dB the median of %d 1 s pieces with sound\n",
           (best_lag - best_from * RATE / mrate) * 1000 / RATE, g, sdr(a, b, n, g), np ? pieces[np / 2] : 0, np);
    // the log-spectral distance, 512-sample frames (Hann), bands of 11.6 Hz to 2.9 kHz
    enum { N = 512 };
    double lsd = 0;
    int frames = 0;
    static double w[N], cs[N][N / 2], sn[N][N / 2];
    for (int i = 0; i < N; i++) {
        w[i] = 0.5 - 0.5 * cos(2 * M_PI * i / N);
        for (int k = 0; k < N / 2; k++) { cs[i][k] = cos(2 * M_PI * i * k / N); sn[i][k] = sin(2 * M_PI * i * k / N); }
    }
    for (int f0 = 0; f0 + N <= n; f0 += N) {
        double e = 0;
        for (int i = 0; i < N; i++) e += a[f0 + i] * a[f0 + i];
        if (e / N < 1e4) continue;
        double d = 0;
        for (int k = 1; k < N / 2; k++) {
            double ra = 0, ia = 0, rb = 0, ib = 0;
            for (int i = 0; i < N; i++) {
                ra += w[i] * a[f0 + i] * cs[i][k]; ia += w[i] * a[f0 + i] * sn[i][k];
                rb += w[i] * loud * b[f0 + i] * cs[i][k]; ib += w[i] * loud * b[f0 + i] * sn[i][k];
            }
            double pa = 10 * log10(ra * ra + ia * ia + 1e3), pb = 10 * log10(rb * rb + ib * ib + 1e3);
            d += (pa - pb) * (pa - pb);
        }
        lsd += sqrt(d / (N / 2 - 1));
        frames++;
    }
    lowpass(a, n, 2000);
    lowpass(b, n, 2000);
    printf("below 2 kHz: %.1f dB; log-spectral distance %.1f dB (%d frames of 86 ms with sound)\n", sdr(a, b, n, g), frames ? lsd / frames : 0, frames);
    return 0;
}
