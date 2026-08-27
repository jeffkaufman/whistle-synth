// Sound generation.  Reads pitch hints; never touches the input signal.
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#include "pitch.h"

// Most detuned copies of the oscillator a preset may ask for, and the most
// partials each one gets.  The low voices need the partial count: three
// octaves down, twelve partials only reaches 2kHz and sounds like a blanket.
// How many independent slow LFOs move the partials of a sustained note.
#define SYNTH_SHIMMER_LFOS 3

#define SYNTH_UNISON 3
#define SYNTH_MAX_HARMONICS 32

// How long the waveguide's delay line is.  It holds one period of the note
// being played, so this is the lowest note the voice can reach: 2048 samples
// is 23Hz at 48kHz, well under the 98Hz `flute-jet` sees at the bottom of its
// range with the fifth switched on.
#define SYNTH_BORE_MAX 2048

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

  // Opts a voice out of the sustain control entirely, so that switching it on
  // leaves this one exactly as it is.  For a voice whose whole shape is a
  // note speaking and getting out of the way, a tail is the opposite
  // instruction, and what comes out is neither: measured, `pluck`'s tails
  // land a median 8.2dB under the end of the note that made them against
  // `reese`'s 6.1, with a p10 of -22dB -- a fifth of them inaudible -- and
  // 19dB duller, because the per-note filter sweep has long since closed.
  bool no_sustain;

  // How deep the tail's movement is for this voice, overriding
  // SYNTH_TAIL_SHIMMER.  Zero means the default.  A voice whose partials are
  // an octave apart moves very differently under the same number from one
  // whose partials are a harmonic series -- there are nine of them rather
  // than thirty-two, and each is a whole octave from its neighbour.
  float shimmer_depth;

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

  // Breath noise, as a fraction of the tone.  Shaped by a band fixed in Hz --
  // see the breath block in synth_process for the measurements behind it.
  float breath;

  // Where that band sits: the bottom corner, in Hz, with the top derived from
  // it so the shape stays the one that was fitted to the recordings.  It is a
  // property of the instrument rather than of the note -- the noise is made at
  // the embouchure and in the player's mouth, and neither changes size with
  // the pitch, which is why the band does not track f0.  But a *bigger* flute
  // has a bigger embouchure hole, so a preset an octave down wants this lower.
  // 0 means the concert flute's measured 1600Hz.
  float breath_hz;

  // Partial amplitudes read straight out of a recording, in dB under the
  // fundamental, instead of computed from pwm/tilt/cutoff.  In use when
  // register_hi_hz is above zero, and then it replaces that whole formula for
  // this preset -- the pulse series can only make smooth monotone curves, and
  // a real instrument's is neither.  `flute`'s seventh partial sits *above*
  // its sixth, which no setting of width, tilt and cutoff produces.
  //
  // Two tables rather than one because the harmonics are not a fixed set of
  // ratios: a flute gets dramatically purer as it goes up, its second partial
  // measuring -11.3dB under the fundamental low in the range and -24.1dB high
  // in it.  The played pitch blends between them, linearly in log frequency,
  // from register_lo_hz to register_hi_hz -- which is the register break, and
  // is why this is indexed by the note rather than by absolute frequency.  A
  // body resonance fixed in Hz was the other candidate and the recordings
  // rule it out: fitted against nine notes it comes out flat to within
  // 0.4dB and explains nothing (4.53dB of residual against 4.57 without it),
  // while indexing by register takes the audible partials from 5.1 to 4.0.
  float partial_lo[SYNTH_MAX_HARMONICS];
  float partial_hi[SYNTH_MAX_HARMONICS];
  float register_lo_hz, register_hi_hz;

  // ------------------------------------------------- the waveguide ---
  //
  // A jet-drive physical model, and the one preset here that has no spectrum
  // in it at all.  Above zero, `jet_drive` replaces the oscillator and
  // everything that shapes it: there are no partial amplitudes to set,
  // because what comes out is whatever a tube one wavelength long does when
  // it is blown this hard.  See the waveguide block in synth_process.
  //
  // This is the loop's small-signal gain at full breath.  1.0 is exactly the
  // threshold of oscillation -- the flute is silent -- and everything above
  // it is how hard the player is blowing.  It is the only dynamics control
  // the voice has, and deliberately: on a real flute how loud a note is, how
  // rich it is and how fast it speaks are not three settings, they are one
  // breath, and here they come off this one number the same way.
  float jet_drive;

  // How far the jet sits off the labium.  Centred, the jet is an odd function
  // of the acoustic field and can only make odd harmonics, which is a
  // clarinet; this is what puts the even ones in, and it is most of what
  // sets the second partial.
  float jet_bias;

  // Where the bore's losses and its low cutoff sit, as multiples of the note
  // rather than fixed in Hz.  That is a property of the tube and not a
  // convenience: a low note is played on a longer tube, so the same loss per
  // metre compounds over more of it, and the loop comes out the same shape at
  // every pitch.
  //
  // It is also what makes the voice play in tune.  Fixed in Hz, a low note's
  // partials run round a loop that barely damps them, come back with the
  // wrong phase -- a one-pole's delay is not the same at every frequency --
  // and the jet mixes them back down onto the fundamental and drags it flat.
  // Measured that way the bottom of the range played 30-65 cents under the
  // top's 10.  Tracking, the whole range and the whole of the dynamics land
  // between +4.3 and +6.5 cents.
  float jet_damp, jet_hp;

  // Turbulence at the embouchure, as a fraction of the acoustic field.  A
  // waveguide is silent until something disturbs it, so this is the thing the
  // note grows out of and it is part of how fast the voice speaks.
  float jet_noise;

  // How many dB the partials above the fundamental drop as the player goes
  // from soft to hard, on top of the tables above, which are the spectrum at
  // mid dynamics.  Positive means the tone gets *purer* when pushed, which is
  // backwards from a filter opening up and is what a flute measurably does.
  float purity_loud;
};

struct Synth {
  float sample_rate;
  const struct SynthParams* params;

  // What the preset's `octave` is multiplied by before anything is
  // synthesized: 1 normally, and 2/3 when the fifth is switched on.  It lives
  // on the synth rather than in the params because it is a player control
  // that applies across the whole table, and the presets are const.
  float octave_mul;

  // Whether the sustain control is on, which is what turns a note the player
  // held into one that outlives the breath that made it.  Like the fifth this
  // is a property of how the player is using the instrument rather than of
  // the voice, so it survives a voice change and applies to all of them.  See
  // synth_set_sustain.
  bool sustain;

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
  // Whether a tail is holding the gate open now that the player has stopped,
  // when the sustain control is on.  The gate does not move at all while this
  // is set, and nothing times it out: the tail is ended by the next note
  // rather than by a clock.  See the gate block in synth_process.
  bool holding_tail;
  float level;         // smoothed hint level
  // A holding preset's running level-weighted pitch, as a leaky numerator and
  // denominator whose quotient is the average.  This is the pitch such a
  // preset holds *at*, rather than the one it happened to be playing when the
  // note ended.  See synth_process.
  float pitch_num, pitch_den;
  // And the same average of the note's *level*, over the same weights, which
  // is the level its tail holds at.  A trail-off is quiet, so it counts for
  // almost nothing here, which is what lets the level itself follow one all
  // the way down without the tail inheriting where it ended up.
  float level_num;
  // How far into the sustain it is: 0 while the note is being played and 1
  // once it has fully arrived, moving both the pitch and the level.  The
  // pitch it set out from is kept because the slide is a ramp between two
  // points rather than a filter towards one -- a fixed 250ms whatever the
  // interval, which is what makes it read as a slide.
  float settle, settle_from_log;
  // The level the slide sets out from, kept for the same reason
  // settle_from_log is: the move onto the sustain is a ramp between two
  // points rather than a filter towards one, and both ends of it are fixed
  // when the player stops.
  float settle_from_level;
  // How long the player has stayed on this note.  A tail is something a note
  // earns by being held rather than something every note gets: see
  // SYNTH_HOLD_MIN_NOTE_S.
  float note_playing;
  float loudness;      // level mapped to 0..1, drives the envelope
  float dynamics;      // level mapped to 0..1, drives brightness and drive
  float note_age;      // seconds since the last onset

  float pwm_pos, growl_pos, wobble_pos, vibrato_pos;
  // Three free-running phases for the movement in a tail; see
  // SYNTH_TAIL_SHIMMER.  Separate accumulators rather than multiples of one,
  // so each can wrap without stepping the others.
  float shimmer_pos[SYNTH_SHIMMER_LFOS];
  // Their current values, computed once per control hop and shared by every
  // partial in a group rather than recomputed per partial.
  float shimmer_gain[SYNTH_SHIMMER_LFOS];
  // How far the LFOs have come up to speed, 0 to 1: see
  // SYNTH_TAIL_SHIMMER_S.
  float shimmer_rate;
  // How much louder this spectrum has to be played to be heard as loudly as
  // one sitting where the cabinet and the ear are at their best.  See
  // synth_audibility.
  float audibility_comp;

  // The plucked part of the note envelope, falling from 1 to sustain_level.
  float pluck;

  // FM modulator phases, and the index smoothed the way drive is.
  float fm_phase[SYNTH_UNISON];
  float fm_index;
  float fm_index_smoothed;

  // Per-copy gain for the partials below mono_partials: sqrt(unison) on the
  // first copy and zero on the rest.
  float low_gain[SYNTH_UNISON];

  // Per-note envelopes that decay towards zero, in octaves.
  float drop;
  float cutoff_env;

  // Breath noise: the generator, the two integrators of the state variable
  // filter that bands it, and two more used as lowpasses to steepen its top
  // edge.
  uint32_t noise_state;
  float breath_lp, breath_bp;
  float breath_post[2][2];

  // Two one-pole high-pass stages, for the low voices only.
  float hp_x[2], hp_y[2];

  // The waveguide's tube, as a delay line, and the four filter stages that
  // close the loop round it: two one-pole low-passes for the bore's losses
  // and two high-pass stages for its low cutoff.
  float bore[SYNTH_BORE_MAX];
  int bore_write;
  float bore_lp[2];
  float bore_hp_x[2], bore_hp_y[2];
  // How long the tube is, in samples, smoothed on the way in: it is a pitch,
  // so it has to move the way a pitch does rather than stepping at the
  // control rate.
  float bore_len, bore_len_target;
  // The loop's coefficients, and the gains each stage is divided back out by
  // so that the round trip is lossless at the note being played.  Recomputed
  // at the control rate, which is what makes them cheap enough to derive
  // exactly rather than approximate.
  float bore_lp_a, bore_hp_r, bore_lp_norm, bore_hp_norm;
  // tanh at the jet's offset, and the reciprocal of its slope there.  Both
  // depend only on the preset, so they are worked out when it is set.
  float bore_t0, bore_slope_inv;

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

// Plays a just fifth below what the preset would otherwise play -- frequency
// times exactly 2/3 -- or puts it back.  Whatever you whistle is then the
// fifth of what you hear rather than the root, so a tune whistled in D comes
// out in G.
//
// This used to be a preset of its own (`fm-fifth`, at octave 1/24 against
// `fm`'s 1/16), but the interval has nothing to do with the timbre and there
// is no reason only one voice should have it.  Survives a voice change, since
// which voice you are playing and what interval you are playing it at are
// separate decisions.
//
// A just fifth rather than a tempered one: dividing by exactly three is the
// harmonic relationship, and it lands about 2 cents flat of equal temperament,
// which down here is one beat every twenty seconds against a fretted
// instrument -- well inside how accurately anyone whistles.
void synth_set_fifth(struct Synth* s, bool on);

// Whether a note the player holds outlives the breath that made it.
//
// Off, every voice sounds for exactly as long as you whistle, which is an
// organ: the bass line stops when you take a breath.  On, a note held for
// SYNTH_HOLD_MIN_NOTE_S slides onto the nearest real note, settles under
// itself, and stays there -- so one note every couple of bars holds a drone
// under a tune, and the line carries through the breath and under the next
// phrase.  It is still monophonic, so a new note takes the tail with it: what
// you get is a line that never stops, not two notes at once.
//
// The tail does not time out, and that is what makes it playable rather than
// something to keep topping up.  It ends when the next note supersedes it,
// and since a note under SYNTH_HOLD_MIN_NOTE_S earns no tail of its own, a
// single short note is how you stop the drone: it takes the tail, and then it
// stops the way any note stops.
//
// Notes too short to have been meant that way are untouched, which is what
// lets this be a switch rather than a voice: with it on, a fast phrase sounds
// exactly as it does with it off, and the tail only appears where a note was
// deliberately held.  See the SYNTH_HOLD_* block in synth.c.
void synth_set_sustain(struct Synth* s, bool on);

// Plays `params` instead of a preset.  The struct is borrowed, not copied, so
// it must outlive the synth -- and since the audio thread reads it every
// block, a caller changing one while it plays needs to publish a *different*
// struct rather than editing this one underneath it.
void synth_set_params(struct Synth* s, const struct SynthParams* params);

// Produces one output sample from the current hint, and leaves the matching
// stereo difference in `s->side`.  Realtime safe.
float synth_process(struct Synth* s, const struct PitchHint* hint);

#endif  // SYNTH_H
