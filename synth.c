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
// `out_gain` is not a taste control.  Each one is set so that all the presets
// measure the same loudness on a full-range system -- equal LUFS, ITU-R
// BS.1770, which is the standard model for that and the reason a bass voice
// has to be numerically hotter than a lead to sound as loud.  Changing one
// means re-running the loudness match, not just that voice.  The match is
// run over recordings/whistling.f32, a recording of the real instrument,
// because it depends on the material: matched on synthetic test tones the
// numbers come out several dB different.

static const struct SynthParams presets[] = {
  {
    .name = "lead",
    .tilt = 1.0f,
    .octave = 0.5f,
    .pwm_center = 0.34f, .pwm_slow_hz = 0.19f, .pwm_slow_depth = 0.11f,
    .growl_hz = 5.0f, .growl_depth = 0.035f, .growl_onset_s = 0.45f,
    .cutoff_soft = 1.3f, .cutoff_loud = 5.5f, .rolloff_exp = 2.0f,
    .drive_soft = 0.7f, .drive_loud = 2.2f,
    .unison = 3, .detune_cents = 4.0f, .harmonics = 10,
    .level_full = 0.22f,
    .attack_s = 0.007f, .release_s = 0.040f, .articulation_s = 0.005f, .glide_s = 0.005f,
    .out_gain = 0.488f,
  },
  {
    // Three octaves down puts a comfortable whistle around 100-300Hz, which
    // is tenor trombone, and leaves room for a fiddle above and a piano
    // underneath.
    .name = "trombone",
    .tilt = 0.7f,
    .octave = 0.125f,
    // Brass doesn't chorus, so almost no width sweep -- but it does growl,
    // and it growls on the long notes, which is exactly what note length is
    // wired to here.
    .pwm_center = 0.34f, .pwm_slow_hz = 0.13f, .pwm_slow_depth = 0.05f,
    .growl_hz = 5.0f, .growl_depth = 0.055f, .growl_onset_s = 0.50f,
    // The widest brightness range of any preset: going from nothing to
    // blazing as you lean on it is the single most recognizable thing about
    // a brass instrument, more than any particular harmonic recipe.
    .cutoff_soft = 1.1f, .cutoff_loud = 14.0f, .rolloff_exp = 2.0f,
    .drive_soft = 0.8f, .drive_loud = 4.0f,
    // One instrument, not a section: just enough spread to stop it sounding
    // like a test tone.
    .unison = 2, .detune_cents = 2.5f, .harmonics = 28,
    .level_full = 0.22f,
    // Slower on and off than a lead, and a slower glide, which reads as the
    // slide.  Too much more and runs start to smear.
    .attack_s = 0.022f, .release_s = 0.055f, .articulation_s = 0.009f, .glide_s = 0.009f,
    .out_gain = 0.455f,
  },
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
    .out_gain = 0.557f,
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
    .out_gain = 0.985f,
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

// Where partial n actually sits, as a multiple of the fundamental.
static float synth_partial_ratio(const struct SynthParams* p, int n) {
  return n * (1 + p->stretch * (n - 1));
}

// One-pole coefficient reaching ~63% of the way in `seconds`.
static float coeff(float seconds, float sample_rate) {
  if (seconds <= 0) {
    return 1;
  }
  return 1 - expf(-1.0f / (seconds * sample_rate));
}

static float atan_norm(float v) {
  return atanf(v) / (float)(M_PI / 2);
}

void synth_set_preset(struct Synth* s, int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  s->params = &presets[preset];

  int unison = s->params->unison;
  if (unison < 1) {
    unison = 1;
  }
  if (unison > SYNTH_UNISON) {
    unison = SYNTH_UNISON;
  }
  s->unison = unison;

  // Spread symmetrically about the centre voice.
  for (int u = 0; u < unison; u++) {
    float spread = unison > 1
      ? (u / (float)(unison - 1)) * 2 - 1   // -1..1
      : 0;
    s->detune[u] = exp2f(spread * s->params->detune_cents / 1200.0f);
  }
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
  s->drive = p->drive_soft + (p->drive_loud - p->drive_soft) * dynamics;

  // Stop before Nyquist rather than aliasing back down.  The highest unison
  // voice is the one that runs out of room first.
  float f0 = exp2f(s->log_freq) * p->octave * s->detune[s->unison - 1];
  int active = p->harmonics;
  if (active > SYNTH_MAX_HARMONICS) {
    active = SYNTH_MAX_HARMONICS;
  }
  while (active > 1 &&
         f0 * synth_partial_ratio(p, active) > s->sample_rate * 0.45f) {
    active--;
  }
  s->harmonics_active = active;

  // Drop partials too low for anything to reproduce.
  int lowest = 1;
  if (p->min_partial_hz > 0) {
    float base = exp2f(s->log_freq) * p->octave;
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
    float rolloff = 1 / (1 + powf(n / cutoff, p->rolloff_exp));
    // Negative is meaningful and wanted: past the first null the pulse series
    // flips sign, and that is part of the PWM sound.
    s->harmonic_target[i] =
      sinf(n * (float)M_PI * width) / powf((float)n, p->tilt) * rolloff;
    power += s->harmonic_target[i] * s->harmonic_target[i];
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

  s->amp = s->gate * s->loudness_env;

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

  if (s->amp < 1e-5f && !hint->voiced) {
    return 0;
  }

  float f0 = exp2f(s->log_freq) * p->octave;
  float out = 0;
  for (int u = 0; u < s->unison; u++) {
    float base = f0 * s->detune[u];

    if (p->stretch > 0) {
      // Partials aren't multiples of anything, so each one carries its own
      // phase and costs a sinf.
      for (int i = 0; i < s->harmonics_active; i++) {
        float* ph = &s->partial_phase[u][i];
        *ph += base * synth_partial_ratio(p, i + 1) / s->sample_rate;
        if (*ph >= 1) {
          *ph -= (int)*ph;
        }
        out += s->harmonic_amp[i] * sinf(2 * (float)M_PI * *ph);
      }
      continue;
    }

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
      out += s->harmonic_amp[i] * sin_cur;
      float next = 2 * cos1 * sin_cur - sin_prev;
      sin_prev = sin_cur;
      sin_cur = next;
    }
  }

  // Drive before the envelope, so how dirty it sounds is set by how hard the
  // player is blowing and not by where they are in the note's decay.  This is
  // also most of what makes the voice cut: it fills in the partials above the
  // ones we synthesize.
  out = atan_norm(out * s->drive_smoothed);

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

  return out * s->amp * p->out_gain;
}
