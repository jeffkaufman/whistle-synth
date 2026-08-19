#include "whistle.h"
#include "whistle_internal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "pitch.h"

/* ---------------------------------------------------------- the engine --- */

// Touched only by the audio thread once a stream is running, and only by the
// main thread when one is not.  Everything the UI wants to change reaches it
// through the published values below.
static struct Engine engine;

/* ------------------------------------------------- UI -> audio handoff --- */

// The voice and its parameters travel together: applying them separately
// would sound one block of the old voice's parameters, or of the new voice's
// defaults, every time the player switches.
struct Program {
  int voice;
  struct SynthParams params;
};

// A ring of slots with a generation counter, rather than a lock.  The writer
// fills the next slot and then publishes it; the reader copies the newest
// slot and re-checks the counter, so it can tell whether the writer lapped it
// mid-copy.  Four slots is far more than the writer can consume in the time
// it takes to copy ~120 bytes -- the retry is there to make that a fact
// rather than an assumption.
#define PROGRAM_SLOTS 4

static struct Program program_slots[PROGRAM_SLOTS];
static _Atomic unsigned program_gen = 1;   // slot (gen % SLOTS) is published

// Audio-thread copies, so the synth reads storage no one else writes.
static unsigned applied_gen;
static struct Program live_program;

static _Atomic int published_volume = 5;
static _Atomic int published_gate = 5;
static int applied_volume = -1;
static int applied_gate = -1;

static bool read_program(struct Program* out) {
  for (int attempt = 0; attempt < 4; attempt++) {
    unsigned gen = atomic_load_explicit(&program_gen, memory_order_acquire);
    if (gen == applied_gen) {
      return false;
    }
    *out = program_slots[gen % PROGRAM_SLOTS];
    // If the writer got PROGRAM_SLOTS ahead of us while we were copying, the
    // slot we read was being overwritten underneath us.  Take the newer one.
    unsigned now = atomic_load_explicit(&program_gen, memory_order_acquire);
    if (now - gen < PROGRAM_SLOTS) {
      applied_gen = gen;
      return true;
    }
  }
  return false;
}

// Once per block, not once per sample.
static void apply_controls(void) {
  struct Program program;
  if (read_program(&program)) {
    live_program = program;
    engine_set_voice(&engine, live_program.voice);
    if (live_program.voice != 0) {
      engine_set_params(&engine, &live_program.params);
    }
  }

  int volume = atomic_load_explicit(&published_volume, memory_order_relaxed);
  if (volume != applied_volume) {
    applied_volume = volume;
    engine_set_volume(&engine, volume);
  }
  int gate = atomic_load_explicit(&published_gate, memory_order_relaxed);
  if (gate != applied_gate) {
    applied_gate = gate;
    engine_set_gate(&engine, gate);
  }
}

void whistle_set_program(int voice, const struct SynthParams* params) {
  unsigned gen = atomic_load_explicit(&program_gen, memory_order_relaxed) + 1;
  struct Program* slot = &program_slots[gen % PROGRAM_SLOTS];

  slot->voice = voice;
  if (voice != 0 && params) {
    slot->params = *params;
  } else {
    whistle_preset_defaults(voice == 0 ? 1 : voice, &slot->params);
  }
  // The UI clamps its sliders, but nothing else guarantees that, and these
  // numbers end up as array bounds and divisors on the audio thread.
  synth_sanitize_params(&slot->params);

  atomic_store_explicit(&program_gen, gen, memory_order_release);
}

void whistle_set_volume(int step) {
  atomic_store_explicit(&published_volume, step, memory_order_relaxed);
}

void whistle_set_gate(int step) {
  atomic_store_explicit(&published_gate, step, memory_order_relaxed);
}

/* --------------------------------------------------------------- names --- */

int whistle_preset_count(void) {
  return synth_preset_count();
}

const char* whistle_voice_name(int voice) {
  return engine_voice_name(voice);
}

void whistle_preset_defaults(int voice, struct SynthParams* out) {
  if (voice >= 1 && voice <= synth_preset_count()) {
    synth_preset_defaults(voice - 1, out);
  }
}

/* -------------------------------------------------------------- meters --- */

static _Atomic int meter_xruns;
static _Atomic int meter_dropouts;
static _Atomic int meter_input_peak_bits;   // float, punned: see peak_max
static _Atomic int meter_output_peak_bits;
static _Atomic int meter_freq_bits;
static _Atomic bool meter_voiced;

// The meters are floats but atomic float support is patchy in C, so they are
// carried as their bit patterns.  Values are all non-negative, where the
// integer ordering of IEEE-754 bits matches the float ordering, so `max` can
// be done on the integers directly.
static int float_bits(float value) {
  int bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static float bits_float(int bits) {
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static void peak_max(_Atomic int* slot, float value) {
  if (!(value > 0)) {
    return;
  }
  int bits = float_bits(value);
  int seen = atomic_load_explicit(slot, memory_order_relaxed);
  while (bits > seen) {
    if (atomic_compare_exchange_weak_explicit(
            slot, &seen, bits, memory_order_relaxed, memory_order_relaxed)) {
      return;
    }
  }
}

static float peak_take(_Atomic int* slot) {
  return bits_float(atomic_exchange_explicit(slot, 0, memory_order_relaxed));
}

void whistle_note_xrun(void) {
  atomic_fetch_add_explicit(&meter_xruns, 1, memory_order_relaxed);
}

void whistle_note_dropouts(int count) {
  atomic_fetch_add_explicit(&meter_dropouts, count, memory_order_relaxed);
}

/* --------------------------------------------------------- stream info --- */

// Written by the main thread with no stream running, read by the UI.  Guarded
// by `info_gen`: odd while being written, so a reader can tell it caught a
// half-updated copy and try again.
static struct {
  _Atomic unsigned gen;
  bool running;
  double sample_rate;
  int buffer_frames;
  double input_latency_ms;
  double output_latency_ms;
  char input_name[WHISTLE_NAME_MAX];
  char output_name[WHISTLE_NAME_MAX];
  bool split_devices;
} stream_info;

void whistle_publish_stream(bool running,
                            double sample_rate,
                            int buffer_frames,
                            double input_latency_ms,
                            double output_latency_ms,
                            const char* input_name,
                            const char* output_name,
                            bool split_devices) {
  atomic_fetch_add_explicit(&stream_info.gen, 1, memory_order_release);

  stream_info.running = running;
  stream_info.sample_rate = sample_rate;
  stream_info.buffer_frames = buffer_frames;
  stream_info.input_latency_ms = input_latency_ms;
  stream_info.output_latency_ms = output_latency_ms;
  stream_info.split_devices = split_devices;
  snprintf(stream_info.input_name, sizeof(stream_info.input_name), "%s",
           input_name ? input_name : "");
  snprintf(stream_info.output_name, sizeof(stream_info.output_name), "%s",
           output_name ? output_name : "");

  atomic_fetch_add_explicit(&stream_info.gen, 1, memory_order_release);
}

void whistle_status(struct WhistleStatus* out) {
  memset(out, 0, sizeof(*out));

  for (int attempt = 0; attempt < 8; attempt++) {
    unsigned before = atomic_load_explicit(&stream_info.gen,
                                           memory_order_acquire);
    if (before & 1) {
      continue;   // mid-write
    }
    out->running = stream_info.running;
    out->sample_rate = stream_info.sample_rate;
    out->buffer_frames = stream_info.buffer_frames;
    out->input_latency_ms = stream_info.input_latency_ms;
    out->output_latency_ms = stream_info.output_latency_ms;
    out->split_devices = stream_info.split_devices;
    memcpy(out->input_name, stream_info.input_name, sizeof(out->input_name));
    memcpy(out->output_name, stream_info.output_name, sizeof(out->output_name));

    if (atomic_load_explicit(&stream_info.gen, memory_order_acquire) == before) {
      break;
    }
  }

  // The analysis window, not buffering: the synth free-runs, so this is how
  // late it hears about a pitch change rather than anything held up on the
  // way through.
  out->detection_lag_ms = out->sample_rate > 0
      ? 500.0 * PITCH_WINDOW / out->sample_rate : 0;

  out->xruns = atomic_load_explicit(&meter_xruns, memory_order_relaxed);
  out->dropouts = atomic_load_explicit(&meter_dropouts, memory_order_relaxed);
  out->input_peak = peak_take(&meter_input_peak_bits);
  out->output_peak = peak_take(&meter_output_peak_bits);
  out->freq = bits_float(atomic_load_explicit(&meter_freq_bits,
                                              memory_order_relaxed));
  out->voiced = atomic_load_explicit(&meter_voiced, memory_order_relaxed);
}

/* ------------------------------------------------------- the audio path --- */

void whistle_engine_prepare(double sample_rate) {
  engine_init(&engine, (float)sample_rate);

  // engine_init resets to its own defaults, so re-apply whatever the UI has
  // already published.  No stream is running, so this is just a direct call.
  applied_gen = 0;
  applied_volume = -1;
  applied_gate = -1;
  apply_controls();

  atomic_store_explicit(&meter_xruns, 0, memory_order_relaxed);
  atomic_store_explicit(&meter_dropouts, 0, memory_order_relaxed);
}

void whistle_engine_process(const float* in, float* left, float* right,
                            int frames) {
  apply_controls();

  float input_peak = 0;
  float output_peak = 0;

  for (int i = 0; i < frames; i++) {
    // Read before writing: `in` is allowed to be the same buffer as `left`.
    float sample = in ? in[i] : 0;
    float magnitude = sample < 0 ? -sample : sample;
    if (magnitude > input_peak) {
      input_peak = magnitude;
    }

    float l, r;
    engine_process_stereo(&engine, sample, &l, &r);
    left[i] = l;
    right[i] = r;

    float loudest = (l < 0 ? -l : l);
    float other = (r < 0 ? -r : r);
    if (other > loudest) {
      loudest = other;
    }
    if (loudest > output_peak) {
      output_peak = loudest;
    }
  }

  peak_max(&meter_input_peak_bits, input_peak);
  peak_max(&meter_output_peak_bits, output_peak);
  atomic_store_explicit(&meter_freq_bits, float_bits(engine.detector.hint.freq),
                        memory_order_relaxed);
  atomic_store_explicit(&meter_voiced, engine.detector.hint.voiced,
                        memory_order_relaxed);
}
