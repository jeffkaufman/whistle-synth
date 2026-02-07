function sine_decimal(v) {
  return Math.sin((v + 0.5) * Math.PI * 2);
}

function clip(v) {
  return Math.max(-1, Math.min(1, v));
}

function atan_decimal(v) {
  return Math.atan(v) / (Math.PI / 2);
}

function saturate(v, useSaturation) {
  if (!useSaturation) {
    return clip(v);
  }
  var c = sine_decimal(atan_decimal(v * 4));
  v += c;
  v += c * c;
  v += c * c * c * c;
  v += c * c * c * c * c * c * c;
  v -= 0.5;
  v *= 0.55;
  return atan_decimal(v / 4);
}

var HISTORY_LENGTH = 8192;
var RECENT_LENGTH = 256;
var GATE_SQUARED = 0.01 * 0.01;
var RECENT_GATE_SQUARED = 40 * 40 * GATE_SQUARED;

class Osc {
  constructor(adjustment, oscConfig, cycles, rough_input_period) {
    this.amp = 0;
    this.pos = -adjustment;
    this.speed = oscConfig.speed;
    this.mode = oscConfig.fn;  // 'nat', 'sqr', 'sin'
    this.vol = oscConfig.volume;
    this.mute = oscConfig.mute;
    this.total_amplitude = 0;
    this.samples = 0;
    this.rough_input_period = rough_input_period;
    this.duration = oscConfig.duration;

    if (oscConfig.mod === 0) {
      this.polarity = 1;
    } else {
      this.polarity = (Math.trunc(cycles * oscConfig.cycle)) % oscConfig.mod ? 1 : -1;
    }

    this.lfo_pos = 0;
    this.lfo_rate = oscConfig.lfo_rate || 0;
    this.lfo_amplitude = oscConfig.lfo_amplitude || 0;
    this.lfo_is_volume = oscConfig.lfo_is_volume !== undefined ? oscConfig.lfo_is_volume : true;
  }

  next(octaver) {
    this.samples++;

    if (this.mute) {
      this.amp = 0;
      return 0;
    }

    if (this.duration > 0) {
      this.amp += 0.01 * (1 - this.amp);
    } else {
      this.amp *= 0.95;
    }

    var posFloor = Math.floor(this.pos);
    var valA = octaver.getHist(posFloor);
    var valB = octaver.getHist(posFloor + 1);
    var amtA = this.pos - posFloor;
    var amtB = 1 - amtA;

    var val = valA * amtA + valB * amtB;
    this.total_amplitude += Math.abs(val);

    if (this.mode !== 'nat') {
      if (this.mode === 'sqr') {
        val = val > 0 ? 1 : -1;
      } else if (this.mode === 'sin') {
        val = sine_decimal(this.pos / this.rough_input_period);
      }
      val *= (this.total_amplitude / this.samples);
    }

    this.pos += this.speed;
    val = this.amp * val * this.polarity * this.vol;

    if (this.lfo_amplitude > 0) {
      var lfo_amount = (sine_decimal(this.lfo_pos) + 1) * this.lfo_amplitude;
      if (this.lfo_is_volume) {
        val = val * lfo_amount + val * (1 - this.lfo_amplitude);
      } else {
        this.pos += lfo_amount;
      }
      this.lfo_pos += (1 / this.lfo_rate);
    }

    return val;
  }

  markCycle() {
    this.duration--;
  }

  expired() {
    return this.duration < 1 && this.amp < 0.001;
  }
}

class Octaver {
  constructor(config) {
    this.config = config;
    this.oscs = [];
    this.cycles = 0;

    this.hist = new Float32Array(HISTORY_LENGTH);
    this.hist_pos = 0;
    this.hist_sq = 0;
    this.recent_hist_sq = 0;

    this.samples_since_last_crossing = 0;
    this.samples_since_attack_began = 0;
    this.positive = true;
    this.previous_sample = 0;
    this.rough_input_period = 40;
    this.ticks = 0;
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

  getHist(pos) {
    return this.hist[(HISTORY_LENGTH + this.hist_pos - pos) % HISTORY_LENGTH];
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
    if (this.config.raw) {
      return s * this.config.gain;
    }

    this.setHist(s);

    this.ticks++;
    if (this.ticks % 441000 === 0) {
      this.hist_sq = this.histSquaredSum();
    }
    if (this.hist_pos === HISTORY_LENGTH - 1) {
      this.recent_hist_sq = this.recentHistSquaredSum();
    }

    this.samples_since_last_crossing++;
    this.samples_since_attack_began++;

    if (this.positive) {
      if (s < 0) {
        var first_negative = s;
        var last_positive = this.previous_sample;
        var adjustment = first_negative / (last_positive - first_negative);
        if (Number.isNaN(adjustment)) {
          adjustment = 0;
        }
        this.samples_since_last_crossing -= adjustment;
        this.rough_input_period = this.samples_since_last_crossing;

        if (this.rough_input_period > this.config.rangeHigh &&
            this.rough_input_period < this.config.rangeLow) {
          for (var i = 0; i < this.config.oscConfigs.length; i++) {
            this.oscs.push(new Osc(
              adjustment, this.config.oscConfigs[i],
              this.cycles, this.rough_input_period));
          }
        }
        this.cycles++;

        for (var i = this.oscs.length - 1; i >= 0; i--) {
          this.oscs[i].markCycle();
          if (this.oscs[i].expired()) {
            this.oscs.splice(i, 1);
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

    var val = 0;
    for (var i = 0; i < this.oscs.length; i++) {
      val += this.oscs[i].next(this);
    }

    // Noise gate
    var gate_sq = this.config.gate_squared || 1;
    if ((this.hist_sq / HISTORY_LENGTH < GATE_SQUARED * gate_sq) &&
        (this.recent_hist_sq / RECENT_LENGTH < RECENT_GATE_SQUARED * gate_sq)) {
      val = 0;
    }

    return val * this.config.gain;
  }
}

class Synth extends AudioWorkletProcessor {
  constructor() {
    super();
    this.output = 0;
    this.config = null;
    this.octaver = null;
    this.port.onmessage = (event) => {
      this.config = event.data;
      this.config.rangeLow = sampleRate / this.config.rangeLow;
      this.config.rangeHigh = sampleRate / this.config.rangeHigh;
      this.config.gain = this.config.gain || 0.25;
      this.config.ungain = this.config.ungain || 1;
      this.config.useSaturation = !!this.config.useSaturation;
      this.config.raw = !!this.config.raw;
      this.octaver = new Octaver(this.config);
    };
    this.port.postMessage("ready");
  }

  next(s) {
    if (!this.config || !this.octaver) {
      return 0;
    }

    var val = this.octaver.update(s);
    this.output += this.config.alpha * (val - this.output);
    var sample_out = this.output / this.config.alpha;

    sample_out = saturate(sample_out, this.config.useSaturation);
    sample_out *= this.config.volume * this.config.ungain;
    sample_out = clip(sample_out);

    return sample_out;
  }

  process(inputs, outputs, parameters) {
    if (!inputs || !inputs[0] || !inputs[0][0]) return true;

    for (var i = 0; i < outputs[0][0].length; i++) {
      var out = this.next(inputs[0][0][i]);
      for (var j = 0; j < outputs.length; j++) {
        for (var k = 0; k < outputs[0].length; k++) {
          outputs[j][k][i] = out;
        }
      }
    }
    return true;
  }
}

registerProcessor('synth', Synth);
