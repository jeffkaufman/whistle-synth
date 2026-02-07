class Osc {
  constructor(adjustment, oscConfig, cycles) {
    this.amp = 0;
    this.pos = -adjustment;
    this.speed = oscConfig.speed;
    cycles = Math.round(cycles*oscConfig.cycle);
    let polarity = 0;
    polarity = cycles % oscConfig.mod ? 1: -1;
    
    this.duration = oscConfig.duration;
    this.polarity = polarity;
    this.vol = oscConfig.volume;
    this.mute = oscConfig.mute;
    this.is_square = oscConfig.fn == "sqr";
    this.total_amplitude = 0;
    this.samples = 0;
  }

  next(octaver, log) {
    this.samples++;
    
    if (this.pos+1 >= octaver.hist.length || this.mute) {
      this.amp = 0;
      return 0;
    }

    if (this.duration > 0) {
      this.amp += 0.01 * (1 - this.amp);
    } else {
      this.amp *= 0.95;
    }
    
    const valA = octaver.getHist(Math.floor(this.pos), log);
    const valB = octaver.getHist(Math.floor(this.pos+1), log);
    const amtA = (this.pos % 1);
    const amtB = 1-amtA;

    let val = valA*amtA + valB*amtB;
    this.total_amplitude += Math.abs(val);
    if (this.is_square) {
      val = val > 0 ? 1 : -1;
      val *= (this.total_amplitude / this.samples);
    }
    
    this.pos += this.speed;
    return this.amp * val * this.polarity * this.vol;
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
    
    this.hist = [];
    for (var i = 0; i < 8192; i++) {
      this.hist.push(0);
    }
    this.hist_pos = 0;
    
    this.samples_since_last_crossing = 0;
    this.samples_since_attack_began = 0;

    this.positive = true;
    this.previous_sample = 0;
    this.rough_input_period = 40;
  }

  setHist(s) {
    this.hist[this.hist_pos] = s;
    this.hist_pos = (this.hist_pos + 1) % this.hist.length;
  }

  // pos is a positive number representing how far back in history to go
  getHist(pos, log) {
    return this.hist[(this.hist.length + this.hist_pos - pos) % this.hist.length];
  }
  
  update(s, log) {
    this.setHist(s);

    this.samples_since_last_crossing++;
    this.samples_since_attack_began++;

    if (this.positive) {
      if (s < 0) {
        /*
         * Let's say we take samples at p and n:
v         *
         *  p
         *   \
         *    \
         *  -------
         *      \
         *       n
         *
         * we could say the zero crossing is at n, but better would be to say
         * it's between p and n in proportion to how far each is from zero.  So,
         * if n is the current sample, that's:
         *
         *        |n|
         *   - ---------
         *     |n| + |p|
         *
         * But p is always positive and n is always negative, so really:
         *
         *        |n|            -n         n
         *   - ---------  =  - ------  =  -----
         *     |n| + |p|       -n + p     p - n
         */
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
          for (let i = 0; i < this.config.oscConfigs.length; i++) {
            this.oscs.push(new Osc(adjustment, this.config.oscConfigs[i], this.cycles));
          }
        }
        this.cycles++;

        for (let i = this.oscs.length - 1; i >= 0; i--) {
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
    
    let val = 0;
    for (let i = 0; i < this.oscs.length; i++) {
      val += this.oscs[i].next(this, log);
    }
    return val * this.config.volume;
  }
}

class Synth extends AudioWorkletProcessor {
  constructor() {
    super();
    this.count = 0;
    this.output = 0;
    this.config = null;
    this.octaver = null;
    this.port.onmessage = (event) => {
      this.config = event.data;
      this.config.speed = 1/this.config.speed;
      this.config.rangeLow = sampleRate / this.config.rangeLow;
      this.config.rangeHigh = sampleRate / this.config.rangeHigh;
      this.octaver = new Octaver(this.config);
    };
    this.port.postMessage("ready");
  }
  
  next(s, log) {
    if (!this.config) {
      return 0;
    }

    let val = this.octaver ? this.octaver.update(s, log) : 0;
    this.output += this.config.alpha * (val - this.output);
    return this.config.volume / this.config.alpha * this.output;
  }
  
  process (inputs, outputs, parameters) {
    if (!inputs || !inputs[0] || !inputs[0][0]) return;
    
    this.count++;
    for (var i = 0; i < outputs[0][0].length; i++) {
      var out = this.next(inputs[0][0][i], i === 0 && this.count % 500 === 0);
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
