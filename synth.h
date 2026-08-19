// Sound generation.  Reads pitch hints; never touches the input signal.
#ifndef SYNTH_H
#define SYNTH_H

#include "pitch.h"

// Most detuned copies of the oscillator a preset may ask for, and the most
// partials each one gets.  The low voices need the partial count: three
// octaves down, twelve partials only reaches 2kHz and sounds like a blanket.
#define SYNTH_UNISON 3
#define SYNTH_MAX_HARMONICS 32

// How often the slow-moving controls (timbre, drive) are recomputed, in
// samples.  Everything derived here is smoothed on the way out, so this only
// has to be fast compared to how quickly a player changes anything.
#ifndef SYNTH_CONTROL_HOP
#define SYNTH_CONTROL_HOP 32
#endif

// A preset.  Everything that distinguishes one lead from another lives here,
// so a new one is a table entry rather than a new code path.
struct SynthParams {
  const char* name;

  // The whistle is resynthesized this far from where it was played: 0.5 puts
  // it an octave down, which lands a 588-3150Hz whistle at 294-1575Hz, where
  // a fiddle plays the tune.
  float octave;

  // Pulse width, and the slow sweep of it that keeps a held note moving.
  float pwm_center;
  float pwm_slow_hz;
  float pwm_slow_depth;

  // A faster width wobble that fades in over a held note.  Driven by note
  // length rather than by level, so runs stay clean and long notes grow a
  // growl.
  float growl_hz;
  float growl_depth;
  float growl_onset_s;

  // Harmonic rolloff, as the partial number where it is 3dB down: the first
  // when the player backs off, the second when they lean in.
  float cutoff_soft;
  float cutoff_loud;

  // How steeply it rolls off past the cutoff -- 2 behaves like a 2-pole
  // filter, 4 like a 4-pole.  It exists for `subbass`, where a shallow
  // rolloff can't be made dark enough: the second partial only sits above the
  // first if the low end is shaped by a narrow pulse rather than by the
  // rolloff, and any 2-pole slope steep enough to darken the top also drags
  // the second partial back under the first.  A steeper slope starting above
  // them settles it.
  float rolloff_exp;

  // Saturation, same two ends.
  float drive_soft;
  float drive_loud;

  // How many detuned copies to run, and how far apart.  One copy means no
  // detune at all, which is what a bass wants -- and which the spread below
  // cannot express, since three voices an equal distance apart in phase sum
  // to nothing at the fundamental.
  int unison;
  float detune_cents;
  int harmonics;

  // How far the unison copies are spread across the stereo field, 0 to 1.
  //
  // This is real stereo rather than a widening effect: the copies are
  // genuinely different signals, detuned against each other, so panning them
  // apart puts decorrelated material on the two sides.  A single-copy voice
  // has nothing to spread and stays centred whatever this says, which is what
  // a bass wants anyway -- a wandering low end is the one thing that will not
  // survive being played through a PA.
  //
  // Carried as a side signal, so the two channels are mid +/- side and fold
  // down to exactly the mono output.  See synth_process.
  float stereo_width;

  // Exponent on the 1/n of the pulse series.  1.0 is a true pulse wave and
  // rolls off at 6dB an octave; lower flattens the spectrum.  Brass gets much
  // of its character from an envelope far flatter than a pulse wave's, with
  // the energy sitting well above the fundamental rather than at it.
  float tilt;

  // Pushes partial n from n to n*(1 + stretch*(n-1)), so the upper partials
  // go progressively sharp instead of landing on exact multiples.
  //
  // This is what the old `ebass` voice's growl was made of: its partials sat
  // at 1, 2, 3.11, 4.3, 5.7 and 6.1 rather than at 1..6.  On its own that is
  // a static clang, but the drive then folds the partials together and the
  // difference tones land a few Hz apart from the harmonics -- and a few Hz
  // of beating is exactly what a growl is.
  //
  // Costs a phase accumulator and a sinf per partial instead of the two
  // multiplies of the recurrence, so it is only paid by presets that ask.
  float stretch;

  // The bottom of the useful range, the mirror of stopping before Nyquist at
  // the top.  Five octaves below a whistle can land under 20Hz, which nothing
  // in the room can reproduce: it is headroom spent on shifting woofers
  // rather than on sound.
  //
  // Two things enforce it.  Partials below it are never synthesized, so the
  // drive doesn't spend its range on them -- but that isn't enough on its
  // own, because saturating partials at 37 and 56Hz regenerates their 19Hz
  // difference.  So the output also gets a 12dB-an-octave high-pass, set a
  // little below this so it clears out what the drive invented without
  // thinning the lowest partial we chose to keep.  0 disables both.
  float min_partial_hz;

  // Input RMS that counts as playing as hard as you're going to.  Absolute,
  // so it assumes a calibrated mic level, but soft-kneed so being off by 2x
  // costs expression rather than usability.
  float level_full;

  // Note shaping: how the sound starts and stops.
  float attack_s;
  float release_s;

  // How fast the level may move *within* a note, in both directions.  This
  // has to be much quicker than release_s, and it is the difference between
  // a run of tongued notes and one long smear: a glottal stop is a ~25ms dip
  // that never silences the whistle, so if the note envelope's release is
  // what governs it, a 70ms release passes barely a fifth of it through.
  float articulation_s;

  float glide_s;

  float out_gain;
};

struct Synth {
  float sample_rate;
  const struct SynthParams* params;

  float phase[SYNTH_UNISON];
  float detune[SYNTH_UNISON];
  float pan[SYNTH_UNISON];   // -1 left to +1 right, always summing to zero
  int unison;

  // Only used when the preset stretches its partials off the harmonic series.
  float partial_phase[SYNTH_UNISON][SYNTH_MAX_HARMONICS];

  float log_freq;      // log2 Hz, smoothed towards the hint
  // The output envelope is the product of two: `gate` is the note starting
  // and stopping, and is the only part that wants a slow release; `loudness_env`
  // is how hard the player is blowing right now, and has to be quick.
  float gate;
  float loudness_env;
  float amp;           // gate * loudness_env
  float level;         // smoothed hint level
  float loudness;      // level mapped to 0..1, drives the envelope
  float dynamics;      // level mapped to 0..1, drives brightness and drive
  float note_age;      // seconds since the last onset

  float pwm_pos, growl_pos;

  // Two one-pole high-pass stages, for the low voices only.
  float hp_x[2], hp_y[2];

  float harmonic_amp[SYNTH_MAX_HARMONICS];  // smoothed
  float harmonic_target[SYNTH_MAX_HARMONICS];
  int harmonics_active;
  float drive;        // target, stepped at the control rate
  float drive_smoothed;  // what actually multiplies the signal

  int control_countdown;

  // The stereo difference for the sample synth_process just returned, already
  // enveloped and scaled by stereo_width.  Left is mid+side and right is
  // mid-side, so a caller that wants mono can ignore this entirely and a
  // caller that adds the two channels back together gets the mid untouched.
  float side;
};

int synth_preset_count(void);
const char* synth_preset_name(int preset);

// Copies a preset's built-in values out, so a caller can edit them and hand
// them back through synth_set_params.  The presets themselves stay const:
// they are the thing you get back when you reset a voice.
void synth_preset_defaults(int preset, struct SynthParams* out);

// Forces values that would otherwise divide by zero or run off the end of a
// fixed array into range.  synth_set_params borrows a const struct and so
// cannot do this itself: whoever fills one in calls this before publishing it.
void synth_sanitize_params(struct SynthParams* p);

void synth_init(struct Synth* s, float sample_rate, int preset);
void synth_set_preset(struct Synth* s, int preset);

// Plays `params` instead of a preset.  The struct is borrowed, not copied, so
// it must outlive the synth -- and since the audio thread reads it every
// block, a caller changing one while it plays needs to publish a *different*
// struct rather than editing this one underneath it.
void synth_set_params(struct Synth* s, const struct SynthParams* params);

// Produces one output sample from the current hint, and leaves the matching
// stereo difference in `s->side`.  Realtime safe.
float synth_process(struct Synth* s, const struct PitchHint* hint);

#endif  // SYNTH_H
