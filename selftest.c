#include "selftest.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Peak amplitude of the played whistle.  Loud enough to be heard clearly by a
// microphone resting against a headphone, quiet enough not to be unpleasant.
#define STIMULUS_AMP 0.20f

// Noise amplitudes, as a fraction of the whistle.  The last one is severe on
// purpose: it should be where detection finally gives up, and it is useful to
// know that it fails by going quiet rather than by making noise.
static const float noise_levels[] = { 0.0f, 0.03f, 0.10f, 0.30f };
#define N_NOISE_LEVELS ((int)(sizeof(noise_levels)/sizeof(noise_levels[0])))

static float semitone(float steps) {
  return 880.0f * powf(2.0f, steps / 12.0f);
}

static void add_event(struct SelfTest* t, float freq, float amp, float noise,
                      float seconds) {
  if (t->event_count >= SELFTEST_MAX_EVENTS) {
    return;
  }
  struct TestEvent* e = &t->events[t->event_count++];
  e->freq = freq;
  e->amp = amp;
  e->noise = noise;
  e->samples = (int)(seconds * t->sample_rate);
  e->label = NULL;
}

static void add_prompt(struct SelfTest* t, const char* label, float seconds) {
  add_event(t, 0, 0, 0, seconds);
  t->events[t->event_count - 1].label = label;
}

static void build_monitor(struct SelfTest* t) {
  add_prompt(t, "play normally -- get used to the sound", 12.0f);
  add_prompt(t, "NOW: whatever brings the noise out.  Keep doing it.", 25.0f);
  add_prompt(t, "again, and let notes trail off slowly at the end", 25.0f);
  add_prompt(t, "again, loud notes, then very quiet ones", 20.0f);
  add_prompt(t, "last stretch -- anything that provokes it", 20.0f);
  add_prompt(t, "done", 2.0f);
}

// The material worth having, in the order it is easiest to play.
static void build_record(struct SelfTest* t) {
  add_prompt(t, "get comfortable -- don't play yet", 3.0f);
  add_prompt(t, "SILENCE: let the room be quiet", 4.0f);
  add_prompt(t, "LEVEL CHECK: hold one steady note, as loud as you'd play", 6.0f);
  add_prompt(t, "SOFT: a few long notes, as quietly as you can", 8.0f);
  add_prompt(t, "LOUD: the same notes, pushing hard", 8.0f);
  add_prompt(t, "SWELL: one long note, quiet -> loud -> quiet", 8.0f);
  add_prompt(t, "SLOW TUNE: any tune, smooth and connected", 10.0f);
  add_prompt(t, "FAST + SEPARATED: eighth notes, break every note", 12.0f);
  add_prompt(t, "FASTEST: same again, as fast as you can articulate", 10.0f);
  add_prompt(t, "TRAIL OFF: long notes, let each one fade to nothing", 10.0f);
  add_prompt(t, "LEAPS: jump around, including big intervals", 8.0f);
  add_prompt(t, "REAL TUNE: play like you would on the gig", 15.0f);
  add_prompt(t, "done -- stop playing", 2.0f);
}

// Tones an octave apart from below a bass guitar's low E up to the top of a
// whistle, each held long enough to measure.
static const float response_tones[] = {
  41, 55, 82, 110, 165, 220, 330, 440, 880, 1760, 3520,
};
#define N_RESPONSE_TONES \
  ((int)(sizeof(response_tones)/sizeof(response_tones[0])))

static float build_response(struct SelfTest* t) {
  add_event(t, 0, 0, 0, 0.8f);
  for (int i = 0; i < N_RESPONSE_TONES; i++) {
    add_event(t, response_tones[i], STIMULUS_AMP, 0, 0.9f);
    add_event(t, 0, 0, 0, 0.35f);
  }
  return 0;
}

float selftest_init(struct SelfTest* t, float sample_rate,
                    enum SelfTestMode mode) {
  memset(t, 0, sizeof(*t));
  t->sample_rate = sample_rate;
  t->rng = 22222;
  t->mode = mode;

  if (mode == SELFTEST_RECORD || mode == SELFTEST_RESPONSE ||
      mode == SELFTEST_MONITOR) {
    if (mode == SELFTEST_RECORD) {
      build_record(t);
    } else if (mode == SELFTEST_MONITOR) {
      build_monitor(t);
    } else {
      build_response(t);
    }
    long long total = 0;
    for (int i = 0; i < t->event_count; i++) {
      total += t->events[i].samples;
    }
    t->capacity = total;
    t->recording = calloc((size_t)total * 3, sizeof(float));
    return t->recording ? (float)total / sample_rate : -1;
  }

  // A moment of nothing, so the recording starts with the room's own noise
  // floor to measure against.
  add_event(t, 0, 0, 0, 1.0f);

  for (int n = 0; n < N_NOISE_LEVELS; n++) {
    float noise = noise_levels[n] * STIMULUS_AMP;

    // Silence at this noise level, first: nothing here should trigger a note.
    add_event(t, 0, 0, noise, 1.0f);

    // A sustained note.
    add_event(t, semitone(0), STIMULUS_AMP, noise, 2.0f);
    add_event(t, 0, 0, noise, 0.4f);

    // Staccato, the case that used to garble.
    for (int k = 0; k < 8; k++) {
      add_event(t, semitone((k % 4) * 2), STIMULUS_AMP, noise, 0.15f);
      add_event(t, 0, 0, noise, 0.08f);
    }
    add_event(t, 0, 0, noise, 0.3f);

    // A short run, legato, the shape most of a contra tune is made of.
    static const float run[] = { 0, 2, 4, 5, 7, 5, 4, 2 };
    for (int k = 0; k < 8; k++) {
      add_event(t, semitone(run[k]), STIMULUS_AMP, noise, 0.18f);
    }
    add_event(t, 0, 0, noise, 0.4f);

    // A quiet note: dynamics have to survive the noise too.
    add_event(t, semitone(-3), STIMULUS_AMP * 0.35f, noise, 1.2f);
    add_event(t, 0, 0, noise, 0.6f);
  }

  long long total = 0;
  for (int i = 0; i < t->event_count; i++) {
    total += t->events[i].samples;
  }
  t->capacity = total;
  t->recording = calloc((size_t)total * 3, sizeof(float));
  if (!t->recording) {
    return -1;
  }
  return (float)total / sample_rate;
}

void selftest_free(struct SelfTest* t) {
  free(t->recording);
  t->recording = NULL;
}

static float white(struct SelfTest* t) {
  // xorshift, so a run is reproducible.
  t->rng ^= t->rng << 13;
  t->rng ^= t->rng >> 17;
  t->rng ^= t->rng << 5;
  return (float)((int)(t->rng & 0xffffff) - 0x800000) / (float)0x800000;
}

float selftest_stimulus(struct SelfTest* t) {
  if (t->event_index >= t->event_count) {
    t->done = true;
    return 0;
  }

  const struct TestEvent* e = &t->events[t->event_index];
  if (t->mode == SELFTEST_RECORD || t->mode == SELFTEST_MONITOR) {
    // Play no stimulus.  In RECORD that means silence; in MONITOR the caller
    // sends the synth to the output instead, so the player can hear what
    // they are provoking.
    if (++t->event_pos >= e->samples) {
      t->event_pos = 0;
      t->event_index++;
    }
    return 0;
  }
  float out = 0;

  if (e->freq > 0) {
    // A whistle is close to a sine with a weak second harmonic, and nobody
    // holds one perfectly steady, so give it a little vibrato.
    t->vibrato_phase += 5.0f / t->sample_rate;
    if (t->vibrato_phase >= 1) {
      t->vibrato_phase -= 1;
    }
    float cents = 10.0f * sinf(2 * (float)M_PI * t->vibrato_phase);
    float freq = e->freq * powf(2.0f, cents / 1200.0f);

    t->phase += freq / t->sample_rate;
    if (t->phase >= 1) {
      t->phase -= 1;
    }
    float theta = 2 * (float)M_PI * t->phase;

    // Ramp the ends so the played note doesn't click -- we want to test the
    // engine, not the loudspeaker.
    float ramp = 1;
    float attack = 0.010f * t->sample_rate;
    float release = 0.020f * t->sample_rate;
    if (t->event_pos < attack) {
      ramp = t->event_pos / attack;
    }
    float left = e->samples - t->event_pos;
    if (left < release) {
      ramp = fminf(ramp, left / release);
    }

    out = e->amp * ramp * (sinf(theta) + 0.05f * sinf(2 * theta));
  }

  out += e->noise * white(t);

  if (++t->event_pos >= e->samples) {
    t->event_pos = 0;
    t->event_index++;
  }
  return out;
}

void selftest_record(struct SelfTest* t, float stimulus, float input,
                     float synth_output) {
  if (t->recorded >= t->capacity) {
    t->done = true;
    return;
  }
  float mag = input < 0 ? -input : input;
  if (mag > t->input_peak) {
    t->input_peak = mag;
  }

  float* frame = &t->recording[t->recorded * 3];
  frame[0] = stimulus;
  frame[1] = input;
  frame[2] = synth_output;
  t->recorded++;
}

const char* selftest_label(const struct SelfTest* t) {
  if (t->event_index >= t->event_count) {
    return NULL;
  }
  return t->events[t->event_index].label;
}

float selftest_remaining(const struct SelfTest* t) {
  if (t->event_index >= t->event_count) {
    return 0;
  }
  const struct TestEvent* e = &t->events[t->event_index];
  return (e->samples - t->event_pos) / t->sample_rate;
}

float selftest_take_peak(struct SelfTest* t) {
  float peak = t->input_peak;
  t->input_peak = 0;
  return peak;
}

bool selftest_done(const struct SelfTest* t) {
  return t->done || t->recorded >= t->capacity;
}

int selftest_write(struct SelfTest* t, const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    perror("can't open recording file");
    fprintf(stderr, "  wanted: %s\n", path);
    return -1;
  }
  size_t frames = (size_t)t->recorded;

  if (t->mode == SELFTEST_RECORD) {
    // Just the microphone.  The other two channels are silent by
    // construction here, and mono means the file can be fed straight to
    // zeros2-offline without being split first.  MONITOR keeps all three,
    // because there the synth channel is the point.
    for (size_t i = 0; i < frames; i++) {
      if (fwrite(&t->recording[i * 3 + 1], sizeof(float), 1, f) != 1) {
        perror("write");
        fclose(f);
        return -1;
      }
    }
  } else if (fwrite(t->recording, sizeof(float) * 3, frames, f) != frames) {
    perror("write");
    fclose(f);
    return -1;
  }
  fclose(f);

  char sections[1024];
  snprintf(sections, sizeof(sections), "%s.sections", path);
  FILE* s = fopen(sections, "w");
  if (!s) {
    perror("can't open sections file");
    return -1;
  }
  fprintf(s, "start\tend\tfreq\tamp\tnoise\n");
  double at = 0;
  for (int i = 0; i < t->event_count; i++) {
    double dur = t->events[i].samples / (double)t->sample_rate;
    fprintf(s, "%.4f\t%.4f\t%.2f\t%.4f\t%.4f\n", at, at + dur,
            t->events[i].freq, t->events[i].amp, t->events[i].noise);
    at += dur;
  }
  fclose(s);

  printf("\nwrote %.2fs to %s (%s)\n",
         t->recorded / (double)t->sample_rate, path,
         t->mode == SELFTEST_RECORD
           ? "mono microphone, ready for zeros2-offline"
           : "3ch: stimulus, microphone, synth");
  printf("wrote %s\n", sections);
  return 0;
}
