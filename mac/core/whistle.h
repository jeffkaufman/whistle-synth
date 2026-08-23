// The C side of the Mac app: owns the engine, the audio device, and the
// handoff between the UI thread and the audio thread.
//
// Everything here is safe to call from the main thread while audio is
// running.  Nothing here allocates or locks on the audio thread.
//
// The engine itself (pitch.c, synth.c, engine.c) knows nothing about any of
// this and is shared verbatim with the command-line build.
#ifndef WHISTLE_H
#define WHISTLE_H

#include <stdbool.h>

#include "synth.h"

#define WHISTLE_NAME_MAX 128
#define WHISTLE_UID_MAX  256
#define WHISTLE_MAX_DEVICES 64

/* -------------------------------------------------------------- devices --- */

struct WhistleDevice {
  unsigned int id;              // AudioDeviceID, valid only this launch
  char uid[WHISTLE_UID_MAX];    // stable across reboots and reconnects
  char name[WHISTLE_NAME_MAX];
  int input_channels;
  int output_channels;
  double sample_rate;
  bool is_default_input;
  bool is_default_output;
};

// Fills `out` with up to `max` devices and returns how many.  Devices come
// and go while the app runs, so this is meant to be called again whenever
// the list changes -- see whistle_set_devices_changed_callback.
int whistle_list_devices(struct WhistleDevice* out, int max);

// Called on an unspecified thread when a device is plugged in or unplugged,
// or when the system default changes.  Re-list from the main thread.
void whistle_set_devices_changed_callback(void (*callback)(void* context),
                                          void* context);

/* ------------------------------------------------------------ lifecycle --- */

struct WhistleConfig {
  // Empty string means "whatever the system default is", which is what an
  // unconfigured install should do.  A UID that is no longer present also
  // falls back to the default rather than refusing to start.
  char input_uid[WHISTLE_UID_MAX];
  char output_uid[WHISTLE_UID_MAX];

  // 0 means "leave the device alone".  Otherwise we ask the device to run at
  // this rate; if it won't, we run at whatever it is doing instead.
  double sample_rate;

  // 0 means "leave the device alone".  This is the latency knob: the device
  // clamps it to what it supports.
  int buffer_frames;
};

// Returns true on success.  On failure whistle_last_error() says why, in a
// sentence meant to be shown to the player.
bool whistle_start(const struct WhistleConfig* config);
void whistle_stop(void);
const char* whistle_last_error(void);

/* --------------------------------------------------------------- status --- */

struct WhistleStatus {
  bool running;

  // What we actually got, which is not always what was asked for.
  double sample_rate;
  int buffer_frames;
  double input_latency_ms;
  double output_latency_ms;
  double detection_lag_ms;   // the analysis window, not buffering
  char input_name[WHISTLE_NAME_MAX];
  char output_name[WHISTLE_NAME_MAX];

  // True when input and output are different devices, which costs a small
  // ring buffer between two clocks.  Worth telling the player, since picking
  // one device that does both is a real latency win.
  bool split_devices;

  int xruns;
  int dropouts;        // ring buffer over- or underruns, split mode only

  // Meters.  Peaks are since the last call to whistle_status and reset here.
  float input_peak;
  float output_peak;

  // The loudest the *detector* heard while a note was actually sounding,
  // which is a different number from input_peak: it is an RMS over the
  // analysis window rather than a sample peak, and it ignores the room
  // between notes.  It is also the number `level_full` is compared against,
  // so it is the one to read when setting it.  The command-line build prints
  // this every four seconds; here it drives a meter.
  float playing_level;

  float freq;          // last detected pitch, Hz
  bool voiced;
};

void whistle_status(struct WhistleStatus* out);

/* ------------------------------------------------------------- controls --- */

// Voice 0 is the raw input passed through; 1..whistle_preset_count() are the
// synth presets.
int whistle_preset_count(void);
const char* whistle_voice_name(int voice);

// Copies a voice's built-in parameters out, for editing and for "reset".
// Voice 0 has none and leaves `out` alone.
void whistle_preset_defaults(int voice, struct SynthParams* out);

// The voice and its parameters are published together, in one atomic step,
// because applying them separately would sound the old voice's parameters or
// the new voice's defaults for a block.  `params` is ignored for voice 0 and
// may be NULL there.
void whistle_set_program(int voice, const struct SynthParams* params);

// Both take the 0-9 steps the knobs used to.
void whistle_set_volume(int step);
void whistle_set_gate(int step);

// Drops every voice a just fifth, so that what the player whistles is the
// fifth of what they hear rather than the root: whistle a D and it plays a G.
// See synth_set_fifth.
//
// Not part of the program above, and deliberately so.  The interval has
// nothing to do with the timbre, so it belongs to the player rather than to
// the patch: it stays put across a voice change, it is not one of the fields
// the Voice tab edits, and it is not stored per voice.
void whistle_set_fifth(bool on);

// Whether a note the player *holds* outlives the breath that made it.  See
// synth_set_sustain.  Belongs to the player for the same reason the fifth
// does.  A voice may opt out (`no_sustain`), in which case this leaves it
// exactly as it was.
void whistle_set_sustain(bool on);

#endif  // WHISTLE_H
