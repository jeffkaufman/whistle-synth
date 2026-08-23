// Sound generation.  Reads pitch hints; never touches the input signal.
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

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

  // ------------------------------------------------------- prototypes ---
  //
  // Everything below is here for the bass voices being auditioned.  All of
  // it is off at zero, so the presets above are unaffected.

  // Octave stack.  When >0 the partials are not a harmonic series at all:
  // partial n sits at 2^(n-1) times the fundamental, and its amplitude comes
  // from a bell curve fixed in *absolute* Hz -- centred at octave_stack_hz,
  // this many octaves wide.  Playing an octave higher slides every component
  // one slot along a weighting that hasn't moved, so the spectrum repeats
  // exactly and the voice has no octave: it is a Shepard tone you can play.
  //
  // Replaces pwm/tilt/cutoff/resonance entirely for a preset that asks for
  // it, since a fixed-in-Hz envelope is the only shape that survives the
  // wrap.  Fixed-in-Hz processing after it is fine, which is why the drive
  // and the high-pass still apply.
  float octave_stack_hz;
  float octave_stack_width;    // gaussian sigma, in octaves

  // How far the bell follows the pitch, in octaves per octave.  0 is the pure
  // Shepard above -- the bell never moves and the voice has no register.  1
  // would track exactly and be an ordinary voice built out of octaves.
  //
  // In between is the useful part: at 0.5 an octave of whistle moves the bass
  // a fifth, so a 2.5 octave whistle range maps into 1.25 octaves of bass and
  // the line keeps real melodic contour while never leaving the window
  // between what a PA can reproduce and where the mandolin starts.  The
  // timbre still repeats exactly, just every 1/(1-track) octaves instead of
  // every one.
  //
  // `ref_hz` is the *whistled* pitch at which the bell sits exactly where
  // octave_stack_hz puts it, so it reads in the range the player thinks in.
  float octave_stack_track;
  float octave_stack_ref_hz;

  // A resonant peak on top of the rolloff, centred at the cutoff: height is
  // the linear boost there, width its sigma in octaves.  This is the
  // difference between a filter and a tone control -- without it a cutoff
  // sweep only gets brighter and darker, and with it the sweep is audible as
  // a filter moving, which is what gives `pluck` its attack.
  float resonance;
  float resonance_width;

  // Free-running LFO on the cutoff, in octaves.  The wobble.
  float wobble_hz;
  float wobble_octaves;

  // Per-note cutoff envelope: each note starts this many octaves above where
  // the dynamics put the cutoff and falls back with this time constant.
  float cutoff_env_octaves;
  float cutoff_env_s;

  // Per-note pitch envelope: each note starts this far sharp and falls to
  // pitch with this time constant.  An 808's thump and a hoover's swoop are
  // the same mechanism at two settings.
  float drop_octaves;
  float drop_s;

  // Vibrato, fading in over a held note the way the growl does.
  float vibrato_hz;
  float vibrato_cents;
  float vibrato_onset_s;

  // How many of the lowest partials come from one oscillator instead of from
  // all the detuned copies.  0 means every copy plays every partial.
  //
  // This is a mono fix and it matters here because mono is what this gets
  // played through.  Detuned copies beat, and beating is cancellation: three
  // copies nine cents apart swing the fundamental over 31dB as they drift in
  // and out of phase, which spread across a stereo image is the sound of a
  // Reese and summed to one speaker is the bass falling out of the tune.
  // Taking the bottom partial or two from a single copy keeps the low end
  // rock steady and leaves the churn where it belongs, in the harmonics.
  //
  // The copy that plays them is scaled by sqrt(unison), because the partials
  // above it get their level from `unison` copies summing in power.
  int mono_partials;

  // Note envelope beyond on-and-off: how fast a note falls from its attack to
  // a held level, and what that level is as a fraction of the peak.  0
  // disables, which is what every voice did before -- they sound for exactly
  // as long as you whistle, which is an organ.  A dance bass wants to speak
  // hard and get out of the way.
  float decay_s;
  float sustain_level;

  // Two-operator FM, which replaces the additive pulse entirely: the carrier
  // is at the fundamental, the modulator at `fm_ratio` times it, and the
  // index is how many radians of phase the modulator pushes the carrier
  // through.  Index takes the place of cutoff here -- opening up an FM voice
  // moves harmonic energy around by Bessel functions rather than by
  // uncovering partials that were already there, and it sounds different in a
  // way no filter setting reaches.
  float fm_ratio;
  float fm_index_soft;
  float fm_index_loud;

  // Offset into the saturator, in units of the signal.  atan is an odd
  // function, so it can only make odd harmonics however hard it is driven;
  // pushing the signal off centre first breaks that symmetry and puts even
  // harmonics in, which is what a valve amp does and what the octave between
  // a bass and a mandolin's low G is otherwise missing.  The saturator's
  // value at the offset is subtracted back out, so this adds no DC.
  float drive_bias;

  // ------------------------------------------------------------- pad ---
  //
  // A latching drone rather than a note: a steady whistle arms it, it holds
  // on its own, and it fades without being held.  A non-zero pad_arm_s
  // switches the whole voice into this mode, and most of the parameters
  // above stop applying -- there is no gate, no articulation, no growl, and
  // the note envelope is replaced entirely.
  //
  // How long the pitch has to hold still to count as a chord change, and how
  // far it may wander while doing so.
  float pad_arm_s;
  float pad_steady_cents;

  // The envelope, in the terms it was asked for: ramp up over attack_s, hold
  // for hold_s, then halve every halflife_s.  Re-whistling the note the pad
  // is already on refreshes the hold rather than starting a second chord.
  float pad_attack_s;
  float pad_hold_s;
  float pad_halflife_s;

  // How fast the previous chord gets out of the way when a new one arrives.
  float pad_duck_s;

  // Which partials of the fundamental sound, as a bitmask -- bit n-1 for
  // partial n.  0 means all of them, which is what every non-pad voice does.
  //
  // This is how the chord is built and why it has no third.  Partials 2, 3,
  // 4, 6, 8, 12, 16, 24 and 32 are the root, its fifth, and octaves of both,
  // and nothing else: partial 5 and partial 10 are the major third and are
  // left out, so the pad never says whether the tune is major or minor.
  // Building the chord out of one harmonic series rather than out of
  // separate oscillators is also what makes it fuse into a single rich tone
  // instead of reading as a stack of pitches -- it is how an organ mixture
  // works, for the same reason.
  uint32_t partial_mask;

  // The spectral envelope, fixed in absolute Hz: a body bell low down and a
  // quieter one up top, with the gap between them left empty on purpose.
  // Fixed in Hz rather than in partial number because a pad has to stay in
  // its lane in the arrangement whatever it is playing -- the body under the
  // mandolin, the shimmer above it, and 300-800Hz left clear for the tune.
  float bell_hz, bell_width;
  float shimmer_hz, shimmer_width, shimmer_level;

  // ---- Movement.  All of it shallow on purpose: the point is that the
  // sound never sits still, not that any one of these is audible as an
  // effect in its own right. ----

  // Per-partial pitch wander, in cents.  This is the ensemble in the pad --
  // what a stack of detuned oscillators would normally provide, done in a way
  // that survives mono.  Detuned *copies* cancel, because two things at
  // almost the same frequency beat; a single partial nudged a few cents has
  // nothing to beat against, so it thickens without a null anywhere.
  float drift_cents;

  // A slow sweep of the shimmer bell, and how far the same bell is pulled
  // down while the chord is still swelling.  Together they are the filter
  // opening and closing that a pad is expected to do -- one on a clock, one
  // on the note.
  float sweep_hz, sweep_octaves;
  float bright_env_octaves;

  // A rotating speaker.  Amplitude and pitch modulated in quadrature, which
  // is what makes it read as something moving rather than as tremolo plus
  // vibrato; the partials below leslie_split_hz get the bass rotor, which
  // turns slower and much shallower than the horn above it.
  float leslie_hz, leslie_am, leslie_cents, leslie_split_hz;

  // Slow independent level drift, per partial, at rates spread around
  // drift_hz so no two partials ever come back into step.
  //
  // This is where a pad's richness has to come from here.  The usual source
  // is a stack of detuned copies, and detuned copies cancel in mono -- see
  // mono_partials, where that cost `reese` 31dB of its fundamental.  A pad
  // is all sustain, so the cancellation would have time to sweep every
  // partial in turn.  Drifting the levels of partials that stay exactly in
  // tune gives movement with nothing to cancel.
  float drift_hz, drift_depth;

  // Concert pitch, as the frequency of A.  Non-zero snaps the chord's root to
  // the nearest equal-tempered semitone; 0 leaves it wherever it was
  // whistled.
  //
  // Only the pad does this, and the asymmetry is the point.  A bass line
  // wants every cent of what was played -- the scoops, the slides, and the
  // fact that a whistle is a fretless instrument are the expression.  A chord
  // has no such freedom: it is either in tune with the mandolin or it is
  // wrong, and 30 cents flat under a fretted instrument is a beat rather than
  // a colour.
  //
  // It is a frequency and not a flag so a band that tunes to 442 can say so.
  float pad_snap_hz;

  // The bottom of the one octave the chord's root is allowed to live in.
  // Whatever is whistled, the root is folded into [pad_fold_hz, 2x) -- so the
  // pad is always in the same register and you never have to whistle in a
  // particular octave to place it.
  //
  // Folding rather than compressing, and that distinction is the whole
  // reason this parameter is a frequency and not a rate.  Compressing the
  // register -- moving the pad half an octave per octave of whistle, the way
  // `octaveless-half` moves -- halves every interval too: whistling D then G
  // then A asks for +5 and +7 semitones and gets +2.5 and +3.5, which are
  // quarter-tones, out of tune with the band and unable to state I-IV-V at
  // all.  That is tolerable in a bass line, where a compressed contour still
  // reads as a line.  It is not tolerable in a chord.  Folding keeps the
  // pitch class exact and moves only the octave.
  float pad_fold_hz;

  // Breath noise, as a fraction of the tone.  Banded around the note rather
  // than white, and louder when the player backs off, which is how a large
  // flute actually behaves -- on a contrabass flute the breath is nearly as
  // loud as the note.
  float breath;
};

// Most partials a pad chord can use, and how many chords can be sounding at
// once.
//
// Four layers, and the number falls straight out of the timings: a chord can
// be armed every pad_arm_s, and a ducking one is inaudible after about four
// pad_duck_s.  At 0.10 and 0.12 that is 0.48s of fading against a new chord
// every 0.10s, so five could overlap in the worst case -- but the ones that
// matter are the recent ones, and by the fourth the first is 29dB down.
// Three would put it at 22dB, which is audible when it gets cut.
#define PAD_PARTIALS 16
#define PAD_LAYERS 4

// One sounding chord.  Each partial carries its own phase rather than coming
// off the Chebyshev recurrence, because they no longer sit at exact multiples
// of anything: drift and leslie move each one independently, which is where
// the pad's movement comes from.
struct PadLayer {
  float log_root;    // log2 Hz of this chord's fundamental
  float env;         // ramp / hold / decay
  float hold_left;   // seconds of hold remaining; <=0 means decaying
  float duck;        // 1, until a newer chord supersedes this one
  float gain;        // how hard the whistle that armed it was
  float level;       // env * duck * gain, smoothed

  int n_partials;
  int partial[PAD_PARTIALS];        // which harmonics of log_root these are
  float phase[PAD_PARTIALS];
  float pitch_mul[PAD_PARTIALS];    // drift and leslie, stepped at control rate
  float leslie_am[PAD_PARTIALS];    // applied after normalization, see below
  float amp[PAD_PARTIALS];          // smoothed
  float amp_target[PAD_PARTIALS];
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

  float pwm_pos, growl_pos, wobble_pos, vibrato_pos;

  // The plucked part of the note envelope, falling from 1 to sustain_level.
  float pluck;

  // FM modulator phases, and the index smoothed the way drive is.
  float fm_phase[SYNTH_UNISON];
  float fm_index;
  float fm_index_smoothed;

  // Per-copy gain for the partials below mono_partials: sqrt(unison) on the
  // first copy and zero on the rest.
  float low_gain[SYNTH_UNISON];

  // Pad mode.  `pad_active` is the layer accepting the current chord; the
  // other one is either silent or ducking.
  struct PadLayer pad[PAD_LAYERS];
  int pad_active;
  float pad_semitone;    // the snapped note currently held, semitones from A
  float pad_steady_s;    // how long the pitch has held still
  float pad_steady_log;  // the pitch it has been holding at
  float pad_gap_s;       // how long the detector has been unvoiced
  float drift_phase[PAD_PARTIALS];
  float leslie_phase;
  float sweep_phase;

  // Per-note envelopes that decay towards zero, in octaves.
  float drop;
  float cutoff_env;

  // Breath noise: the generator, and the two integrators of the state
  // variable filter that bands it.
  uint32_t noise_state;
  float breath_lp, breath_bp;

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
