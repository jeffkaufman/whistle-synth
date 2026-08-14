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

void engine_init(struct Engine* e, float sample_rate, float* delay_history) {
  memset(e, 0, sizeof(*e));
  pitch_init(&e->detector, sample_rate, ENGINE_MIN_HZ, ENGINE_MAX_HZ);
  synth_init(&e->synth, sample_rate, 1);
  if (delay_history) {
    delay_init(&e->delay, delay_history, sample_rate);
  }
  engine_set_voice(e, 2);
  engine_set_volume(e, 5);
  engine_set_gate(e, 5);
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

void engine_process(struct Engine* e, float in_main, float in_delay,
                    float* out_main, float* out_delay) {
  // The detector always runs, even in passthrough, so switching voices
  // doesn't start from a cold buffer.
  const struct PitchHint* hint = pitch_process(&e->detector, in_main);

  if (hint->voiced && hint->level > e->peak_level) {
    e->peak_level = hint->level;
  }

  float value = e->passthrough ? in_main : synth_process(&e->synth, hint);

  *out_main = clip(value * e->volume);
  *out_delay = e->delay.history ? clip(delay_process(&e->delay, in_delay)) : 0;
}
