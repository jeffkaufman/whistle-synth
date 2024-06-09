#include <Audio.h>
#include <ADC.h>

ADC adc;

#define SAMPLE_RATE (44100)  // if you change this, change MIN/MAX_INPUT_PERIOD too

#define WHISTLE_RANGE_HIGH (14)
#define WHISTLE_RANGE_LOW (75)

// TODO: remember what this is about and why it's so low
#define DURATION (9)

float gate_squared = 0.001 * 0.001;
#define GATE_SCALAR (0.1)
#define RECENT_GATE_SQUARED_MULTIPLIER (gate_squared * 40 * 40)

#define HISTORY_LENGTH (8192)
#define RECENT_LENGTH (256)

#define BOOL char
#define TRUE 1
#define FALSE 0

#define DURATION_UNITS (400)   // samples
#define DURATION_BLOCKS (100)  // in DURATION_UNITS
#define DURATION_MAX_VAL (0.04)

#define USE_OSC2 (0)
#define N_OSC2S (8)
/*******************************************************************/

float wah = 0;

float duration_hist[DURATION_BLOCKS];
int duration_pos = 0;
float duration_current_total = 0;
int duration_current_count = 0;
float duration_val = 0;
void update_duration(float sample) {
  duration_current_total += fabs(sample);
  duration_current_count++;
  if (duration_current_count > DURATION_UNITS) {
    float val = duration_current_total / duration_current_count;
    duration_current_total = 0;
    duration_current_count = 0;
    duration_hist[(++duration_pos) % DURATION_BLOCKS] = val;

    duration_val = 0;
    float block_min = -1;
    for (int i = 0; i < DURATION_BLOCKS; i++) {
      float histval = duration_hist[(DURATION_BLOCKS + duration_pos - i) % DURATION_BLOCKS];
      if (block_min < 0 || histval < block_min) {
        block_min = histval;
      }
      duration_val += block_min;
    }
    duration_val = fminf(duration_val / DURATION_BLOCKS, DURATION_MAX_VAL);
    //printf("%.2f\n", duration_val);
  }
}

struct Octaver {
  float hist[HISTORY_LENGTH];
  unsigned int hist_pos;
  unsigned long long cycles;
  float samples_since_last_crossing;
  float samples_since_attack_began;
  BOOL positive;
  float previous_sample;
  float rough_input_period;
  float hist_sq;
  float recent_hist_sq;
};

struct Octaver octaver;

void init_octaver() {
  for (int i = 0; i < HISTORY_LENGTH; i++) {
    octaver.hist[i] = 0;
  }
  octaver.cycles = 0;
  octaver.hist_pos = 0;

  octaver.samples_since_last_crossing = 0;
  octaver.samples_since_attack_began = 0;

  octaver.positive = TRUE;
  octaver.previous_sample = 0;
  octaver.rough_input_period = 40;
  octaver.hist_sq = 0;
  octaver.recent_hist_sq = 0;
}

void set_hist(float s) {
  octaver.hist_sq += (s * s);
  octaver.hist_sq -= (octaver.hist[octaver.hist_pos] * octaver.hist[octaver.hist_pos]);

  octaver.recent_hist_sq += (s * s);
  int recent_pos = (octaver.hist_pos - RECENT_LENGTH + HISTORY_LENGTH) % HISTORY_LENGTH;
  octaver.recent_hist_sq -= (octaver.hist[recent_pos] * octaver.hist[recent_pos]);

  octaver.hist[octaver.hist_pos] = s;
  octaver.hist_pos = (octaver.hist_pos + 1) % HISTORY_LENGTH;
}

float get_hist(int pos) {
  return octaver.hist[(HISTORY_LENGTH + octaver.hist_pos - pos) % HISTORY_LENGTH];
}

float hist_squared_sum() {
  float s = 0;
  for (int i = 0; i < HISTORY_LENGTH; i++) {
    s += (octaver.hist[i] * octaver.hist[i]);
  }
  return s;
}

// Only called when octaver.hist_pos = HISTORY_LENGTH-1
float recent_hist_squared_sum() {
  float s = 0;
  for (int i = HISTORY_LENGTH - RECENT_LENGTH; i < HISTORY_LENGTH; i++) {
    s += (octaver.hist[i] * octaver.hist[i]);
  }
  return s;
}

struct Osc {
  BOOL active;
  float amp;
  float pos;
  unsigned long long samples;
  float total_amplitude;
  int duration;

  float speed;
  float polarity;
  float vol;

  float rough_input_period;
};

struct Osc2 {
  float vol;
  float pos;
  float period;

  float target_vol;
  float target_period;
};


void osc_init(
  struct Osc* osc, long long cycles, float adjustment, float vol,
  float speed, float cycle, int mod) {

  osc->active = TRUE;
  osc->amp = 0;
  osc->pos = -adjustment;
  osc->samples = 0;
  osc->total_amplitude = 0;
  osc->duration = DURATION;

  osc->speed = speed;
  osc->polarity = ((int)(cycle * cycles)) % mod ? 1 : -1;
  osc->vol = vol;

  osc->rough_input_period = octaver.rough_input_period;
}

#define N_OSCS_PER_LAYER 1
#define N_OSCS (N_OSCS_PER_LAYER * DURATION)
struct Osc oscs[N_OSCS];

#define ALPHA_LOW (0.01)

float sine_decimal(float v) {
  return sin((v + 0.5) * M_PI * 2);
}

float clip(float v) {
  return fmaxf(-1, fminf(1, v));
}

Osc2 osc2s[N_OSC2S];

#define OSC2_PERIOD_ALPHA (0.999)
#define OSC2_VOL_ALPHA (0.999)

void osc2_tick(struct Osc2* osc2) {
  osc2->period = osc2->period * OSC2_PERIOD_ALPHA + (1-OSC2_PERIOD_ALPHA) * osc2->target_period;
  osc2->vol = osc2->vol * OSC2_VOL_ALPHA + (1-OSC2_VOL_ALPHA) * osc2->target_vol;
}

void osc2_init(struct Osc2* osc2, float vol, float period) {
  osc2->pos = 0;
  osc2->period = osc2->target_period = period;
  osc2->vol = osc2->target_vol = vol;
}

float osc2_next(struct Osc2* osc2) {
  osc2->pos += 1.0/(osc2->period);
  if (osc2->pos > 1) {
    osc2->pos -= 1;
  }
  return sine_decimal(osc2->pos - 0.5) * osc2->vol;
}

float varspeed = 1.0/16;

void init_oscs(float adjustment) {
  unsigned long long cycles = octaver.cycles;

  if (cycles % 2 != 0) {
    return;
  }
  cycles = cycles / 2;

  unsigned long long offset = (cycles % DURATION) * N_OSCS_PER_LAYER;

  float volspeed = 1/(2*(10-min(9, max(octaver.recent_hist_sq, 0))));
  //float volspeed = max(1/32, (min(9, max(octaver.recent_hist_sq, 0)) / .9 / 20));

  float usespeed = (1/16.0 * (1-wah)) + (volspeed * wah);

  osc_init(&oscs[offset + 0],
           cycles,
           adjustment,
           /*vol=*/0.12,
           /*speed=*/ usespeed,
           /*cycle=*/ 1.0/2,
           /*mod=*/4);
}

float osc_next(struct Osc* osc) {
  if (!osc->active) {
    return 0;
  }

  osc->samples++;

  if (osc->duration > 0) {
    osc->amp += 0.01 * (1 - osc->amp);
  } else {
    osc->amp *= 0.95;
  }

  float valA = get_hist((int)osc->pos);
  float valB = get_hist((int)(osc->pos + 1));
  float amtA = osc->pos - (int)osc->pos;
  float amtB = 1 - amtA;

  float val = valA * amtA + valB * amtB;
  osc->total_amplitude += fabsf(val);
  val = sine_decimal(osc->pos / osc->rough_input_period) * (osc->total_amplitude / osc->samples);
  osc->pos += osc->speed;
  val = osc->amp * val * osc->polarity * osc->vol;

  return val;
}

void handle_cycle() {
  for (int i = 0; i < N_OSCS; i++) {
    if (oscs[i].active) {
      if (oscs[i].duration > 0) {
        oscs[i].duration--;
      }
      if (oscs[i].duration < 1 && oscs[i].amp < 0.001) {
        oscs[i].active = FALSE;
      }
    }
  }
}

u_int64_t ticks = 0;
u_int64_t grace_ticks = 0;
float update_sample(float s) {
  set_hist(s);
  update_duration(s);

  if (false && ticks % 10000 == 0) {
    Serial.printf("!! %lu %f\n", ticks, s);
  }

  if (false && ticks % 1000 == 0) {
    Serial.printf("%lu %f\n", ticks, s);
  }


  // To avoid drift, recompute history every 10s.
  if ((++ticks) % 441000 == 0) {
    //printf("%lld volume: %.12f -- %.12f\n", ticks, hist_squared_sum(), octaver.hist_sq/HISTORY_LENGTH);
    octaver.hist_sq = hist_squared_sum();
  }
  if (octaver.hist_pos == HISTORY_LENGTH - 1) {
    //printf("recent: %.12f -- %.12f\n", recent_hist_squared_sum(), octaver.recent_hist_sq/RECENT_LENGTH);
    octaver.recent_hist_sq = recent_hist_squared_sum();
  }

  octaver.samples_since_last_crossing++;
  octaver.samples_since_attack_began++;

  int range_high = WHISTLE_RANGE_HIGH;
  int range_low = WHISTLE_RANGE_LOW;

  if (octaver.positive) {
    if (s < 0) {
      /*
       * Let's say we take samples at p and n:
       *
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
      float first_negative = s;
      float last_positive = octaver.previous_sample;
      float adjustment = first_negative / (last_positive - first_negative);
      if (isnan(adjustment)) {
        adjustment = 0;
      }
      octaver.samples_since_last_crossing -= adjustment;
      octaver.rough_input_period = octaver.samples_since_last_crossing;

      if (octaver.rough_input_period > range_high && octaver.rough_input_period < range_low) {
        init_oscs(adjustment);
      }

      octaver.cycles++;
      handle_cycle();

      octaver.positive = FALSE;
      octaver.samples_since_last_crossing = -adjustment;
    }
  } else {
    if (s > 0) {
      octaver.positive = TRUE;
    }
  }

  octaver.previous_sample = s;

  float val = 0;

  if (USE_OSC2) {
    for (int i = 0 ; i < N_OSC2S; i++) {
      osc2s[i].target_period = octaver.rough_input_period * 4 / (i+1.0);
      osc2s[i].target_vol = max(0, min(1, octaver.recent_hist_sq / 10.0 / N_OSC2S / (1+0.5*i)));
      osc2_tick(&osc2s[i]);
      val += osc2_next(&osc2s[i]);
    }
  } else {
    for (int i = 0; i < N_OSCS; i++) {
      val += osc_next(&oscs[i]);
    }
  }

  if ((octaver.hist_sq / HISTORY_LENGTH < gate_squared) && (octaver.recent_hist_sq / RECENT_LENGTH < gate_squared * RECENT_GATE_SQUARED_MULTIPLIER)) {
    //  if (grace_ticks == 0) {
    val = 0;
    //    } else {
    //      grace_ticks--;
    //  } else {
    //    grace_ticks = GRACE_TICKS;
    //  }
  }

  return val;
}

float output = 0;
float alpha = ALPHA_LOW;

class WhistleSynth : public AudioStream {
private:
  audio_block_t* inputQueueArray[1];

public:
  WhistleSynth()
    : AudioStream(1, inputQueueArray) {
    // any extra initialization
  }
  virtual void update(void) {
    audio_block_t* block = receiveWritable(0);
    if (!block) {
      return;
    }


    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      float sample = block->data[i] / 32767.5;
      float val = update_sample(sample);
      float sample_out;

      if (USE_OSC2) {
        sample_out = val;
      } else {
        output += alpha * (val - output);
        sample_out = output / alpha;  // makeup gain

        // Ideally this is never hit, but it would be really bad if it wrapped.
        sample_out = clip(sample_out / 3);
      }

      block->data[i] = sample_out * 32767;
    }

    transmit(block, 0);
    release(block);
  }
};

AudioInputI2S i2s2;
WhistleSynth whistleSynth;
AudioOutputI2S i2s1;

AudioConnection patchCord1(i2s2, 0, whistleSynth, 0);
AudioConnection patchCord3(whistleSynth, 0, i2s1, 0);
AudioConnection patchCord4(whistleSynth, 0, i2s1, 1);

AudioControlSGTL5000 sgtl5000_1;

void setup() {
  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(12);

  // Enable the audio shield
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.5);

  init_octaver();

  for (int i = 0; i < N_OSCS; i++) {
    oscs[i].active = FALSE;
  }

  for (int i = 0; i < DURATION_BLOCKS; i++) {
    duration_hist[i] = 0;
  }

  adc.adc0->setAveraging(127);

  for (int i = 0; i < N_OSC2S; i++) {
    osc2_init(&osc2s[i], 0, 30*16/(1+i));
  }
}

void loop() {
  int a2Value = analogRead(A2); // 5 (black)
  gate_squared = (GATE_SCALAR * a2Value / 1024) * (GATE_SCALAR * a2Value / 1024);

  /*
  float a8Value = analogRead(A8) / 1024.0; // 4 (white)
  if (a8Value < 0.1) {
    a8Value = 0;
  } else if (a8Value > 0.9) {
    a8Value = 1;
  }
  wah = a8Value;*/
  
  Serial.printf("%.5f %.5f %.5f, %.5f, %.5f\n", osc2s[0].period, osc2s[0].target_period, osc2s[0].pos, octaver.recent_hist_sq, wah);


  //Serial.printf("%.3f\n", a8Value);
  //Serial.printf("%.5f\n", octaver.recent_hist_sq);
/*
  if (a8Value > 0.8) {
    varspeed = 1/64.0;
  } else if (a8Value > 0.7) {
    varspeed = 1/32.0;
  } else if (a8Value > 0.6) {
    varspeed = 1/16.0;
  } else if (a8Value > 0.5) {
    varspeed = 1/8.0;
  } else if (a8Value > 0.4) {
    varspeed = 1/4.0;
  } else if (a8Value > 0.3) {
    varspeed = 1/2.0;
  } else {
    varspeed = 1/3.0;
  }
  */

  //varspeed = 1/pow(2, max(a8Value,0.1) * 10);
  

  //Serial.printf("A0=%d, A1=%d, A3=%d, A8=%d\n", analogRead(A0), analogRead(A1), analogRead(A3), analogRead(A8));

  delay(100);
}