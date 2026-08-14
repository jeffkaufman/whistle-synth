// Pitch detection: turns an input signal into hints about what the player is
// doing.  Knows nothing about synthesis.
#ifndef PITCH_H
#define PITCH_H

#include <stdbool.h>

// Analysis window and how often we re-analyze, both in samples.  The window
// bounds the lowest pitch we can find (we need a couple of periods inside it)
// and it sets the detection lag: a pitch change is visible somewhere around
// the middle of the window, so about PITCH_WINDOW/2 samples after it happens.
// At 48kHz that's ~4ms, which is the price of knowing the pitch at all.  It
// is not added to the audio path -- the synth free-runs, so nothing is
// delayed, it just learns about changes ~4ms late.
#define PITCH_WINDOW 384
#define PITCH_HOP 64

// Longest period we search for, in samples: 96 is 500Hz at 48kHz.  Raising it
// lowers the bottom of the range but costs analysis time and needs a longer
// window to stay reliable.
#define PITCH_MAX_PERIOD 96

// What the detector believes the player is doing.
//
// These are hints, not measurements.  Both "what pitch is this" and "is the
// player playing at all" are guesses, and they go wrong at exactly the moments
// that matter most -- the start and end of a note, where the signal is weak
// and not yet periodic.  The synth is expected to treat them as advice, and
// `confidence` is how much of it to take.
struct PitchHint {
  bool voiced;       // committed guess that a note is sounding
  bool onset;        // set on the single hint that begins a note
  float freq;        // Hz.  Holds its last trustworthy value while confidence
                     // is low, so it is always safe to read.
  float confidence;  // 0..1
  float level;       // RMS of the analysis window, in input units
};

struct PitchDetector {
  float sample_rate;
  int min_period, max_period;

  // A note has to stand clear of the room by this factor.  Relative rather
  // than absolute because how hot the microphone runs depends on the mic, the
  // preamp, and how close the player is -- none of which we know, and an
  // absolute threshold that suits one rig silently swallows notes on another.
  // It takes more margin to start a note than to keep one, so a note that
  // sags in the middle doesn't chatter on and off.
  float gate_margin;

  // The room's own level, tracked only while nothing is being played.  Falls
  // fast, so it settles on the quietest thing it has heard; rises slowly, so
  // a noisy stretch raises the bar but one loud moment doesn't.
  float noise_floor;

  float window[PITCH_WINDOW];
  int fill;

  float recent[3];    // last three raw estimates, for a median
  int recent_count;

  int on_hops, off_hops;
  int voiced_hops;   // analysis hops since this note started


  struct PitchHint hint;
};

// min_hz must be no lower than sample_rate/PITCH_MAX_PERIOD.
void pitch_init(struct PitchDetector* d, float sample_rate,
                float min_hz, float max_hz);

// How far above the room a note has to be to count.  1 would be the noise
// floor itself; useful values are a few times that.
void pitch_set_gate(struct PitchDetector* d, float margin);

// Feeds one input sample.  Returns the current hint, which is re-derived
// every PITCH_HOP samples and held in between.  Realtime safe.
const struct PitchHint* pitch_process(struct PitchDetector* d, float sample);

#endif  // PITCH_H
