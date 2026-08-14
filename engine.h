// Wires the detector and the synth together into the thing the audio callback
// calls.  Knows about neither PortAudio nor files.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#include "delay.h"
#include "pitch.h"
#include "synth.h"

// The whistle range the detector is pointed at.  The bottom is limited by
// PITCH_MAX_PERIOD.
#define ENGINE_MIN_HZ 550
#define ENGINE_MAX_HZ 3150

struct Engine {
  struct PitchDetector detector;
  struct Synth synth;
  struct Delay delay;

  float volume;
  bool passthrough;  // voice 0: raw input, for checking mic level and gate

  float peak_level;  // loudest input seen while a note was sounding
};

// `delay_history` must have room for DELAY_MAX_SECONDS*sample_rate floats and
// outlive the engine.  Pass NULL to run without the delay.
void engine_init(struct Engine* e, float sample_rate, float* delay_history);

// All three take the 0-9 values the control files carry.
void engine_set_voice(struct Engine* e, int voice);
void engine_set_volume(struct Engine* e, int step);
void engine_set_gate(struct Engine* e, int step);

// Names the voice numbers, for startup logging.
const char* engine_voice_name(int voice);

// Loudest input level seen while a note was sounding since the last call,
// then resets.  SynthParams.level_full is an absolute number and so assumes a
// particular mic and preamp; this is how you find out what yours actually
// does, without having to guess.
float engine_take_peak_level(struct Engine* e);

// One sample in, one sample out, on the audio thread.  Realtime safe.
void engine_process(struct Engine* e, float in_main, float in_delay,
                    float* out_main, float* out_delay);

#endif  // ENGINE_H
