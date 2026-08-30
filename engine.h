// Wires the detector and the synth together into the thing the audio callback
// calls.  Knows about neither PortAudio nor files.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#include "pitch.h"
#include "synth.h"

// The whistle range the detector is pointed at unless it is told otherwise:
// what an ordinary player's whistle actually covers, and what the detector
// costs the least to find.  C#5 to G7 in notes.
#define ENGINE_MIN_HZ 550
#define ENGINE_MAX_HZ 3150

// And the widest it can be *asked* for, which is what anyone has been
// recorded whistling rather than what most people can: F3 (174.6Hz) at the
// bottom and E9 (5274Hz) at the top, each with half a semitone of margin so
// that the note itself is reachable however the player's intonation lands.
//
// Being able to ask is not free at the bottom -- finding a note that low
// needs a longer analysis window, which is detection lag -- so the search
// follows what was asked for and this is only the limit of it.  See
// pitch_set_trigger_range.  The top costs nothing: shorter lags are cheaper,
// and the search reaches ENGINE_HIGHEST_HZ at all times whatever the range,
// because refusing a note by not looking for it plays its subharmonic
// instead.
#define ENGINE_LOWEST_HZ 169
#define ENGINE_HIGHEST_HZ 5450

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

// The fourth control, and the only one that isn't a 0-9 knob: 0 or 1 (or any
// non-zero) for whether every voice is dropped a just fifth, so that what you
// whistle is the fifth of what you hear rather than the root.  See
// synth_set_fifth.  Unlike the voice this is a property of the player rather
// than of the patch, so it stays put across a voice change -- including
// across passthrough, which has no synth to transpose.
void engine_set_fifth(struct Engine* e, int step);

// And a second such control (0 or 1, or anything non-zero) for whether a note
// the player holds outlives the breath that made it.  See synth_set_sustain.
// Like the fifth this belongs to the player rather than to the voice, so it
// survives a voice change and applies to all of them.
void engine_set_sustain(struct Engine* e, int step);

// The lowest and highest notes that count as notes, in Hz -- everything
// outside is heard, understood, and refused.  0 at either end means the
// widest that end goes, ENGINE_LOWEST_HZ or ENGINE_HIGHEST_HZ; engine_init
// starts at the default range above rather than at the widest, because the
// widest costs latency at the bottom and nobody has asked for it yet.
//
// A player control like the fifth and the sustain rather than part of a
// voice: it is about the instrument's range, so it survives a voice change.
// Its two uses are keeping the bottom of a whistle's own wobble from
// triggering an octave below the tune, and keeping a room -- a cymbal, a
// chair, a squeak of feedback -- from being played as a note.
void engine_set_range(struct Engine* e, float min_hz, float max_hz);

// The input level that counts as playing as hard as the player is going to,
// as a 0-9 knob; anything negative leaves every voice on the value its preset
// carries, which is where engine_init starts.
//
// A player control, and the clearest case of one in the table: every preset
// asks for the same 0.22, because it is not a property of the sound at all --
// it is how hot this microphone, this preamp and this player's whistle
// happen to run.  See synth_set_level_full.
void engine_set_level_full(struct Engine* e, int step);

// What that knob's steps mean, for a UI that has to show the number.
float engine_level_full_for_step(int step);

// Moves every voice by whole octaves, on top of the octave the voice already
// plays at.  A player control like the fifth: see synth_set_octave_shift.
void engine_set_octave(struct Engine* e, int octaves);

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
