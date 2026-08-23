// Self-test: plays a synthetic whistle out the speakers, lets it come back in
// through the microphone, runs it through the engine as normal, and records
// everything for analysis afterwards.
//
// The engine's output is deliberately *not* played -- the microphone is
// pointed at the headphone, so anything we play gets heard, and playing the
// synth would put it back into the detector and contaminate the test.  Only
// the stimulus goes out; the synth output goes to the recording.
//
// Noise is added to the stimulus in increasing amounts across the run, so the
// same material is tested at several signal-to-noise ratios.
#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdbool.h>

#define SELFTEST_MAX_EVENTS 256

// What to play.  NOTES exercises the engine; RESPONSE measures what the
// speaker-to-microphone path itself does to each part of the spectrum, which
// is worth knowing before trusting the rig to judge anything by ear.
enum SelfTestMode {
  SELFTEST_NOTES,
  SELFTEST_RESPONSE,
  // Plays nothing and records the microphone, prompting through a scripted
  // set of things to play.  Synthetic whistles only go so far -- glottal
  // stops, how a real note trails off, and what a real breath noise floor
  // looks like are all guesses until there's a recording of the real thing.
  SELFTEST_RECORD,
  // The same, but asking for what a *holding* voice has to be judged on.
  // RECORD's script is about whether the detector tracks, and has almost no
  // rests in it; a voice that carries a note through a rest is judged on the
  // rests, on what it picks to carry, and on leaving alone the playing that
  // has no rests in it at all.  See build_record_hold.
  SELFTEST_RECORD_HOLD,
  // Plays the synth so it can be heard, and records the microphone and the
  // synth output side by side.  For chasing an artifact the player can hear
  // and provoke but that hasn't shown up in analysis: they drive it, and the
  // recording captures the synth digitally rather than through the air.
  SELFTEST_MONITOR,
};

struct TestEvent {
  float freq;     // Hz, or 0 for a rest
  float amp;
  float noise;    // white noise amplitude, added whether or not there's a note
  int samples;
  const char* label;   // what to tell the player, in record mode
};

struct SelfTest {
  float sample_rate;
  enum SelfTestMode mode;

  struct TestEvent events[SELFTEST_MAX_EVENTS];
  int event_count;
  int event_index;
  int event_pos;

  float phase;
  float vibrato_phase;
  unsigned int rng;

  // Interleaved stimulus / microphone / synth-output, one frame per sample.
  float* recording;
  float input_peak;    // since the last time it was read, for the meter
  long long recorded;
  long long capacity;

  bool done;
};

// Builds the schedule and allocates the recording buffer.  Returns the length
// of the test in seconds, or -1 if allocation failed.
float selftest_init(struct SelfTest* t, float sample_rate,
                    enum SelfTestMode mode);
void selftest_free(struct SelfTest* t);

// Whether a mode plays nothing and only records the player.  The two record
// scripts differ in what they ask for and in nothing else.
bool selftest_is_record(enum SelfTestMode mode);

// Next stimulus sample to play.  Realtime safe.
float selftest_stimulus(struct SelfTest* t);

// Stores one frame.  Realtime safe.
void selftest_record(struct SelfTest* t, float stimulus, float input,
                     float synth_output);

bool selftest_done(const struct SelfTest* t);

// What the player should be doing now, or NULL.  Poll from the main thread.
const char* selftest_label(const struct SelfTest* t);
// Seconds left in the current section.
float selftest_remaining(const struct SelfTest* t);
// Peak input since the last call, then resets.
float selftest_take_peak(struct SelfTest* t);

// Writes the recording as interleaved 3-channel raw float, and a companion
// .sections file describing what was playing when.  Not realtime safe; call
// after the stream has stopped.
int selftest_write(struct SelfTest* t, const char* path);

#endif  // SELFTEST_H
