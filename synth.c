#include "synth.h"

#include <math.h>
#include <string.h>

// The tone is a pulse wave, built additively.  Partial n of a width-w pulse
// has amplitude sin(n*pi*w)/n, and sweeping w is the classic PWM lead
// movement: at w=0.5 the even partials null out and it goes hollow and
// square, and as w narrows they fill back in and it turns nasal and bright.
//
// Additive rather than a shaped ramp because we can then simply stop before
// Nyquist and never alias, and because it makes the harmonic rolloff -- the
// thing that has to track how hard the player is blowing -- a direct
// multiply instead of a filter to tune.
//
// `out_gain` is not a taste control.  Each one is set so that the presets
// measure the same loudness -- equal LUFS, ITU-R BS.1770, which is the
// standard model for perceived loudness and the reason one bass voice has to
// be numerically much hotter than another to sound as loud.  Changing one
// means re-running the match, not just adjusting that voice.  The match is
// run over recordings/whistling.f32, a recording of the real instrument,
// because it depends on the material: matched on synthetic test tones the
// numbers come out several dB different.
//
// It is run through a model of the speaker rather than on the signal itself:
// a QSC K10.2, flat to about 55Hz and then a ported cabinet's cliff.  Every
// voice here is a bass voice and several of them put real energy under that
// corner, so matching them full-range matches a system nobody is listening
// on.  All of them land on -21.0 LUFS through that filter, and on a
// full-range system they therefore do *not* measure equal.
//
// The one exception is `pluck`, which says so where it sits: a plucked
// envelope has a high crest factor by design, an integrated meter always
// reads that low, and it is set by peak instead.
//
// The ceiling on the whole table is `subbass`, which reaches 0.82 peak at
// this target.  Nothing can go louder without it clipping.

static const struct SynthParams presets[] = {
  {
    // Four octaves down: a comfortable whistle lands around 50-160Hz, which
    // is where an electric bass actually plays.  (Five octaves, which an
    // earlier version of this program used, is below the low E.)
    .name = "bass",
    .tilt = 1.0f,
    .octave = 0.0625f,
    // No movement to speak of.  A wobbling bass fights the piano's left hand
    // and turns to mud in a PA.
    .pwm_center = 0.30f, .pwm_slow_hz = 0.11f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    // Keep the fundamental dominant when played softly and open up when dug
    // into, like a filter tracking the pluck.
    .cutoff_soft = 1.5f, .cutoff_loud = 7.0f, .rolloff_exp = 2.0f,
    .drive_soft = 1.0f, .drive_loud = 3.0f,
    // Strictly one voice.  Detuned bass beats against itself and smears the
    // low end, and it is the note starting exactly on time that makes a bass
    // line feel tight.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.040f, .articulation_s = 0.008f, .glide_s = 0.003f,
    .out_gain = 0.604f,
  },
  {
    // Five octaves down, which is where the old `ebass` voice lived and what
    // this is here to bring back.  A comfortable whistle lands around
    // 25-80Hz: below the bottom of an electric bass, felt as much as heard.
    .name = "subbass",
    // No 1/n rolloff at all, which sounds like the wrong direction for a
    // dark voice but isn't: the cutoff below does the darkening, and it has
    // to be set low enough that a pulse's own rolloff on top would drop the
    // second partial under the first and lose the character.  The two work
    // against each other and this is where they balance.
    .tilt = 0.20f,
    // Matches the old voice closely: partial 3 lands at 3.12 against its 3.11,
    // partial 4 at 4.24 against 4.3, partial 5 at 5.40 against 5.7.
    .stretch = 0.014f,
    .octave = 0.03125f,
    // A narrow pulse puts the second partial above the first, which is what
    // the old voice did (0.24 against 0.20) and is why it read as pitched
    // rather than as rumble: the octave up carries the note, the fundamental
    // carries the weight.
    .pwm_center = 0.21f, .pwm_slow_hz = 0.09f, .pwm_slow_depth = 0.045f,
    // Both the stretch and this wobble are deliberately restrained.  Pushed
    // any harder they stop reading as a growl and start reading as
    // distortion, which is what a sub-bass least wants: down here the ear
    // takes any harshness in the partials as the whole character, because
    // the fundamental is more felt than heard.
    .growl_hz = 5.0f, .growl_depth = 0.035f, .growl_onset_s = 0.25f,
    .cutoff_soft = 2.6f, .cutoff_loud = 4.5f, .rolloff_exp = 4.0f,
    .drive_soft = 0.5f, .drive_loud = 1.1f,
    // One oscillator.  At 30Hz two detuned copies beat over about ten
    // seconds, so the fundamental would slowly vanish and come back.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 14,
    .min_partial_hz = 22.0f,
    .level_full = 0.22f,
    .attack_s = 0.005f, .release_s = 0.050f, .articulation_s = 0.012f, .glide_s = 0.004f,
    .out_gain = 0.996f,
  },
  {
    // Octaveless: a Shepard tone you can play.  The partials are octaves
    // rather than harmonics, and their loudness comes from a bell fixed in
    // Hz, so whistling an octave higher slides the whole stack one slot along
    // a curve that hasn't moved and lands on exactly the spectrum it started
    // from.  Play a rising scale and it rises without ever arriving: what
    // fades in at the bottom is what fades out at the top.
    //
    // The nominal fundamental is eight octaves down, which puts it at 2-12Hz.
    // That is not a pitch, it is the spacing of the stack; what you hear is
    // the bell, centred on 82Hz -- low E -- with nine octaves above the
    // fundamental reaching 3.1kHz at the top of the whistle range.
    //
    // The sizing is the whole design.  The whistle spans 2.5 octaves and the
    // stack spans 8, so the bell has about 5.5 octaves to sit in without
    // either tail running off an end, which is what would break the wrap.  A
    // sigma of 0.9 octaves fits with room to spare: the outermost component
    // is 60dB down at both extremes of the range.
    .name = "octaveless",
    .octave = 0.00390625f,   // 2^-8
    .octave_stack_hz = 82.0f, .octave_stack_width = 0.9f,
    .unison = 1, .harmonics = 9,
    // Nearly off, and that is a constraint rather than a taste.  Difference
    // tones between octave-spaced partials are themselves octave-spaced and
    // survive the wrap; the odd-order products at 3f and 5f are not, and they
    // are what would put an audible seam in the illusion.  This is as much
    // grit as the trick will take.
    .drive_soft = 0.8f, .drive_loud = 1.3f,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    // A longer glide than the other basses want, because the illusion needs
    // continuity: heard as a smooth sweep the stack has no octave at all,
    // and heard as discrete jumps the ear starts tracking one component and
    // finds the seam.
    .attack_s = 0.006f, .release_s = 0.090f, .articulation_s = 0.010f,
    .glide_s = 0.020f,
    .out_gain = 0.804f,
  },
  {
    // Half a register.  The bell follows the pitch at half rate, so an octave
    // of whistle moves the bass a fifth: the 2.5 octaves you can whistle map
    // into 1.25 octaves of bass, which fits between what a PA reproduces and
    // where a mandolin's low G starts.  The line keeps real contour and never
    // runs out of either end.
    //
    // 95Hz at a 1316Hz whistle -- the middle of the range -- puts the bell
    // between 61 and 147Hz across everything you can play.  The timbre still
    // repeats exactly, every two octaves now rather than every one.
    //
    // Octaveless: a Shepard tone you can play.  The partials are octaves
    // rather than harmonics, and their loudness comes from a bell fixed in
    // Hz, so whistling an octave higher slides the whole stack one slot along
    // a curve that hasn't moved and lands on exactly the spectrum it started
    // from.  Play a rising scale and it rises without ever arriving: what
    // fades in at the bottom is what fades out at the top.
    //
    // The nominal fundamental is eight octaves down, which puts it at 2-12Hz.
    // That is not a pitch, it is the spacing of the stack; what you hear is
    // the bell, centred on 82Hz -- low E -- with nine octaves above the
    // fundamental reaching 3.1kHz at the top of the whistle range.
    //
    // The sizing is the whole design.  The whistle spans 2.5 octaves and the
    // stack spans 8, so the bell has about 5.5 octaves to sit in without
    // either tail running off an end, which is what would break the wrap.  A
    // sigma of 0.9 octaves fits with room to spare: the outermost component
    // is 60dB down at both extremes of the range.
    .name = "octaveless-half",
    .octave = 0.00390625f,   // 2^-8
    .octave_stack_hz = 95.0f, .octave_stack_width = 0.9f,
    .octave_stack_track = 0.5f, .octave_stack_ref_hz = 1316.0f,
    .unison = 1, .harmonics = 9,
    // Nearly off, and that is a constraint rather than a taste.  Difference
    // tones between octave-spaced partials are themselves octave-spaced and
    // survive the wrap; the odd-order products at 3f and 5f are not, and they
    // are what would put an audible seam in the illusion.  This is as much
    // grit as the trick will take.
    .drive_soft = 0.8f, .drive_loud = 1.3f,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    // A longer glide than the other basses want, because the illusion needs
    // continuity: heard as a smooth sweep the stack has no octave at all,
    // and heard as discrete jumps the ear starts tracking one component and
    // finds the seam.
    .attack_s = 0.006f, .release_s = 0.090f, .articulation_s = 0.010f,
    .glide_s = 0.020f,
    .out_gain = 0.742f,
  },
  {
    // The Reese: two or three saws detuned far enough that the beating is the
    // instrument.  Four octaves down, so a comfortable whistle is 50-160Hz --
    // at the bottom of that the outer copies beat about twice a second, which
    // is the slow churn the sound is named for, and at the top about six.
    //
    // Everything `bass` says about not detuning a bass is true and this
    // ignores all of it on purpose: the smear *is* the patch.
    .name = "reese",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.28f, .pwm_slow_hz = 0.07f, .pwm_slow_depth = 0.03f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 3.0f, .cutoff_loud = 12.0f, .rolloff_exp = 2.0f,
    // Enough peak to hear the corner move, not enough to whistle.
    .resonance = 5.0f, .resonance_width = 0.35f,
    .drive_soft = 1.2f, .drive_loud = 2.6f,
    .unison = 3, .detune_cents = 9.0f, .harmonics = 32,
    // The bottom two partials come from one copy.  Measured before that, the
    // fundamental swung 31dB as the three copies drifted in and out of phase
    // -- in stereo that is the sound of a Reese and in mono it is the bass
    // falling out of the tune every couple of seconds.  The churn is still
    // there, it just lives above the octave now.
    .mono_partials = 2,
    .level_full = 0.22f,
    .attack_s = 0.008f, .release_s = 0.060f, .articulation_s = 0.008f,
    .glide_s = 0.006f,
    .out_gain = 0.501f,
  },
  {
    // The 808: nearly a sine, with the pitch dropping half an octave over the
    // first 35ms.  That drop is the whole sound -- it is what a drum machine
    // put there to fake the thump of a skin, and without it this is just a
    // sub.
    //
    // The release is 70ms, not the 300 an 808 actually rings for.  A record's
    // 808 is one note every beat or two with space around it; a bass line
    // under a dance tune is not, and at 300ms the notes run into each other
    // and the line stops being playable.  The pitch drop is what makes this
    // an 808, not the tail.
    //
    // Four octaves down rather than five, which is where the register wants
    // to be for this and not just where the other sub voices sit: an 808 is
    // its fundamental and almost nothing else, so it has to stay above
    // `min_partial_hz`.  Five octaves puts most of the whistle range under
    // 25Hz, and the voice would spend its time playing its second partial.
    .name = "eight-oh-eight",
    .tilt = 1.2f,
    .octave = 0.0625f,
    .pwm_center = 0.40f, .pwm_slow_hz = 0.05f, .pwm_slow_depth = 0.01f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.2f, .cutoff_loud = 6.0f, .rolloff_exp = 2.0f,
    .drop_octaves = 0.6f, .drop_s = 0.035f,
    // The dirt an 808 gets from being turned up.  Not a garnish: an 808 on a
    // record has been through a desk and a PA and is audibly saturated, and
    // that saturation is most of why it survives being played on a phone.
    .drive_soft = 1.6f, .drive_loud = 4.0f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 12,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.070f, .articulation_s = 0.012f,
    .glide_s = 0.004f,
        // Deliberately 3.2dB off the equal-LUFS match the rest of the table
    // keeps, which is the one thing the note at the top of this file says not
    // to do.  The reason: measured full-range this voice is level with the
    // others, and measured through a model of a K10.2 -- flat to about 55Hz,
    // then a ported box's 24dB/octave cliff -- it is 3.2dB under the lead.
    // So is every other bass voice, within a dB.  But this one is nearly pure
    // fundamental, and a near-sine at 40-200Hz is the case where a K-weighted
    // measurement most over-states what you actually hear.  Set by ear on the
    // speaker it gets played through, which beats the model.
    //
    // If the bass voices as a family turn out to sit low on that box, the fix
    // is to re-run the whole match against the same filter, not to keep
    // nudging one preset.
.out_gain = 0.562f,
  },
  {
    // A plucked bass: the first voice here whose note has a shape of its own.
    // Everything else sounds for exactly as long as you whistle, which is an
    // organ; this one speaks hard and falls back to a quarter of its peak in
    // a third of a second, so a run of notes has space between them and the
    // pulse comes from the attacks rather than from the gaps.
    .name = "pluck",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.30f, .pwm_slow_hz = 0.09f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 1.6f, .cutoff_loud = 7.0f, .rolloff_exp = 2.0f,
    // The filter falls with the note, which is what a plucked string does and
    // what makes the attack read as an attack rather than as a volume bump.
    .resonance = 2.5f, .resonance_width = 0.40f,
    .cutoff_env_octaves = 2.0f, .cutoff_env_s = 0.12f,
    // At 110bpm an eighth note is 270ms, so the decay has to do most of its
    // work inside that or the notes just run together at a lower level and
    // nothing has been gained.  150ms to a tenth of the peak puts a note
    // 11dB down by the time the next one lands.
    .decay_s = 0.15f, .sustain_level = 0.12f,
    .drive_soft = 1.0f, .drive_loud = 2.8f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.003f, .release_s = 0.060f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
        // Set by peak rather than by the K10.2 match the rest of the table uses,
    // and so it measures about 3.5dB under them.  A plucked envelope has a
    // high crest factor on purpose -- that is what an attack *is* -- so an
    // integrated measurement always reads low against a voice that sits at
    // one level, and matching it there put 0.18% of samples over 0.75 with
    // the peak at 0.985.  It will not sound quiet: the attacks arrive at the
    // same height as everything else, and on a dance floor the attack is the
    // part being listened to.
.out_gain = 1.004f,
  },
  {
    // Two-operator FM, and the only voice here that isn't a pulse wave.  The
    // modulator sits at the fundamental and the index runs 0.8 to 3.0 with
    // how hard you blow.
    //
    // Worth having because of *how* it opens up rather than how it sounds
    // standing still: a filter uncovers partials that were already there in
    // order, while the index moves energy around by Bessel functions, so
    // partials rise and fall out of order and some of them invert on the way.
    // It is the sound of a DX bass and there is no filter setting that
    // reaches it.
    .name = "fm",
    .octave = 0.0625f,
    .fm_ratio = 1.0f, .fm_index_soft = 0.8f, .fm_index_loud = 3.0f,
    .drive_soft = 1.0f, .drive_loud = 1.8f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 1,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.050f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
    .out_gain = 0.569f,
  },
  {
    // Valve grind.  The saturator is pushed off centre before it clips, which
    // is the difference between a transistor and a valve: an odd function can
    // only make odd harmonics however hard you drive it, and the bias is what
    // puts the even ones in.  Those land an octave above the fundamental --
    // in the gap between this and a mandolin's low G, which is otherwise the
    // emptiest part of the arrangement.
    .name = "grind",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.32f, .pwm_slow_hz = 0.08f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.5f, .cutoff_loud = 9.0f, .rolloff_exp = 2.0f,
    .drive_soft = 2.0f, .drive_loud = 5.0f, .drive_bias = 0.45f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.005f, .release_s = 0.050f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
    .out_gain = 0.519f,
  },
  {
    // A hollow square and nothing else: width exactly 0.5, which nulls every
    // even partial, no detune, no sweep, and as little drive as the voice
    // will take.  There is no trick in it.  It is here because a plain square
    // bass is a real sound with a long history and this table had no
    // deliberately plain voice in it to check the others against.
    .name = "square",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.50f, .pwm_slow_hz = 0.0f, .pwm_slow_depth = 0.0f,
    .growl_hz = 0.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.0f, .cutoff_loud = 8.0f, .rolloff_exp = 2.0f,
    .drive_soft = 0.6f, .drive_loud = 1.0f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.003f, .release_s = 0.040f, .articulation_s = 0.008f,
    .glide_s = 0.003f,
    .out_gain = 1.122f,
  },
};

#define N_PRESETS ((int)(sizeof(presets)/sizeof(presets[0])))

int synth_preset_count(void) {
  return N_PRESETS;
}

const char* synth_preset_name(int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    return "?";
  }
  return presets[preset].name;
}

// Where partial n actually sits, as a multiple of the fundamental.  The
// octave stack isn't a harmonic series: its partial n is the nth octave.
static float synth_partial_ratio(const struct SynthParams* p, int n) {
  if (p->octave_stack_hz > 0) {
    return exp2f((float)(n - 1));
  }
  return n * (1 + p->stretch * (n - 1));
}

// Whether the partials are far enough off the harmonic series that the
// Chebyshev recurrence can't reach them and each one needs its own phase.
static bool synth_needs_partial_phase(const struct SynthParams* p) {
  return p->stretch > 0 || p->octave_stack_hz > 0;
}

// White noise in -1..1.  Xorshift rather than an LCG, and not for the usual
// reasons: the low bits of a power-of-two LCG cycle with period 2, 4, 8...,
// so taking the whole word puts a periodic component *in* the noise, and
// through a band that emphasizes nothing in particular it is audible as a
// buzz rather than as air.  Xorshift's bits are all equally good.
static float synth_noise(struct Synth* s) {
  uint32_t x = s->noise_state ? s->noise_state : 0x9e3779b9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->noise_state = x;
  return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

// One-pole coefficient reaching ~63% of the way in `seconds`.
static float coeff(float seconds, float sample_rate) {
  if (seconds <= 0) {
    return 1;
  }
  return 1 - expf(-1.0f / (seconds * sample_rate));
}

// The pitch actually being played: the note, plus whatever the per-note drop
// and the vibrato are adding to it right now.  In log2 Hz, like log_freq.
static float synth_pitch_log(const struct Synth* s) {
  const struct SynthParams* p = s->params;
  float v = s->log_freq + s->drop;
  if (p->vibrato_cents > 0) {
    float onset = p->vibrato_onset_s > 0
        ? fminf(1, s->note_age / p->vibrato_onset_s) : 1;
    v += onset * (p->vibrato_cents / 1200.0f) *
         sinf(2 * (float)M_PI * s->vibrato_pos);
  }
  return v;
}

static float atan_norm(float v) {
  return atanf(v) / (float)(M_PI / 2);
}

void synth_preset_defaults(int preset, struct SynthParams* out) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  *out = presets[preset];
}

void synth_sanitize_params(struct SynthParams* p) {
  // level_full is a divisor, and octave multiplies every partial's frequency.
  if (!(p->level_full > 1e-4f)) {
    p->level_full = 1e-4f;
  }
  if (!(p->octave > 1e-4f)) {
    p->octave = 1e-4f;
  }
  if (p->unison < 1) {
    p->unison = 1;
  }
  if (p->unison > SYNTH_UNISON) {
    p->unison = SYNTH_UNISON;
  }
  if (p->harmonics < 1) {
    p->harmonics = 1;
  }
  if (p->harmonics > SYNTH_MAX_HARMONICS) {
    p->harmonics = SYNTH_MAX_HARMONICS;
  }
  // powf(n, tilt) and powf(n/cutoff, rolloff_exp) both want a positive base
  // and a finite exponent; cutoff_* are divisors.
  if (!(p->cutoff_soft > 1e-3f)) {
    p->cutoff_soft = 1e-3f;
  }
  if (!(p->cutoff_loud > 1e-3f)) {
    p->cutoff_loud = 1e-3f;
  }
  if (!(p->rolloff_exp > 0)) {
    p->rolloff_exp = 1;
  }
  if (p->growl_onset_s <= 0) {
    p->growl_onset_s = 1e-3f;
  }
  if (p->min_partial_hz < 0) {
    p->min_partial_hz = 0;
  }
  if (!(p->stereo_width >= 0)) {
    p->stereo_width = 0;
  }
  if (p->stereo_width > 1) {
    p->stereo_width = 1;
  }
  // Both widths are divisors, and only mean anything when the thing they
  // shape is switched on.
  if (p->octave_stack_hz > 0 && !(p->octave_stack_width > 1e-3f)) {
    p->octave_stack_width = 1;
  }
  if (p->resonance > 0 && !(p->resonance_width > 1e-3f)) {
    p->resonance_width = 0.25f;
  }
  if (!(p->breath >= 0)) {
    p->breath = 0;
  }
  if (p->mono_partials < 0) {
    p->mono_partials = 0;
  }
  if (p->mono_partials > SYNTH_MAX_HARMONICS) {
    p->mono_partials = SYNTH_MAX_HARMONICS;
  }
  if (!(p->sustain_level >= 0)) {
    p->sustain_level = 0;
  }
  if (p->sustain_level > 1) {
    p->sustain_level = 1;
  }
  // Both are divisors or multipliers that only mean anything when the thing
  // they belong to is switched on.
  if ((p->fm_index_soft > 0 || p->fm_index_loud > 0) && !(p->fm_ratio > 0)) {
    p->fm_ratio = 1;
  }
  if (p->octave_stack_track > 0 && !(p->octave_stack_ref_hz > 1)) {
    p->octave_stack_ref_hz = 1000;
  }
}

void synth_set_params(struct Synth* s, const struct SynthParams* params) {
  s->params = params;

  int unison = params->unison;
  if (unison < 1) {
    unison = 1;
  }
  if (unison > SYNTH_UNISON) {
    unison = SYNTH_UNISON;
  }
  s->unison = unison;

  // Spread symmetrically about the centre voice, in tuning and in position:
  // the copy that is flat is the one on the left.  Symmetric means the pans
  // sum to zero, which is what keeps the side signal from leaking a copy of
  // the mid into the image.
  for (int u = 0; u < unison; u++) {
    float spread = unison > 1
      ? (u / (float)(unison - 1)) * 2 - 1   // -1..1
      : 0;
    s->detune[u] = exp2f(spread * params->detune_cents / 1200.0f);
    s->pan[u] = spread;
    // The first copy carries the low partials alone, at the level the rest
    // get from `unison` copies summing in power.
    s->low_gain[u] = u == 0 ? sqrtf((float)unison) : 0.0f;
  }
}

void synth_set_preset(struct Synth* s, int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  synth_set_params(s, &presets[preset]);
}

void synth_init(struct Synth* s, float sample_rate, int preset) {
  memset(s, 0, sizeof(*s));
  s->sample_rate = sample_rate;
  s->log_freq = log2f(440.0f);
  s->control_countdown = 0;
  // All together: detune pulls them apart within a second, and starting them
  // deliberately spread would mean starting partly cancelled.
  for (int u = 0; u < SYNTH_UNISON; u++) {
    s->phase[u] = 0;
  }
  // Not zero: a plucked voice whose envelope hasn't been armed yet should be
  // at full, not silent, or the very first note is missing its attack.
  s->pluck = 1;
  synth_set_preset(s, preset);
}

// Recomputes timbre and drive.  Called at SYNTH_CONTROL_HOP, not per sample.
static void update_controls(struct Synth* s, bool voiced) {
  const struct SynthParams* p = s->params;

  float reach = fmaxf(0, s->level / p->level_full);

  // Loudness stays close to what was played -- a slight compression, no more,
  // or the lead stops responding to how hard it's being pushed.  It is frozen
  // once the note is released, because from there the gate does the fading
  // and having both fall would halve the release time.
  if (voiced) {
    s->loudness = fminf(1, powf(reach, 0.8f));
  }

  // Brightness and drive get a soft knee instead: they should keep responding
  // when the player leans past what we called full, and a mis-set input level
  // should cost expression rather than usability.  Unlike loudness this keeps
  // following the level as it falls away, so the tail of a note darkens
  // rather than buzzing at full brightness the whole way down.
  float dynamics = 1 - expf(-2.2f * reach);
  s->dynamics = dynamics;

  // How long they have been on this note.  Note length rather than level,
  // because these want to do different jobs: brightness has to arrive on
  // every note of a run, and the growl must not.
  float sustain = fminf(1, s->note_age / p->growl_onset_s);

  float width = p->pwm_center +
    p->pwm_slow_depth * sinf(2 * (float)M_PI * s->pwm_pos) +
    p->growl_depth * sustain * sinf(2 * (float)M_PI * s->growl_pos);
  width = fmaxf(0.06f, fminf(0.5f, width));

  float cutoff = p->cutoff_soft + (p->cutoff_loud - p->cutoff_soft) * dynamics;
  // Both cutoff modulations are in octaves and multiply, so a wobble stays
  // the same wobble wherever the dynamics and the note envelope have put the
  // filter.  cutoff_env is what the note has left of its opening sweep.
  if (p->wobble_octaves > 0) {
    cutoff *= exp2f(p->wobble_octaves * sinf(2 * (float)M_PI * s->wobble_pos));
  }
  if (p->cutoff_env_octaves > 0) {
    cutoff *= exp2f(s->cutoff_env);
  }
  s->drive = p->drive_soft + (p->drive_loud - p->drive_soft) * dynamics;
  s->fm_index =
    p->fm_index_soft + (p->fm_index_loud - p->fm_index_soft) * dynamics;

  // Stop before Nyquist rather than aliasing back down.  The highest unison
  // voice is the one that runs out of room first.
  float f0 = exp2f(synth_pitch_log(s)) * p->octave * s->detune[s->unison - 1];
  int active = p->harmonics;
  if (active > SYNTH_MAX_HARMONICS) {
    active = SYNTH_MAX_HARMONICS;
  }
  while (active > 1 &&
         f0 * synth_partial_ratio(p, active) > s->sample_rate * 0.45f) {
    active--;
  }
  s->harmonics_active = active;

  float base = exp2f(synth_pitch_log(s)) * p->octave;

  // Drop partials too low for anything to reproduce.
  int lowest = 1;
  if (p->min_partial_hz > 0) {
    while (lowest <= active &&
           base * synth_partial_ratio(p, lowest) < p->min_partial_hz) {
      lowest++;
    }
  }

  float power = 0;
  for (int i = 0; i < lowest - 1 && i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_target[i] = 0;
  }
  for (int i = lowest - 1; i < active; i++) {
    int n = i + 1;
    float amp;
    if (p->octave_stack_hz > 0) {
      // A bell fixed in Hz, not in partial number.  That is the whole trick:
      // the components move under a weighting that doesn't, so an octave of
      // played pitch slides the stack one slot and lands back on itself.
      float hz = base * synth_partial_ratio(p, n);
      float bell = p->octave_stack_hz;
      if (p->octave_stack_track > 0) {
        // Moves `track` octaves for every octave of played pitch.  At 0 this
        // is the fixed bell that makes the voice octaveless; anything above
        // trades some of that for melodic contour.
        bell *= exp2f(p->octave_stack_track *
                      (synth_pitch_log(s) - log2f(p->octave_stack_ref_hz)));
      }
      float x = log2f(hz / bell) / p->octave_stack_width;
      amp = expf(-0.5f * x * x);
    } else {
      float rolloff = 1 / (1 + powf(n / cutoff, p->rolloff_exp));
      if (p->resonance > 0) {
        // A bump at the cutoff, on top of the rolloff rather than replacing
        // it, so the peak rides the same sweep the corner does.
        float d = log2f(n / cutoff) / p->resonance_width;
        rolloff *= 1 + p->resonance * expf(-0.5f * d * d);
      }
      // Negative is meaningful and wanted: past the first null the pulse
      // series flips sign, and that is part of the PWM sound.
      amp = sinf(n * (float)M_PI * width) / powf((float)n, p->tilt) * rolloff;
    }
    s->harmonic_target[i] = amp;
    power += amp * amp;
  }
  for (int i = active; i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_target[i] = 0;
  }

  // Hold the level steady as the timbre moves, so the only thing changing how
  // loud this is is how hard the player is blowing.  Unison voices are
  // mutually detuned and so sum in power, not amplitude.
  float norm = sqrtf(power * s->unison * 0.5f);
  if (norm < 1e-6f) {
    norm = 1e-6f;
  }
  for (int i = lowest - 1; i < active; i++) {
    s->harmonic_target[i] *= 0.7f / norm;
  }
}

float synth_process(struct Synth* s, const struct PitchHint* hint) {
  const struct SynthParams* p = s->params;

  if (hint->onset) {
    // Land on the note rather than gliding onto it, and restart the growl
    // timer so the wobble belongs to this note.  Take the level as read too,
    // so the attack has this note's dynamics and not the last one's.
    s->log_freq = log2f(fmaxf(1, hint->freq));
    s->note_age = 0;
    s->level = hint->level;
    s->control_countdown = 0;   // this note's dynamics, not the last one's
    // Both per-note sweeps are armed here and decay from here on.  They are
    // set rather than added to, so a fast run gets the same swoop on every
    // note instead of stacking them up.
    s->drop = p->drop_octaves;
    s->cutoff_env = p->cutoff_env_octaves;
    s->pluck = 1;
  } else if (hint->voiced) {
    // Otherwise track continuously.  Glide is in the log domain so a bend
    // takes the same time everywhere, and it is fast enough to feel instant
    // while still smoothing the detector's hop-to-hop jitter.  When the hint
    // is not trustworthy its freq simply hasn't moved, so this holds.
    float target = log2f(fmaxf(1, hint->freq));
    s->log_freq += coeff(p->glide_s, s->sample_rate) * (target - s->log_freq);
  }

  // The level drives the timbre, and tracks the input quickly while a note is
  // sounding.  Once the note is released it is *held*, not decayed: letting
  // it fall sweeps the cutoff across every partial in a few tens of
  // milliseconds, and sweeping thirty-odd harmonic amplitudes that fast
  // splatters -- it puts a scatter of non-harmonic energy around 1-3kHz into
  // the tail, which is audible precisely because the fundamental is on its
  // way out and no longer covers it.  Freezing the timbre and letting the
  // gate do the fading costs a little realism (real instruments do get
  // duller as they decay) and removes the artifact.
  if (hint->voiced) {
    s->level +=
      coeff(p->articulation_s, s->sample_rate) * (hint->level - s->level);
  }

  // Note the pre-decrement placement: `countdown-- <= 0` after setting
  // SYNTH_CONTROL_HOP fires every HOP+1 samples, not every HOP.
  if (--s->control_countdown <= 0) {
    update_controls(s, hint->voiced);
    s->control_countdown = SYNTH_CONTROL_HOP;
  }
  if (hint->onset) {
    // Start at this note's dynamics rather than ramping up into them.
    s->loudness_env = s->loudness;
  }

  // The note starting and stopping.  This is the only thing that starts or
  // stops sound -- there is no hard gate anywhere, so an uncertain detector
  // costs a fade rather than a click.
  float gate_target = hint->voiced ? 1.0f : 0.0f;
  float gate_coeff = coeff(
      gate_target > s->gate ? p->attack_s : p->release_s, s->sample_rate);
  s->gate += gate_coeff * (gate_target - s->gate);

  // How hard they are blowing, quick in both directions so the dips between
  // tongued notes survive.
  s->loudness_env +=
    coeff(p->articulation_s, s->sample_rate) * (s->loudness - s->loudness_env);

  // Three envelopes multiplied: the note starting and stopping, how hard the
  // player is blowing, and -- for the plucked voices -- where in the note's
  // own decay we are.
  s->amp = s->gate * s->loudness_env;
  if (p->decay_s > 0) {
    s->amp *= s->pluck;
  }

  if (hint->voiced) {
    s->note_age += 1.0f / s->sample_rate;
  } else {
    s->note_age = 0;
  }

  s->pwm_pos += p->pwm_slow_hz / s->sample_rate;
  if (s->pwm_pos >= 1) {
    s->pwm_pos -= 1;
  }
  s->growl_pos += p->growl_hz / s->sample_rate;
  if (s->growl_pos >= 1) {
    s->growl_pos -= 1;
  }
  // Free-running, like the growl: a wobble is a property of the patch, not of
  // where in the note you are, and restarting it per note is the one thing
  // that stops it sounding like a wobble at all.
  s->wobble_pos += p->wobble_hz / s->sample_rate;
  if (s->wobble_pos >= 1) {
    s->wobble_pos -= 1;
  }
  s->vibrato_pos += p->vibrato_hz / s->sample_rate;
  if (s->vibrato_pos >= 1) {
    s->vibrato_pos -= 1;
  }

  s->drop -= coeff(p->drop_s, s->sample_rate) * s->drop;
  s->cutoff_env -= coeff(p->cutoff_env_s, s->sample_rate) * s->cutoff_env;
  if (p->decay_s > 0) {
    s->pluck += coeff(p->decay_s, s->sample_rate) * (p->sustain_level - s->pluck);
  }

  // Everything that comes out of update_controls steps at the control rate,
  // and anything that reaches the output unsmoothed puts a tone there: a
  // gain that jumps 1455 times a second buzzes at 1455Hz, fixed, regardless
  // of what is being played.  The harmonic amplitudes were already smoothed;
  // the drive was not, and it multiplies the entire signal.
  float smooth = coeff(0.004f, s->sample_rate);
  for (int i = 0; i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_amp[i] += smooth * (s->harmonic_target[i] - s->harmonic_amp[i]);
  }
  s->drive_smoothed += smooth * (s->drive - s->drive_smoothed);
  s->fm_index_smoothed += smooth * (s->fm_index - s->fm_index_smoothed);

  if (s->amp < 1e-5f && !hint->voiced) {
    s->side = 0;
    return 0;
  }

  float f0 = exp2f(synth_pitch_log(s)) * p->octave;
  float out = 0;
  // The same sum again, but weighted by where each copy sits.  Accumulated
  // here rather than reconstructed later because this is the only place the
  // copies exist separately -- one line down they are one signal.
  float spread = 0;
  for (int u = 0; u < s->unison; u++) {
    float base = f0 * s->detune[u];
    float voice = 0;

    if (p->fm_index_loud > 0 || p->fm_index_soft > 0) {
      // Two-operator FM instead of the additive sum.  Nothing above the
      // fundamental is chosen here -- the index decides how much energy sits
      // where, and the Bessel envelope it produces moves in a way no
      // arrangement of `cutoff` and `tilt` reaches.  Scaled to 0.7 so it
      // arrives at the drive at the same level the additive path does.
      s->fm_phase[u] += base * p->fm_ratio / s->sample_rate;
      if (s->fm_phase[u] >= 1) {
        s->fm_phase[u] -= (int)s->fm_phase[u];
      }
      s->phase[u] += base / s->sample_rate;
      if (s->phase[u] >= 1) {
        s->phase[u] -= (int)s->phase[u];
      }
      float mod = sinf(2 * (float)M_PI * s->fm_phase[u]);
      voice = 0.7f * sinf(2 * (float)M_PI * s->phase[u] +
                          s->fm_index_smoothed * mod);
    } else if (synth_needs_partial_phase(p)) {
      // Partials aren't multiples of anything, so each one carries its own
      // phase and costs a sinf.
      for (int i = 0; i < s->harmonics_active; i++) {
        float* ph = &s->partial_phase[u][i];
        *ph += base * synth_partial_ratio(p, i + 1) / s->sample_rate;
        if (*ph >= 1) {
          *ph -= (int)*ph;
        }
        float a = s->harmonic_amp[i];
        if (i < p->mono_partials) {
          a *= s->low_gain[u];
        }
        voice += a * sinf(2 * (float)M_PI * *ph);
      }
    } else {
      s->phase[u] += base / s->sample_rate;
      if (s->phase[u] >= 1) {
        s->phase[u] -= (int)s->phase[u];
      }

      float theta = 2 * (float)M_PI * s->phase[u];
      float cos1 = cosf(theta);
      // sin(n*theta) by the Chebyshev recurrence, so each partial costs two
      // multiplies instead of a sinf.
      float sin_prev = 0;              // sin(0)
      float sin_cur = sinf(theta);     // sin(theta)
      for (int i = 0; i < s->harmonics_active; i++) {
        float a = s->harmonic_amp[i];
        if (i < p->mono_partials) {
          a *= s->low_gain[u];
        }
        voice += a * sin_cur;
        float next = 2 * cos1 * sin_cur - sin_prev;
        sin_prev = sin_cur;
        sin_cur = next;
      }
    }

    out += voice;
    spread += voice * s->pan[u];
  }

  // Drive before the envelope, so how dirty it sounds is set by how hard the
  // player is blowing and not by where they are in the note's decay.  This is
  // also most of what makes the voice cut: it fills in the partials above the
  // ones we synthesize.
  float undriven = out;
  if (p->drive_bias != 0) {
    // Off centre going in, and the saturator's value at that offset taken
    // back out so nothing is left at DC.  An odd function driven hard makes
    // only odd harmonics; this is what puts the even ones in.
    float b = p->drive_bias;
    out = atan_norm((out + b) * s->drive_smoothed) -
          atan_norm(b * s->drive_smoothed);
  } else {
    out = atan_norm(out * s->drive_smoothed);
  }

  // The gain the saturation just applied.  The side signal is scaled by this
  // rather than being saturated itself: running a difference of two copies
  // through its own atan normalizes it against a sum of three, which leaves
  // the sides louder than the middle and the image inside-out.  Taking the
  // gain instead keeps the spread in the same proportion to the note that it
  // had before the drive.
  float drive_gain = fabsf(undriven) > 1e-6f
      ? out / undriven : atan_norm(s->drive_smoothed);

  // Breath, added after the drive rather than through it: on a real flute the
  // noise is made at the embouchure and shares the tube, not the reed, so
  // saturating it along with the tone reads as a fuzzbox rather than as air.
  // The band tracks the note because the tube's resonances do.
  //
  // The level is set by the unweighted tone-to-noise ratio and not by LUFS,
  // which is misleading here: a K-weighted measurement of a 100Hz near-sine
  // against a noise band two octaves above it called the air "0.9LU over the
  // tone" when the actual ratio was 6.6dB, which is not any flute.  A real
  // flute's low register runs 15-25dB and the breathy big ones about 10-15.
  if (p->breath > 0) {
    float n = synth_noise(s);
    // A resonant bandpass, not a pair of gentle slopes.  Air in a tube is a
    // *band* -- the noise excites the bore and comes back with the bore's
    // shape on it -- and white noise with a tilt on it is not that.  A
    // Chamberlin state variable filter is two adds and two multiplies and
    // gives a real resonant band, which the cascaded one-poles could not:
    // any arrangement of one-poles is a slope, and a slope up to a couple of
    // kHz is what a snare sounds like.
    //
    // Centred low, at two and a half times the fundamental, because that is
    // where a large flute's air sits.  Capped in absolute Hz so the top of
    // the range doesn't drag the band up into the hiss again.
    float fc = fminf(900.0f, f0 * 2.5f);
    float f = 2 * sinf((float)M_PI * fc / s->sample_rate);
    const float q = 1.0f / 1.2f;
    s->breath_lp += f * s->breath_bp;
    float high = n - s->breath_lp - q * s->breath_bp;
    s->breath_bp += f * high;

    // Modulated by the tone, but only slightly.  The jet at the embouchure
    // makes both the note and the noise, so some correlation is right and it
    // is what puts the noise around the harmonics rather than under them --
    // but at any depth worth noticing it stops being a flute and becomes a
    // snare, which is a band of noise switched on and off at a low pitch.
    // 15% is enough to bind the air to the note and not enough to rattle.
    out += s->breath_bp * p->breath * (1.0f - 0.4f * s->dynamics) *
           (0.9f + 0.15f * out);
  }

  // High-pass after the drive, since the drive is what puts energy back below
  // the partials we were careful not to synthesize.
  if (p->min_partial_hz > 0) {
    // Below the lowest partial we allow, not at it: the job here is to
    // remove what the drive invented underneath, not to thin out the
    // fundamental we deliberately kept.
    float rc = 1.0f / (2 * (float)M_PI * p->min_partial_hz * 0.7f);
    float a = rc / (rc + 1.0f / s->sample_rate);
    for (int stage = 0; stage < 2; stage++) {
      float y = a * (s->hp_y[stage] + out - s->hp_x[stage]);
      s->hp_x[stage] = out;
      s->hp_y[stage] = y;
      out = y;
    }
  }

  // Same envelope and gain as the mid, so width changes where a note sits
  // and not how loud it is.  It skips the high-pass: only single-oscillator
  // voices ask for one, and those have no spread to begin with.
  //
  // Left is mid+side and right is mid-side, so the two channels sum back to
  // exactly twice the mono output -- a stereo image that folds down without
  // anything cancelling, which for detuned copies is otherwise exactly what
  // goes wrong.
  s->side = spread * drive_gain * p->stereo_width * s->amp * p->out_gain;

  return out * s->amp * p->out_gain;
}
