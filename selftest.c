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

// Free playing with the synth audible, for a fault the player can hear and
// provoke but that has not shown up in analysis.  There is nothing to follow
// here -- the prompts only give a take some shape, and ctrl-C keeps whatever
// has been recorded, so the way to use it is to play until the thing happens
// and then stop.  Long enough that it is the player who decides when a take
// is over rather than the script.
static void build_monitor(struct SelfTest* t) {
  add_prompt(t, "play normally -- get used to the sound", 12.0f);
  add_prompt(t, "now provoke it: whatever brings the problem out", 48.0f);
  add_prompt(t, "keep going -- ctrl-C once you have it recorded", 300.0f);
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

// The same idea for a voice that holds.  Everything above was written to find
// out whether the detector tracks what is being played, so it is nearly all
// playing; a holding voice is judged on the gaps, and that script has hardly
// any.  Every section here is a rest away from being one of those.
//
// Three things are asked for, kept apart so a fault can be attributed rather
// than guessed at:
//
//   - notes that start while the last one's tail is still sounding, which is
//     the only place this voice has a discontinuity to click on.  The tail is
//     two seconds, so a 2s rest puts the next note where it starts to release
//     and a 1s rest puts it dead in the middle of the sustain.
//   - long deliberate notes, ended in the several ways a whistle really ends,
//     so what gets held can be compared against what was meant.
//   - playing with no rests in it at all, which should hold nothing.
//
// The pitches are the player's own choice: what a held note should be is the
// note it sat on, and that is in the recording whether or not anyone names it.
static void build_record_hold(struct SelfTest* t) {
  add_prompt(t, "get comfortable -- don't play yet", 3.0f);
  add_prompt(t, "SILENCE: let the room be quiet", 4.0f);
  add_prompt(t, "LEVEL CHECK: hold one steady note, as loud as you'd play",
             6.0f);

  // A note landing on a tail, at the two rest lengths that put it in
  // different parts of one.
  add_prompt(t, "HOLD: a 2s note, then a 2s rest.  Over and over.", 16.0f);
  add_prompt(t, "SHORT REST: the same, but rest only 1s between notes", 12.0f);
  add_prompt(t, "SAME NOTE: 2s on, 2s off, always the same pitch", 12.0f);

  // Whether a click tracks the size of the step says which of the two things
  // that jump at an onset -- the level and the pitch -- is the one being
  // heard.
  add_prompt(t, "LOUD, QUIET: alternate a loud note and a quiet one, 2s rests",
             14.0f);
  add_prompt(t, "LEAPS: 2s notes, 2s rests, big jumps between them", 12.0f);

  // How a note ends, which is the part of it a hold must not listen to.
  add_prompt(t, "TRAIL OFF: 2s notes, let each one fade away, 2s rests", 12.0f);
  add_prompt(t, "SAG: 2s notes, let the pitch sag flat at the very end", 12.0f);
  add_prompt(t, "SCOOP: 2s notes, slide up into each one, 2s rests", 12.0f);

  // Nothing in this one has earned a hold.
  add_prompt(t, "BLIPS: very short notes, 2s rests -- these should NOT hold",
             12.0f);

  // And nothing in these two should hold either, until the phrase stops.
  add_prompt(t, "FAST, NO RESTS: eighth notes, tongued, don't stop", 12.0f);
  add_prompt(t, "FAST, SLURRED: the same run, all in one breath", 10.0f);
  add_prompt(t, "PHRASE: play fast, then stop dead and let the last note hold",
             16.0f);

  add_prompt(t, "REAL TUNE: play like you would on the gig", 20.0f);
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

bool selftest_is_record(enum SelfTestMode mode) {
  return mode == SELFTEST_RECORD || mode == SELFTEST_RECORD_HOLD;
}

float selftest_init(struct SelfTest* t, float sample_rate,
                    enum SelfTestMode mode) {
  memset(t, 0, sizeof(*t));
  t->sample_rate = sample_rate;
  t->rng = 22222;
  t->mode = mode;

  if (selftest_is_record(mode) || mode == SELFTEST_RESPONSE ||
      mode == SELFTEST_MONITOR) {
    if (mode == SELFTEST_RECORD) {
      build_record(t);
    } else if (mode == SELFTEST_RECORD_HOLD) {
      build_record_hold(t);
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
  if (selftest_is_record(t->mode) || t->mode == SELFTEST_MONITOR) {
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

  if (selftest_is_record(t->mode) || t->mode == SELFTEST_MONITOR) {
    // Just the microphone.  Mono, so it can be fed straight to zeros2-offline
    // without being split first -- which is the whole point of keeping a
    // recording: every later render comes from it.  In RECORD the other two
    // channels are silent by construction; in MONITOR the synth is written
    // beside this one, below.
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

  // And what the player actually heard, which is the other half of a demo:
  // it says whether a fault is in the synth or in the room, and it is the
  // thing to compare a re-render against.
  char synth_path[1024];
  if (t->mode == SELFTEST_MONITOR) {
    snprintf(synth_path, sizeof(synth_path), "%s.synth.f32", path);
    FILE* sf = fopen(synth_path, "wb");
    if (!sf) {
      perror("can't open synth file");
      return -1;
    }
    for (size_t i = 0; i < frames; i++) {
      if (fwrite(&t->recording[i * 3 + 2], sizeof(float), 1, sf) != 1) {
        perror("write");
        fclose(sf);
        return -1;
      }
    }
    fclose(sf);
  }

  char sections[1024];
  snprintf(sections, sizeof(sections), "%s.sections", path);
  FILE* s = fopen(sections, "w");
  if (!s) {
    perror("can't open sections file");
    return -1;
  }
  fprintf(s, "start\tend\tfreq\tamp\tnoise\tlabel\n");
  double at = 0;
  double recorded = t->recorded / (double)t->sample_rate;
  for (int i = 0; i < t->event_count; i++) {
    double dur = t->events[i].samples / (double)t->sample_rate;
    // A take stopped with ctrl-C should not claim sections it never reached,
    // or the labels stop lining up with the audio.
    if (at >= recorded) {
      break;
    }
    double end = at + dur < recorded ? at + dur : recorded;
    fprintf(s, "%.4f\t%.4f\t%.2f\t%.4f\t%.4f\t%s\n", at, end,
            t->events[i].freq, t->events[i].amp, t->events[i].noise,
            t->events[i].label ? t->events[i].label : "");
    at += dur;
  }
  fclose(s);

  printf("\nwrote %.2fs to %s (%s)\n",
         t->recorded / (double)t->sample_rate, path,
         selftest_is_record(t->mode) || t->mode == SELFTEST_MONITOR
           ? "mono microphone, ready for zeros2-offline"
           : "3ch: stimulus, microphone, synth");
  if (t->mode == SELFTEST_MONITOR) {
    printf("wrote %s (mono, what you heard)\n", synth_path);
  }
  printf("wrote %s\n", sections);
  return 0;
}
