# RtMidi, for the simulator's MIDI ports

[RtMidi](https://github.com/thestk/rtmidi) 6.1.0 (2026-10-15), by Gary P.
Scavone and contributors, under its MIT-style license (`LICENSE`). The
simulator uses its C API (`rtmidi_c.h`) in `sim/midi.c`, and builds it
with the simulator (`sim/CMakeLists.txt`): CoreMIDI on macOS, ALSA on
Linux, WinMM on Windows.

From the release's `rtmidi-6.1.0.tar.gz`, whose SHA-256 the release page
gives as
`83c4bb79ae813020873b87e09de8dad979ff0994597cfd33f4a6c416e8c7fcd5`.
These five files are copied from it unchanged:

| File | SHA-256 |
|---|---|
| `RtMidi.cpp` | `2586f5c38ce53814badf2784a38c11e02176f7e87f6792a017fb1aa36715e0d1` |
| `RtMidi.h` | `d350813b155af65328593ecdede98ba6da527599bb2a7175dd875a7009dea847` |
| `rtmidi_c.cpp` | `4e1d82b63507db40b48de80beee4ff86089a6b19ea2c433decd00f955116f813` |
| `rtmidi_c.h` | `688e575f2aca412d769f05f7b7517746ab7b64d813901dd68a37235a4dc5e59e` |
| `LICENSE` | `7fa091d9c1aa492ebfcafa9f919d27bbce61e4f048ece2dfc3b7f93181e9e364` |

To move to a newer release, copy the same files from it, and update the
version, the checksums and `THIRD-PARTY.md`.
