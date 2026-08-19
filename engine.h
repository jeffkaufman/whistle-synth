// Wires the detector and the synth together into the thing the audio callback
// calls.  Knows about neither PortAudio nor files.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#include "pitch.h"
#include "synth.h"

// The whistle range the detector is pointed at.  The bottom is limited by
// PITCH_MAX_PERIOD.
#define ENGINE_MIN_HZ 550
#define ENGINE_MAX_HZ 3150

struct Engine {
  struct PitchDetector detector;
  struct Synth synth;

  float volume;
  bool passthrough;  // voice 0: raw input, for checking mic level and gate

  float peak_level;  // loudest input seen while a note was sounding
};

void engine_init(struct Engine* e, float sample_rate);

// All three take the 0-9 values the controls carry.
void engine_set_voice(struct Engine* e, int voice);
void engine_set_volume(struct Engine* e, int step);
void engine_set_gate(struct Engine* e, int step);

// Plays `params` rather than the current voice's built-in preset, for voices
// the player has edited.  Borrowed, not copied -- see synth_set_params.  Has
// no effect while voice 0 (passthrough) is selected, and is undone by the
// next engine_set_voice.
void engine_set_params(struct Engine* e, const struct SynthParams* params);

// Names the voice numbers, for logging and for building a voice menu.
const char* engine_voice_name(int voice);

// Loudest input level seen while a note was sounding since the last call,
// then resets.  SynthParams.level_full is an absolute number and so assumes a
// particular mic and preamp; this is how you find out what yours actually
// does, without having to guess.
float engine_take_peak_level(struct Engine* e);

// One sample in, one sample out, on the audio thread.  Realtime safe.
float engine_process(struct Engine* e, float in);

// The same, in stereo.  The two channels are the mono signal plus and minus
// the synth's unison spread, so `(*left + *right) / 2` is what
// engine_process would have returned and nothing cancels on fold-down.
// Voices that run a single oscillator have no spread and come out centred.
void engine_process_stereo(struct Engine* e, float in,
                           float* left, float* right);

#endif  // ENGINE_H
