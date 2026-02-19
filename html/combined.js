function clip(v) {
  return Math.max(-1, Math.min(1, v));
}

function polyblep(phase, dt) {
  if (phase < dt) {
    var t = phase / dt;
    return t + t - t * t - 1;
  } else if (phase > 1 - dt) {
    var t = (phase - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

var HISTORY_LENGTH = 8192;
var RECENT_LENGTH = 256;
var GATE_SQUARED = 0.01 * 0.01;
var RECENT_GATE_SQUARED = 40 * 40 * GATE_SQUARED;
var ONSET_RAMP_SAMPLES = 132; // ~3ms at 44100Hz, just enough to prevent clicks

class SupersawSynth {
  constructor(config) {
    this.config = config;

    // History buffer for gate and pitch detection
    this.hist = new Float32Array(HISTORY_LENGTH);
    this.hist_pos = 0;
    this.hist_sq = 0;
    this.recent_hist_sq = 0;

    // Zero-crossing pitch detection
    this.samples_since_last_crossing = 0;
    this.positive = true;
    this.previous_sample = 0;
    this.detected_period = 0;
    this.smoothed_period = 0;
    this.ticks = 0;

    // Amplitude envelope
    this.smoothed_amp = 0;
    this.onset_ramp = 0; // counts up from 0 to ONSET_RAMP_SAMPLES on gate open

    // Phase accumulators for 7 saw voices
    this.phases = new Float64Array(7);

    // Gate state
    this.gated = true;
    this.was_gated = true;
  }

  setHist(s) {
    this.hist_sq += s * s;
    this.hist_sq -= this.hist[this.hist_pos] * this.hist[this.hist_pos];

    this.recent_hist_sq += s * s;
    var recent_pos = (this.hist_pos - RECENT_LENGTH + HISTORY_LENGTH) % HISTORY_LENGTH;
    this.recent_hist_sq -= this.hist[recent_pos] * this.hist[recent_pos];

    this.hist[this.hist_pos] = s;
    this.hist_pos = (this.hist_pos + 1) % HISTORY_LENGTH;
  }

  histSquaredSum() {
    var s = 0;
    for (var i = 0; i < HISTORY_LENGTH; i++) {
      s += this.hist[i] * this.hist[i];
    }
    return s;
  }

  recentHistSquaredSum() {
    var s = 0;
    for (var i = HISTORY_LENGTH - RECENT_LENGTH; i < HISTORY_LENGTH; i++) {
      s += this.hist[i] * this.hist[i];
    }
    return s;
  }

  update(s) {
    this.setHist(s);

    this.ticks++;
    if (this.ticks % 441000 === 0) {
      this.hist_sq = this.histSquaredSum();
    }
    if (this.hist_pos === HISTORY_LENGTH - 1) {
      this.recent_hist_sq = this.recentHistSquaredSum();
    }

    // Zero-crossing pitch detection
    this.samples_since_last_crossing++;

    if (this.positive) {
      if (s < 0) {
        var first_negative = s;
        var last_positive = this.previous_sample;
        var adjustment = first_negative / (last_positive - first_negative);
        if (Number.isNaN(adjustment)) {
          adjustment = 0;
        }
        this.samples_since_last_crossing -= adjustment;
        var period = this.samples_since_last_crossing;

        // Only accept periods in the whistle range
        if (period > this.config.rangeHigh &&
            period < this.config.rangeLow) {
          this.detected_period = period;
          if (this.smoothed_period === 0) {
            this.smoothed_period = period;
          }
        }

        this.positive = false;
        this.samples_since_last_crossing = -adjustment;
      }
    } else {
      if (s > 0) {
        this.positive = true;
      }
    }
    this.previous_sample = s;

    // Smooth pitch tracking
    if (this.smoothed_period > 0 && this.detected_period > 0) {
      this.smoothed_period +=
        this.config.pitch_smooth * (this.detected_period - this.smoothed_period);
    }

    // Noise gate
    var gate_sq = this.config.gate_squared || 1;
    this.was_gated = this.gated;
    this.gated =
      (this.hist_sq / HISTORY_LENGTH < GATE_SQUARED * gate_sq) &&
      (this.recent_hist_sq / RECENT_LENGTH < RECENT_GATE_SQUARED * gate_sq);

    // Amplitude tracking
    var input_rms = Math.sqrt(
      Math.max(0, this.recent_hist_sq) / RECENT_LENGTH);
    var target_amp = this.gated ? 0 : input_rms;

    if (this.was_gated && !this.gated) {
      // Gate just opened: snap amplitude to input level, start anti-click ramp
      this.smoothed_amp = input_rms;
      this.onset_ramp = 0;
    } else if (target_amp > this.smoothed_amp) {
      this.smoothed_amp +=
        this.config.attack_coeff * (target_amp - this.smoothed_amp);
    } else {
      this.smoothed_amp +=
        this.config.release_coeff * (target_amp - this.smoothed_amp);
    }

    // Anti-click ramp on note onset
    var onset_scale = 1;
    if (this.onset_ramp < ONSET_RAMP_SAMPLES) {
      onset_scale = this.onset_ramp / ONSET_RAMP_SAMPLES;
      this.onset_ramp++;
    }

    // If we don't have a valid pitch yet, output nothing
    if (this.smoothed_period <= 0) {
      return 0;
    }

    // Supersaw synthesis
    var octave_divisor = Math.pow(2, this.config.octave_shift);
    var center_period = this.smoothed_period * octave_divisor;
    var detune_cents = this.config.detune_cents;
    var num_voices = this.config.num_voices;
    var half = (num_voices - 1) / 2;

    // Voice weights: center is loudest, outer voices quieter
    var WEIGHTS = [0.3, 0.5, 0.7, 1.0, 0.7, 0.5, 0.3];
    // Index into weights centered at index 3
    var weight_offset = 3 - half;

    var val = 0;
    var weight_sum = 0;

    for (var i = 0; i < num_voices; i++) {
      var offset = i - half; // e.g. -3, -2, -1, 0, +1, +2, +3 for 7 voices
      var voice_period = center_period;
      if (half > 0) {
        voice_period = center_period / Math.pow(2, offset * detune_cents / (half * 1200));
      }
      var dt = 1 / voice_period; // phase increment per sample

      this.phases[i] += dt;
      this.phases[i] -= Math.floor(this.phases[i]);

      var saw = 2 * this.phases[i] - 1 - polyblep(this.phases[i], dt);
      var w = WEIGHTS[i + weight_offset];
      val += saw * w;
      weight_sum += w;
    }

    // Normalize
    if (weight_sum > 0) {
      val /= weight_sum;
    }

    // Scale by amplitude envelope and onset ramp
    val *= this.smoothed_amp * onset_scale;

    return val;
  }
}

class Synth extends AudioWorkletProcessor {
  constructor() {
    super();
    this.config = null;
    this.synth = null;
    this.port.onmessage = (event) => {
      var data = event.data;

      // Convert frequency range to period range (in samples)
      data.rangeLow = sampleRate / data.rangeLow;
      data.rangeHigh = sampleRate / data.rangeHigh;

      // Convert attack/release from ms to per-sample coefficients
      var attack_samples = (data.attack_ms / 1000) * sampleRate;
      var release_samples = (data.release_ms / 1000) * sampleRate;
      data.attack_coeff = attack_samples > 0 ?
        1 - Math.exp(-1 / attack_samples) : 1;
      data.release_coeff = release_samples > 0 ?
        1 - Math.exp(-1 / release_samples) : 1;

      if (this.synth) {
        // Update config without resetting state
        this.synth.config = data;
      } else {
        this.synth = new SupersawSynth(data);
      }
      this.config = data;
    };
    this.port.postMessage("ready");
  }

  process(inputs, outputs, parameters) {
    if (!inputs || !inputs[0] || !inputs[0][0]) return true;
    if (!this.config || !this.synth) return true;

    for (var i = 0; i < outputs[0][0].length; i++) {
      var val = this.synth.update(inputs[0][0][i]);
      val *= this.config.volume;
      val = clip(val);

      for (var j = 0; j < outputs.length; j++) {
        for (var k = 0; k < outputs[0].length; k++) {
          outputs[j][k][i] = val;
        }
      }
    }
    return true;
  }
}

registerProcessor('synth', Synth);
