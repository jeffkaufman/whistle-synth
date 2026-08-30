// Pitch detection: turns an input signal into hints about what the player is
// doing.  Knows nothing about synthesis.
#ifndef PITCH_H
#define PITCH_H

#include <stdbool.h>

// How often we re-analyze, in samples.
#define PITCH_HOP 64

// The analysis window, in samples -- and the reason there are three numbers
// here rather than one.
//
// The window bounds the lowest pitch that can be found (a lag can only be
// measured against what is left of the window behind it) and it sets the
// detection lag: a pitch change is visible somewhere around the middle of the
// window, so about window/2 samples after it happens.  Those pull opposite
// ways, and how far apart they pull depends entirely on how low the player
// has asked to go.  So the window is *chosen*, per range, rather than fixed:
// see `window_len` and pitch_set_trigger_range.
//
// PITCH_WINDOW is therefore a capacity rather than a length -- the longest
// window that can ever be asked for, which is the lowest playable note at the
// highest sample rate offered: 4 x PITCH_MAX_PERIOD, rounded to a whole
// number of hops.  PITCH_WINDOW_MIN is the shortest, and is what the ordinary
// whistle range uses: at 48kHz it is 8ms of signal and a ~4ms detection lag,
// which is the price of knowing the pitch at all.
//
// None of it is added to the audio path.  The synth free-runs, so nothing is
// delayed; it just learns about changes that late.
#define PITCH_WINDOW 2304
#define PITCH_WINDOW_MIN 384

// Longest period that can be searched for, in samples.  576 is F3 (174.6Hz,
// the lowest note anyone has been recorded whistling) at 96kHz, which is the
// highest rate the app offers; at 48kHz the same note is 275 samples.  It is
// a capacity too: what is actually searched is `max_period`, which follows
// the range the player asked for, because searching lower than they asked
// costs both analysis time and window -- which is to say, latency.
#define PITCH_MAX_PERIOD 576

// What the detector believes the player is doing.
//
// These are hints, not measurements.  Both "what pitch is this" and "is the
// player playing at all" are guesses, and they go wrong at exactly the moments
// that matter most -- the start and end of a note, where the signal is weak
// and not yet periodic.  The synth is expected to treat them as advice, and
// `confidence` is how much of it to take.
struct PitchHint {
  bool voiced;       // committed guess that a note is sounding
  bool onset;        // set on the single hint that begins a note
  float freq;        // Hz.  Holds its last trustworthy value while confidence
                     // is low, so it is always safe to read.
  float confidence;  // 0..1
  float level;       // RMS of the analysis window, in input units
};

struct PitchDetector {
  float sample_rate;
  // The search, in samples of lag.  min_period is fixed at the highest
  // playable note and never narrows -- see pitch_set_trigger_range for why
  // that end has to stay wide -- while max_period follows the bottom of the
  // player's range, down to period_cap.
  int min_period, max_period;
  int period_cap;
  // The analysis window actually in use, PITCH_WINDOW_MIN..PITCH_WINDOW,
  // sized to whatever max_period currently is.  Everything reads this rather
  // than PITCH_WINDOW, which is only how much room there is for it.
  int window_len;

  // The player's own range: the lowest and highest notes that count as notes,
  // in Hz, or 0 for no limit at that end.  Distinct from min_period and
  // max_period, which are the *search* range, and deliberately so -- narrowing
  // the search does not stop a whistle above the top from being heard, it
  // makes it come out an octave down, because the only period left to find is
  // the subharmonic.  So the search stays wide and this vetoes the answer:
  // out here, a whistle is heard clearly, understood correctly, and refused.
  float trigger_min_hz, trigger_max_hz;

  // A note has to stand clear of the room by this factor.  Relative rather
  // than absolute because how hot the microphone runs depends on the mic, the
  // preamp, and how close the player is -- none of which we know, and an
  // absolute threshold that suits one rig silently swallows notes on another.
  // It takes more margin to start a note than to keep one, so a note that
  // sags in the middle doesn't chatter on and off.
  float gate_margin;

  // The room's own level, tracked only while nothing is being played.  Falls
  // fast, so it settles on the quietest thing it has heard; rises slowly, so
  // a noisy stretch raises the bar but one loud moment doesn't.
  float noise_floor;

  float window[PITCH_WINDOW];
  int fill;

  float recent[3];    // last three raw estimates, for a median
  int recent_count;

  int on_hops, off_hops;
  int voiced_hops;   // analysis hops since this note started


  struct PitchHint hint;
};

// min_hz and max_hz are the *widest* the detector may later be asked for --
// the ends of the instrument rather than of any one tune.  min_hz must be no
// lower than sample_rate/PITCH_MAX_PERIOD; above that it is silently the
// lowest the rate allows.  The detector starts out searching all of it, which
// is the honest reading of "no range has been set"; pitch_set_trigger_range
// is what narrows the work down to a range someone actually plays in.
void pitch_init(struct PitchDetector* d, float sample_rate,
                float min_hz, float max_hz);

// How far above the room a note has to be to count.  1 would be the noise
// floor itself; useful values are a few times that.
void pitch_set_gate(struct PitchDetector* d, float margin);

// The range of pitches that may start or continue a note.  Either end may be
// 0 for "no limit there", which is what pitch_init sets.  Outside it the
// detector still hears and still tracks -- `freq` and `level` are as they
// were -- but it will not commit to a note, and it comes and goes on the same
// hysteresis as the gate rather than chattering at the boundary.
//
// A whistle out of range is *not* counted as room noise, because the floor is
// tracked on periodicity rather than level: a refused note is still plainly
// periodic, so it cannot pull the threshold up behind it and gate the notes
// that were in range.
//
// The *bottom* of the range also moves the search, and the window with it.
// Nothing else can: finding a 175Hz note at 48kHz means comparing lags out to
// 275 samples, which needs a window three times that to compare them against,
// which is 24ms of signal and a 12ms detection lag -- three times what the
// ordinary whistle range costs.  That is a real price and it is paid only by
// the player who asks for those notes; ask for the usual range and the
// detector does exactly what it has always done.
//
// The *top* of the range does not move the search, and must not.  Refusing a
// high note by not looking for it does not make it inaudible, it makes it
// come out an octave down -- with the true period excluded, the first lag
// that explains the signal is the subharmonic, and if that lands inside the
// range it is played as a note the player did not whistle.  So the search
// always reaches the highest playable note, and the veto above is what turns
// a real reading into no note.
//
// Realtime safe: it recomputes nothing that allocates, so it may be called
// from the audio thread.  Changing the window mid-note costs one analysis
// window of settling and nothing else.
void pitch_set_trigger_range(struct PitchDetector* d,
                             float min_hz, float max_hz);

// Feeds one input sample.  Returns the current hint, which is re-derived
// every PITCH_HOP samples and held in between.  Realtime safe.
const struct PitchHint* pitch_process(struct PitchDetector* d, float sample);

#endif  // PITCH_H
