#include "engine.h"

#include <math.h>
#include <string.h>

// Roughly 3.5dB a step, so the knob feels even.
static const float volume_steps[10] = {
  0.026f, 0.039f, 0.059f, 0.088f, 0.132f,
  0.198f, 0.296f, 0.444f, 0.667f, 1.000f,
};

static float clip(float v) {
  return fmaxf(-1, fminf(1, v));
}

static int clamp_step(int step) {
  if (step < 0) {
    return 0;
  }
  if (step > 9) {
    return 9;
  }
  return step;
}

void engine_init(struct Engine* e, float sample_rate) {
  memset(e, 0, sizeof(*e));
  pitch_init(&e->detector, sample_rate, ENGINE_MIN_HZ, ENGINE_MAX_HZ);
  synth_init(&e->synth, sample_rate, 1);
  engine_set_voice(e, 2);
  engine_set_volume(e, 5);
  engine_set_gate(e, 5);
  engine_set_fifth(e, 0);
  engine_set_sustain(e, 0);
}

// Voice 0 is the raw input; 1..synth_preset_count() are synth presets.  Out
// of range falls back to the first preset rather than going silent, since
// this is driven by a file that anything can write to.
static int clamp_voice(int voice) {
  if (voice < 0 || voice > synth_preset_count()) {
    return 1;
  }
  return voice;
}

void engine_set_voice(struct Engine* e, int voice) {
  voice = clamp_voice(voice);
  e->passthrough = (voice == 0);
  if (!e->passthrough) {
    synth_set_preset(&e->synth, voice - 1);
  }
}

void engine_set_fifth(struct Engine* e, int step) {
  // Anything non-zero counts as on: the control file holds 0 or 1, and a
  // stray 2 should transpose rather than silently do nothing.
  synth_set_fifth(&e->synth, step != 0);
}

void engine_set_sustain(struct Engine* e, int step) {
  synth_set_sustain(&e->synth, step != 0);
}

void engine_set_params(struct Engine* e, const struct SynthParams* params) {
  if (!e->passthrough) {
    synth_set_params(&e->synth, params);
  }
}

const char* engine_voice_name(int voice) {
  int clamped = clamp_voice(voice);
  if (clamped == 0) {
    return "raw input (passthrough)";
  }
  return synth_preset_name(clamped - 1);
}

void engine_set_volume(struct Engine* e, int step) {
  e->volume = volume_steps[clamp_step(step)];
}

void engine_set_gate(struct Engine* e, int step) {
  // Keeps the feel of the old knob -- higher numbers gate less -- but the
  // number now means "how far above the room noise a note has to be", which
  // is the same on any microphone.  Step 5 is 3x the noise floor.
  step = clamp_step(step);
  pitch_set_gate(&e->detector, 3.0f * powf(1.5f, (float)(5 - step)));
}

float engine_take_peak_level(struct Engine* e) {
  float peak = e->peak_level;
  e->peak_level = 0;
  return peak;
}

// Everything both process functions share.  Leaves the stereo difference for
// this sample in e->synth.side, which is zero in passthrough and for any
// voice running a single oscillator.
static float engine_render(struct Engine* e, float in) {
  // The detector always runs, even in passthrough, so switching voices
  // doesn't start from a cold buffer.
  const struct PitchHint* hint = pitch_process(&e->detector, in);

  if (hint->voiced && hint->level > e->peak_level) {
    e->peak_level = hint->level;
  }

  if (e->passthrough) {
    e->synth.side = 0;
    return in;
  }
  return synth_process(&e->synth, hint);
}

float engine_process(struct Engine* e, float in) {
  return clip(engine_render(e, in) * e->volume);
}

void engine_process_stereo(struct Engine* e, float in,
                           float* left, float* right) {
  float value = engine_render(e, in);
  float side = e->synth.side;
  // Clipped per channel rather than before the split, so width can never push
  // a channel past full scale.
  *left = clip((value + side) * e->volume);
  *right = clip((value - side) * e->volume);
}
