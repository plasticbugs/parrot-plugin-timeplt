// midi.c -- the plugin's USB MIDI, as this computer's MIDI ports, through
// RtMidi's C API (sim/third_party/rtmidi: CoreMIDI, ALSA, WinMM).
//
// What is on the cart's USB port, the desktop's ports stand in for:
//
//   MIDI_OUT   the cart plugged into a computer is a MIDI device there,
//              named after the plugin. Here: a virtual port of that name,
//              both ways: what the plugin sends comes out of it, and what
//              the computer's programs send to it goes to the plugin. Not
//              on Windows, which has no virtual ports: --midi-out NAME
//              sends to a port that is there instead.
//   MIDI_IN    a MIDI keyboard on the cart's hub. Here: this computer's
//              MIDI inputs, every one of them (--midi-in NAME: just one).
//
// What comes in is read once a TV frame and handed to the model
// (model_midi_in), which gives it to the plugin as cvusb_midi_read() would.
// What the plugin sends (host.c's SIM_MIDI hook) goes out at once, a
// message at a time, and to --midi-log.
//
// Headless, no port is opened unless --midi-in or --midi-out names one:
// tests read --midi-log instead, and never touch the computer's MIDI.
#include "sim.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvusb.h"                      // the plugin's CVUSB_* switches
#include "rtmidi_c.h"

#define MAX_INS 16
static RtMidiInPtr ins[MAX_INS];        // the inputs read, the virtual one too
static int n_ins;
static RtMidiOutPtr out;
static FILE *log_file;
static char summary[512] = "no MIDI ports";
static bool any_port;

// a call to RtMidi went wrong: say so, once per port
static bool ok(RtMidiPtr d, const char *what) {
    if (d && d->ok) return true;
    fprintf(stderr, "MIDI: %s: %s\n", what, d && d->msg && d->msg[0] ? d->msg : "no MIDI on this computer");
    return false;
}

static void port_name(RtMidiPtr d, unsigned i, char *buf, int size) {
    int len = size;
    buf[0] = 0;
    if (rtmidi_get_port_name(d, i, buf, &len) < 0) snprintf(buf, (size_t)size, "(port %u)", i);
}

// the port whose name is want (any case), or failing that has it in its
// name; -1 if none has
static int find_port(RtMidiPtr d, const char *want, char *name, int size) {
    const unsigned n = rtmidi_get_port_count(d);
    for (int pass = 0; pass < 2; pass++)
        for (unsigned i = 0; i < n; i++) {
            char low[256], w[256];
            port_name(d, i, name, size);
            snprintf(low, sizeof low, "%s", name);
            snprintf(w, sizeof w, "%s", want);
            for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
            for (char *p = w; *p; p++) *p = (char)tolower((unsigned char)*p);
            if (pass == 0 ? !strcmp(low, w) : strstr(low, w) != NULL) return (int)i;
        }
    name[0] = 0;
    return -1;
}

static RtMidiInPtr new_in(void) {
    RtMidiInPtr d = rtmidi_in_create(RTMIDI_API_UNSPECIFIED, opt.name, 1024);
    if (!ok(d, "can't read MIDI")) { rtmidi_in_free(d); return NULL; }
    rtmidi_in_ignore_types(d, true, false, false);  // SysEx: the cart passes none in; the clock and the rest, yes
    return d;
}

static void keep_in(RtMidiInPtr d) {
    if (n_ins < MAX_INS) ins[n_ins++] = d;
    else rtmidi_in_free(d);
}

static void list_names(RtMidiPtr d, const char *what) {
    const unsigned n = rtmidi_get_port_count(d);
    printf("MIDI %s:%s\n", what, n ? "" : " none");
    for (unsigned i = 0; i < n; i++) {
        char name[256];
        port_name(d, i, name, sizeof name);
        printf("  %s\n", name);
    }
}

void midi_list(void) {
    RtMidiInPtr in = rtmidi_in_create(RTMIDI_API_UNSPECIFIED, "parrotsim", 100);
    if (ok(in, "inputs")) list_names(in, "inputs (--midi-in NAME)");
    rtmidi_in_free(in);
    RtMidiOutPtr o = rtmidi_out_create(RTMIDI_API_UNSPECIFIED, "parrotsim");
    if (ok(o, "outputs")) list_names(o, "outputs (--midi-out NAME)");
    rtmidi_out_free(o);
}

void midi_open(void) {
    const bool midi = CVUSB_MIDI_IN || CVUSB_MIDI_OUT;
#ifdef _WIN32
    const bool can_virtual = false;
#else
    const bool can_virtual = true;
#endif
    const char *in_sel = opt.midi_in ? opt.midi_in : opt.headless || !CVUSB_MIDI_IN ? "none" : "all";
    const char *out_sel = opt.midi_out ? opt.midi_out : opt.headless || !CVUSB_MIDI_OUT || !can_virtual ? "none" : "virtual";
    char name[256], ins_said[256] = "";
    int o = 0;

    if (opt.midi_log && !(log_file = fopen(opt.midi_log, "wb"))) { perror(opt.midi_log); exit(2); }   // (\n, on Windows too)

    // the inputs, before this program's own virtual port is there to be found
    if (!strcmp(in_sel, "all")) {
        RtMidiInPtr probe = new_in();
        const unsigned n = probe ? rtmidi_get_port_count(probe) : 0;
        for (unsigned i = 0; i < n && n_ins < MAX_INS; i++) {
            RtMidiInPtr d = new_in();
            if (!d) break;
            port_name(probe, i, name, sizeof name);
            rtmidi_open_port(d, i, opt.name);
            if (!ok(d, name)) { rtmidi_in_free(d); continue; }
            keep_in(d);
            o += snprintf(ins_said + o, sizeof ins_said - (size_t)o, "%s\"%s\"", o ? ", " : "", name);
            if (o >= (int)sizeof ins_said) o = (int)sizeof ins_said - 1;
        }
        if (probe) rtmidi_in_free(probe);
    } else if (strcmp(in_sel, "none")) {
        RtMidiInPtr d = new_in();
        int i = d ? find_port(d, in_sel, name, sizeof name) : -1;
        if (d && i < 0) {
            fprintf(stderr, "--midi-in: no MIDI input is called \"%s\". There are:\n", in_sel);
            midi_list();
            exit(2);
        }
        if (d) {
            rtmidi_open_port(d, (unsigned)i, opt.name);
            if (ok(d, name)) {
                keep_in(d);
                snprintf(ins_said, sizeof ins_said, "\"%s\"", name);
            } else rtmidi_in_free(d);
        }
    }
    const int real_ins = n_ins;

    // out: a virtual port named after the plugin, both ways, as the cart on a
    // computer; or a port that is there
    char out_said[300] = "";
    if (!strcmp(out_sel, "virtual")) {
        if (!can_virtual) {
            fprintf(stderr, "--midi-out virtual: Windows has no virtual MIDI ports. --midi-out NAME sends to one\n"
                            "that is there (--midi-list lists them; loopMIDI makes one).\n");
            exit(2);
        }
        out = rtmidi_out_create(RTMIDI_API_UNSPECIFIED, opt.name);
        if (ok(out, "can't send MIDI")) {
            rtmidi_open_virtual_port(out, opt.name);
            if (ok(out, opt.name)) {
                snprintf(out_said, sizeof out_said, "a MIDI port called \"%s\"", opt.name);
                RtMidiInPtr d = new_in();       // and what the computer's programs send to it
                if (d) {
                    rtmidi_open_virtual_port(d, opt.name);
                    if (ok(d, opt.name)) keep_in(d);
                    else rtmidi_in_free(d);
                }
            } else { rtmidi_out_free(out); out = NULL; }
        } else { rtmidi_out_free(out); out = NULL; }
    } else if (strcmp(out_sel, "none")) {
        out = rtmidi_out_create(RTMIDI_API_UNSPECIFIED, opt.name);
        if (ok(out, "can't send MIDI")) {
            const int i = find_port(out, out_sel, name, sizeof name);
            if (i < 0) {
                fprintf(stderr, "--midi-out: no MIDI output is called \"%s\". There are:\n", out_sel);
                midi_list();
                exit(2);
            }
            rtmidi_open_port(out, (unsigned)i, opt.name);
            if (ok(out, name)) snprintf(out_said, sizeof out_said, "\"%s\"", name);
            else { rtmidi_out_free(out); out = NULL; }
        } else { rtmidi_out_free(out); out = NULL; }
    }
    any_port = n_ins || out;
    if (real_ins && CVUSB_MIDI_IN) model_midi_device();    // a MIDI keyboard on the hub from the start

    if (!midi && !any_port && !log_file) { summary[0] = 0; return; }
    snprintf(summary, sizeof summary, "MIDI out: %s; in: %s", out ? out_said : "nowhere", real_ins ? ins_said : "no ports");
    if (out && n_ins > real_ins) {
        const size_t l = strlen(summary);
        snprintf(summary + l, sizeof summary - l, "%s and the virtual port", real_ins ? "," : ";");
    }
    if (!opt.headless || any_port) printf("%s\n", summary);
#ifdef _WIN32
    if (!opt.headless && CVUSB_MIDI_OUT && !out)
        printf("(Windows has no virtual MIDI ports: --midi-out NAME sends the plugin's MIDI to a port that\n"
               " is there, a synth or a loopMIDI port. --midi-list lists them.)\n");
#endif
}

void midi_poll(void) {
    uint8_t buf[1024];
    for (int k = 0; k < n_ins; k++) {
        for (int guard = 0; guard < 4096; guard++) {
            size_t size = sizeof buf;
            rtmidi_in_get_message(ins[k], buf, &size);
            if (!ins[k]->ok) {          // too long for buf (SysEx is left out: it can't be): skip it
                uint8_t *big = size > sizeof buf ? malloc(size) : NULL;
                if (!big) break;
                rtmidi_in_get_message(ins[k], big, &size);
                free(big);
                continue;
            }
            if (!size) break;
            model_midi_in(buf, (unsigned)size);
        }
    }
}

// a message's length by its status: 0 for a byte that isn't one we send
static unsigned msg_len(uint8_t s) {
    if (s >= 0x80 && s < 0xF0) return (s & 0xE0) == 0xC0 ? 2 : 3;     // C0-DF: two bytes
    switch (s) {
    case 0xF1: case 0xF3: return 2;
    case 0xF2: return 3;
    case 0xF6: return 1;
    default: return s >= 0xF8 ? 1 : 0;
    }
}

static void out_one(const uint8_t *m, unsigned n) {
    if (rtmidi_out_send_message(out, m, (int)n) < 0) {
        static bool said;
        if (!said) fprintf(stderr, "MIDI out: %s\n", out->msg ? out->msg : "it failed");
        said = true;
    }
}

// whole messages, one at a time (a port takes one a call): running status
// written out in full, a SysEx from F0 to F7 as it is
static void out_messages(const uint8_t *b, unsigned n) {
    static uint8_t status;
    unsigned i = 0;
    while (i < n) {
        const uint8_t c = b[i];
        if (c >= 0xF8) { out_one(&c, 1); i++; continue; }      // real time: on its own
        if (c == 0xF0) {
            unsigned j = i + 1;
            while (j < n && b[j] != 0xF7) j++;
            if (j < n) out_one(b + i, j + 1 - i);
            i = j + 1;
            status = 0;
            continue;
        }
        uint8_t m[3];
        unsigned k = i;
        if (c < 0x80) {                         // running status
            if (!status) { i++; continue; }
            m[0] = status;
        } else {
            m[0] = c;
            status = c < 0xF0 ? c : 0;
            k++;
        }
        const unsigned len = msg_len(m[0]);
        unsigned have = 1;
        while (len && have < len && k < n && b[k] < 0x80) m[have++] = b[k++];
        if (len && have == len) out_one(m, len);
        i = k > i ? k : i + 1;
    }
}

void midi_send(const uint8_t *b, unsigned n, bool to_device, uint64_t t_us) {
    if (log_file) {
        fprintf(log_file, "%6u", (unsigned)(t_us / 1000));
        for (unsigned i = 0; i < n; i++) fprintf(log_file, " %02X", b[i]);
        fprintf(log_file, "%s\n", to_device ? " (to the MIDI device)" : "");
    }
    if (out) out_messages(b, n);
}

bool midi_ports_open(void) { return any_port; }

const char *midi_summary(void) { return summary; }

void midi_close(void) {
    for (int k = 0; k < n_ins; k++) rtmidi_in_free(ins[k]);
    n_ins = 0;
    if (out) rtmidi_out_free(out);
    out = NULL;
    if (log_file) fclose(log_file);
    log_file = NULL;
}
