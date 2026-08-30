#include "pitch.h"

#include <math.h>
#include <string.h>

// YIN, de Cheveigne & Kawahara 2002.  Autocorrelation finds a peak wherever
// the signal repeats, including at every multiple of the true period, which
// is where naive pitch detectors get their octave errors.  YIN instead
// minimizes a difference function and normalizes it by its own running mean,
// which biases the answer towards the *shortest* period that explains the
// signal, and hands back a number that doubles as a confidence.

// A difference below this counts as periodic enough to stop looking.
#define YIN_THRESHOLD 0.15f

// Confidence needed to start a note, and to keep one going.  Starting is the
// stricter test: a false start is audible, a slightly late release is not.
// 0.80 rather than something lower because a whistle is extremely periodic:
// measured over a real take, 95% of frames while playing sit above 0.95 and
// the median is 0.99, while the room and the player's own breath between
// notes sit at 0.02 median.  Raising the bar from 0.55 to 0.80 cut false
// triggers over a 104s take from 13 to 3 and cost 3 notes out of 184.  It is
// free in noise -- over a 39dB-to-10dB SNR sweep the count is unchanged at
// 70/72 either way, because there ON_HOPS is what binds: a real note holds
// its confidence for hundreds of hops even when buried, so the frames that
// dip below are at its edges rather than throughout.
#define CONF_ON 0.80f
#define CONF_OFF 0.30f

// Confidence needed before we'll move the reported frequency at all.  Below
// this we hold the last good value, which is what keeps a breathy moment
// mid-note from throwing the synth somewhere random.
#define CONF_TRUST 0.40f

// Consecutive analysis hops that have to agree before we change our mind.
//
// ON_HOPS is what separates a note from a room transient.  A real note holds
// its confidence for hundreds of hops; a chair creak or a thump manages a
// couple, and in a quiet room those can look surprisingly periodic.  Time is
// the cheap discriminator: raising the confidence bar instead would cost
// notes in a noisy room, which is where this has to work.
//
// 8 hops costs 10.7ms before a note starts and takes false triggers over a
// real 104s take from 3 to 0.  It does *not* fix onset pitch accuracy: about
// 10% of onsets still land over 150 cents from where the note settles,
// because the analysis window at that moment is still mostly whatever came
// before the note.  Going to 10 hops only got that from 15 to 11 and is not
// worth the timing.
//
// Releasing is slower still, to ride out the moment at the end of a note
// where a whistle goes breathy before it stops.
#define ON_HOPS 8
#define OFF_HOPS 8

// Below this confidence we assume an exact 2x error is likelier than a real
// octave leap.  A clean leap reads well above it.
//
// 0.75 was too low, and the failure it let through is worth describing
// because it is not the obvious one.  A whistle does not stop, it decays, and
// on the way down the estimator's confidence decays with it -- but not
// monotonically, and it passes through the high 0.70s while the signal is
// already 4 to 27dB under the note.  An exact halving arriving right there
// reads as confident enough to be a leap.  Measured over four real takes,
// every single disagreement between 0.75 and 0.90 -- 22 of them in 187
// seconds -- was an exact 2:1 error at low level in a burst of 1 to 32ms, and
// none was a real leap: the deliberate-leap passage covers the same
// 1072-2198Hz either way.
//
//    guard   holding   scoop-up   up-down   whistling
//    0.75       39         0          5         26      octave glitches
//    0.85       17         0          2         17
//    0.90        3         0          0          4
//    0.95        3         0          0          2
//
// 0.90 takes nearly all of it and keeps a real escape hatch; past that the
// curve is flat and all that is left is giving up on leaps.
//
// Most voices ride out a 30ms octave blip -- it is gone before the ear has
// resolved it.  `reese-hold` is the one that cannot: it can be the last thing
// it hears before the player stops, and then it holds it for six seconds.
#define OCTAVE_GUARD_CONF 0.90f

// The guard only starts protecting a note once it has been going this long.
// At an onset the window is still mostly whatever preceded the note, so the
// first estimate is the *least* reliable one -- and the guard, which keeps
// the pitch where it already is, would hold an onset octave error in place
// instead of letting it correct.  That is what turns a slip into an audible
// swoop: the note starts an octave out and crawls back over ~120ms.
#define OCTAVE_GUARD_AFTER_HOPS 8

// How fast the noise floor follows the input, per analysis hop.  Down in a
// few tens of milliseconds, so a loud passage doesn't keep the bar raised
// after it ends; up over a few seconds, so it takes a sustained change to
// move it.
#define FLOOR_FALL 0.03f
#define FLOOR_RISE 0.0005f

// Digital silence would otherwise drive the threshold to zero and make
// anything at all look like a note.
#define FLOOR_MIN 1e-5f

// The window that goes with a search out to `max_period`: enough signal left
// behind the longest lag to compare it against.  Four times the longest lag
// keeps three periods of it inside the comparison, which is the ratio the
// fixed 384/96 pair had and what the detector was tuned at.
static int window_for(int max_period) {
  int want = 4 * max_period;
  // A whole number of hops, so the fill logic below stays exact.
  want = (want + PITCH_HOP - 1) / PITCH_HOP * PITCH_HOP;
  if (want < PITCH_WINDOW_MIN) {
    want = PITCH_WINDOW_MIN;
  }
  if (want > PITCH_WINDOW) {
    want = PITCH_WINDOW;
  }
  return want;
}

// Points the search at everything down to `min_hz`, and sizes the window for
// it.  `fill` is clamped rather than cleared: a shorter window must not be
// left holding more samples than it has room to analyze, and a longer one is
// simply short of a full window for a moment, which is the same state it is
// in at startup.
static void set_search_floor(struct PitchDetector* d, float min_hz) {
  int period = min_hz > 0 ? (int)(d->sample_rate / min_hz) : d->period_cap;
  if (period > d->period_cap) {
    period = d->period_cap;
  }
  if (period < d->min_period + 1) {
    period = d->min_period + 1;
  }
  d->max_period = period;
  d->window_len = window_for(period);
  if (d->fill > d->window_len - PITCH_HOP) {
    d->fill = d->window_len - PITCH_HOP;
  }
}

void pitch_init(struct PitchDetector* d, float sample_rate,
                float min_hz, float max_hz) {
  memset(d, 0, sizeof(*d));
  d->sample_rate = sample_rate;

  d->min_period = (int)(sample_rate / max_hz);
  if (d->min_period < 2) {
    d->min_period = 2;
  }
  d->period_cap = (int)(sample_rate / min_hz);
  if (d->period_cap > PITCH_MAX_PERIOD) {
    d->period_cap = PITCH_MAX_PERIOD;
  }
  // Everything it may ever be asked for, until it is asked for less.
  set_search_floor(d, min_hz);

  pitch_set_gate(d, 3.0f);
  // Start pessimistic and let it fall to the real floor within a second or
  // so, rather than starting at zero and false-triggering on the way up.
  d->noise_floor = 0.01f;
  d->hint.freq = 1000;
}

void pitch_set_gate(struct PitchDetector* d, float margin) {
  d->gate_margin = fmaxf(1.5f, margin);
}

void pitch_set_trigger_range(struct PitchDetector* d,
                             float min_hz, float max_hz) {
  // A range with nothing in it would be a detector that can never trigger,
  // which is indistinguishable from a broken one; an inverted pair is taken
  // as no limit rather than as silence.
  if (min_hz > 0 && max_hz > 0 && min_hz >= max_hz) {
    min_hz = 0;
    max_hz = 0;
  }
  d->trigger_min_hz = min_hz > 0 ? min_hz : 0;
  d->trigger_max_hz = max_hz > 0 ? max_hz : 0;
  // The bottom of the range is the bottom of the search: there is no point
  // paying for lags the player has said they will not use, and the price is
  // paid in latency rather than only in cycles.  The top is deliberately not
  // touched -- see the header.
  set_search_floor(d, d->trigger_min_hz);
}

static float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

// Runs YIN over the analysis window.  Returns the estimated frequency and
// writes 0..1 confidence to out_confidence.
static float estimate(struct PitchDetector* d, float* out_confidence) {
  const float* x = d->window;
  const int tau_max = d->max_period;
  // Lags are compared over whatever is left of the window once the longest
  // lag is accounted for.  The window is sized for the search, so this stays
  // around three times tau_max whatever the range is.
  const int compared = d->window_len - tau_max;

  float diff[PITCH_MAX_PERIOD + 1];
  float normalized[PITCH_MAX_PERIOD + 1];

  normalized[0] = 1;
  float running = 0;
  for (int tau = 1; tau <= tau_max; tau++) {
    float sum = 0;
    for (int j = 0; j < compared; j++) {
      float delta = x[j] - x[j + tau];
      sum += delta * delta;
    }
    diff[tau] = sum;
    running += sum;
    // The cumulative mean normalization: how much better this lag explains
    // the signal than every shorter lag did on average.
    normalized[tau] = running > 0 ? sum * tau / running : 1;
  }

  // First lag good enough to accept, descending into its local minimum.
  // Taking the *first* rather than the best is deliberate -- it's what keeps
  // us off the octave below.
  int best = -1;
  for (int tau = d->min_period; tau <= tau_max; tau++) {
    if (normalized[tau] < YIN_THRESHOLD) {
      while (tau + 1 <= tau_max && normalized[tau + 1] < normalized[tau]) {
        tau++;
      }
      best = tau;
      break;
    }
  }
  if (best < 0) {
    // Nothing cleared the bar; report the best we saw and let the low
    // confidence that comes with it speak for itself.
    best = d->min_period;
    for (int tau = d->min_period + 1; tau <= tau_max; tau++) {
      if (normalized[tau] < normalized[best]) {
        best = tau;
      }
    }
  }

  // The true minimum rarely lands on a whole sample.  At 3kHz a period is 16
  // samples, so rounding to one costs ~50 cents; fitting a parabola to the
  // neighbours gets that back.
  float period = best;
  if (best > d->min_period && best < tau_max) {
    float a = normalized[best - 1];
    float b = normalized[best];
    float c = normalized[best + 1];
    float denom = a - 2 * b + c;
    if (denom > 1e-12f) {
      period += 0.5f * (a - c) / denom;
    }
  }

  *out_confidence = fmaxf(0, fminf(1, 1 - normalized[best]));
  return d->sample_rate / period;
}

// Rejects an estimate about an octave from where we already are, unless it
// is confident enough to be a real leap.
//
// Measured against a real take, this earns its keep and the obvious
// alternatives do not.  Removing it entirely doubles octave glitches (36 to
// 72) and makes onsets settle *slower*, not faster.  Replacing it with
// "accept the jump once it has been said twice" lands in between (58) for no
// benefit -- it does not unblock anything, because the guard was never
// blocking real leaps: over a passage of deliberate leaps, with and without
// it, the pitch covers the same 559-2011Hz and passes the same 1934-cent
// jump.  Low confidence is rare enough during a real leap that the escape
// hatch above is sufficient.
static float snap_octave(float candidate, float held, float confidence) {
  if (held <= 0 || confidence >= OCTAVE_GUARD_CONF) {
    return candidate;
  }
  const float tolerance = 0.04f;  // ~70 cents
  if (fabsf(candidate / (held * 2) - 1) < tolerance ||
      fabsf(candidate / (held * 0.5f) - 1) < tolerance) {
    return held;
  }
  return candidate;
}

static void analyze(struct PitchDetector* d) {
  struct PitchHint* h = &d->hint;
  h->onset = false;

  double sumsq = 0;
  for (int i = 0; i < d->window_len; i++) {
    sumsq += d->window[i] * (double)d->window[i];
  }
  h->level = sqrtf(sumsq / d->window_len);

  float confidence;
  float freq = estimate(d, &confidence);
  h->confidence = confidence;

  // A median of the last three readings costs two hops of lag and throws out
  // the isolated wild estimate that a single bad window produces.
  d->recent[2] = d->recent[1];
  d->recent[1] = d->recent[0];
  d->recent[0] = freq;
  if (d->recent_count < 3) {
    d->recent_count++;
  }
  float smoothed = d->recent_count == 3
    ? median3(d->recent[0], d->recent[1], d->recent[2])
    : freq;

  if (confidence >= CONF_TRUST) {
    bool settled = h->voiced && d->voiced_hops >= OCTAVE_GUARD_AFTER_HOPS;
    h->freq = settled ? snap_octave(smoothed, h->freq, confidence) : smoothed;
  }
  // Otherwise h->freq keeps its last trustworthy value.

  // Track the room only when what we're hearing doesn't look like a note --
  // not voiced, and not periodic either.  Going by level alone would let a
  // note that fades in slowly pull the floor up behind it and never trigger,
  // which is exactly what a whistle does at the start of a phrase.
  // Periodicity is the right test because it doesn't care how loud it is.
  if (!h->voiced && confidence < CONF_ON) {
    float rate = h->level < d->noise_floor ? FLOOR_FALL : FLOOR_RISE;
    d->noise_floor += rate * (h->level - d->noise_floor);
  }
  d->noise_floor = fmaxf(d->noise_floor, FLOOR_MIN);

  float threshold = d->noise_floor * d->gate_margin;
  bool loud_enough = h->level >= (h->voiced ? threshold * 0.5f : threshold);
  // In the player's range, tested on the smoothed estimate rather than on
  // h->freq: h->freq holds its last trustworthy value, so a note that walked
  // out of range would be kept alive by the reading it used to have.
  bool in_range =
      (d->trigger_min_hz <= 0 || smoothed >= d->trigger_min_hz) &&
      (d->trigger_max_hz <= 0 || smoothed <= d->trigger_max_hz);
  // Folded in with the confidence rather than tested on its own, so that
  // leaving the range ends a note exactly the way losing the pitch does:
  // after OFF_HOPS, not on the first hop that says so.
  bool clear_enough = confidence >= (h->voiced ? CONF_OFF : CONF_ON) &&
                      in_range;

  if (h->voiced) {
    d->voiced_hops++;
  } else {
    d->voiced_hops = 0;
  }

  if (!h->voiced) {
    if (loud_enough && clear_enough) {
      if (++d->on_hops >= ON_HOPS) {
        h->voiced = true;
        h->onset = true;
        d->off_hops = 0;
        // Start the note where it actually is rather than gliding up from
        // wherever the last one ended.
        h->freq = smoothed;
      }
    } else {
      d->on_hops = 0;
    }
  } else {
    if (!loud_enough || !clear_enough) {
      if (++d->off_hops >= OFF_HOPS) {
        h->voiced = false;
        d->on_hops = 0;
      }
    } else {
      d->off_hops = 0;
    }
  }
}

const struct PitchHint* pitch_process(struct PitchDetector* d, float sample) {
  d->window[d->fill++] = sample;
  if (d->fill >= d->window_len) {
    analyze(d);
    memmove(d->window, d->window + PITCH_HOP,
            (d->window_len - PITCH_HOP) * sizeof(float));
    d->fill = d->window_len - PITCH_HOP;
  } else {
    d->hint.onset = false;
  }
  return &d->hint;
}
