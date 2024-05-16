#include <Audio.h>

#define SAMPLE_RATE       (44100)    // if you change this, change MIN/MAX_INPUT_PERIOD too

#define WHISTLE_RANGE_HIGH (14)
#define VOCAL_RANGE_HIGH (50)
#define WHISTLE_RANGE_LOW (75)
#define VOCAL_RANGE_LOW (300)
#define SLIDE (4)

#define DURATION (3)

float gate_squared = 0.001*0.001;
#define GATE_SCALAR (0.1)
#define RECENT_GATE_SQUARED_MULTIPLIER (gate_squared * 40 * 40)
//#define GRACE_TICKS (44100)

#define HISTORY_LENGTH (8192)
#define RECENT_LENGTH (256)

#define BOOL char
#define TRUE 1
#define FALSE 0

#define OSC_NAT 0
#define OSC_SQR 1
#define OSC_SIN 2

#define DURATION_UNITS (400) // samples
#define DURATION_BLOCKS (100) // in DURATION_UNITS
#define DURATION_MAX_VAL (0.04)

/*******************************************************************/

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
      float histval =	duration_hist[(DURATION_BLOCKS + duration_pos - i)%DURATION_BLOCKS];
      if (block_min < 0 || histval < block_min) {
        block_min = histval;
      }
      duration_val += block_min;
    }
    duration_val = fminf(duration_val/DURATION_BLOCKS, DURATION_MAX_VAL);
    //printf("%.2f\n", duration_val);
  }
}

struct Octaver {
  float hist[HISTORY_LENGTH];
  int hist_pos;
  long long cycles;
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
  octaver.hist_sq += (s*s);
  octaver.hist_sq -= (octaver.hist[octaver.hist_pos] *
                      octaver.hist[octaver.hist_pos]);

  octaver.recent_hist_sq += (s*s);
  int recent_pos = (octaver.hist_pos - RECENT_LENGTH +
                    HISTORY_LENGTH) % HISTORY_LENGTH;
  octaver.recent_hist_sq -= (octaver.hist[recent_pos] *
                             octaver.hist[recent_pos]);

  octaver.hist[octaver.hist_pos] = s;
  octaver.hist_pos = (octaver.hist_pos + 1) % HISTORY_LENGTH;
}

float get_hist(int pos) {
  return octaver.hist[
    (HISTORY_LENGTH + octaver.hist_pos - pos) % HISTORY_LENGTH];
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
  int samples;
  float total_amplitude;
  int duration;

  int mode;
  float speed;
  float polarity;
  float vol;

  float lfo_pos;
  float lfo_rate;
  float lfo_amplitude;
  BOOL lfo_is_volume;  // either affects volume or speed

  float rough_input_period;
};

void osc_init(
    struct Osc* osc, long long cycles, float adjustment, float vol,
    int mode, float lfo_rate, float lfo_amplitude,
    BOOL lfo_is_volume, float speed, float cycle, int mod) {

  osc->active = TRUE;
  osc->amp = 0;
  osc->pos = -adjustment;
  osc->samples = 0;
  osc->total_amplitude = 0;
  osc->duration = DURATION;

  osc->lfo_rate = lfo_rate;
  osc->lfo_amplitude = lfo_amplitude;
  osc->lfo_is_volume = lfo_is_volume;

  osc->mode = mode;
  osc->speed = speed;
  if (mod == 0) {
    osc->polarity = 1;
  } else {
    osc->polarity = ((int)(cycle * cycles)) % mod ? 1 : -1;
  }
  osc->vol = vol;

  osc->rough_input_period = octaver.rough_input_period;
}

void osc_diff(struct Osc* osc1, struct Osc* osc2) {
  if (osc1->pos != osc2->pos) {
    printf("pos mismatch %.5f %.5f\n", osc1->pos, osc2->pos);
  }
}

float volumes[10] = {
                     0.026, // 0
                     0.039, // 1
                     0.059, // 2
                     0.088, // 3
                     0.132, // 4
                     0.198, // 5
                     0.296, // 6
                     0.444, // 7
                     0.667, // 8
                     1.000, // 9
};

struct int_from_file {
  const char* purpose;
  int value;
};

struct int_from_file voice_iff;
struct int_from_file volume_iff;
struct int_from_file gate_iff;

#define V_SOPRANO_RECORDER 1
#define V_BASS_FLUTE 2
#define V_DIST 3
#define V_REED 4
#define V_FLUTE 5
#define V_EBASS 6
#define V_VOCAL_2 7
#define V_VOCAL_1 8
#define V_RAW 9
#define V_RAWDIST 0

#define N_OSCS_PER_LAYER 6
#define N_OSCS (N_OSCS_PER_LAYER*DURATION)
struct Osc oscs[N_OSCS];

#define ALPHA_HIGH (0.1)
#define ALPHA_MEDIUM (0.03)
#define ALPHA_LOW (0.01)

float sine_decimal(float v) {
  return sin((v+0.5)*M_PI*2);
}

float clip(float v) {
  return fmaxf(-1, fminf(1, v));
}

float atan_decimal(float v) {
  return atanf(v) / (M_PI/2);
}

#define SAT_1 1
#define SAT_2 1
#define SAT_4 1
#define SAT_8 1
#define SAT_BIAS 0.5

void init_oscs(float adjustment) {
  long long cycles = octaver.cycles;
  long long offset = (cycles % DURATION) * N_OSCS_PER_LAYER;

  osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/  0.5,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 1.0/4,
	     /*cycle=*/ 1.0/4,
	     /*mod=*/ 2);

  return;

  if (voice_iff.value == V_SOPRANO_RECORDER) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/  0.5,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_BASS_FLUTE) {
    int cycle_base = 4;
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/  0.5,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 1.0/cycle_base,
	     /*cycle=*/ 1.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/  0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 2.0/cycle_base,
	     /*cycle=*/ 2.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/  0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 3.0/cycle_base,
	     /*cycle=*/ 3.0/cycle_base,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_DIST) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.5,
	     /*mode=*/ OSC_SQR,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_REED) {
    int cycle_base = 4;
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/  0.5,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.25,
	     /*cycle=*/ 1.0/cycle_base,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_FLUTE) {
    int cycle_base = 2;
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/  0.5,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/  0.15,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.53,
	     /*cycle=*/ 2.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/  0.15,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.48,
	     /*cycle=*/ 2.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/  0.15,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.51,
	     /*cycle=*/ 3.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+4],
	     cycles,
	     adjustment,
	     /*vol=*/  0.15,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.49,
	     /*cycle=*/ 3.0/cycle_base,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_VOCAL_2) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.4,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 0.5,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_VOCAL_1) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.4,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_EBASS) {
    int speed_base = 32;
    int cycle_base = 16;
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 1.0/speed_base,
	     /*cycle=*/ 8.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.24,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 2.0/speed_base,
	     /*cycle=*/ 2.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 3.11/speed_base,
	     /*cycle=*/ 3.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 4.3/speed_base,
	     /*cycle=*/ 4.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+4],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.06,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 5.7/speed_base,
	     /*cycle=*/ 5.0/cycle_base,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+5],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.06,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 6.1/speed_base,
	     /*cycle=*/ 6.0/cycle_base,
	     /*mod=*/ 2);
  }
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
  float valB = get_hist((int)(osc->pos+1));
  float amtA = osc->pos - (int)osc->pos;
  float amtB = 1-amtA;

  float val = valA*amtA + valB*amtB;
  osc->total_amplitude += fabsf(val);
  if (osc->mode != OSC_NAT) {
    if (osc->mode == OSC_SQR) {
      val = val > 0 ? 1 : -1;
    } else if (osc->mode == OSC_SIN) {
      val = sine_decimal(osc->pos / osc->rough_input_period);
    }
    val *= (osc->total_amplitude / osc->samples);
  }

  osc->pos += osc->speed;
  val = osc->amp * val * osc->polarity * osc->vol;

  if (osc->lfo_amplitude > 0) {
    //printf("%.2f %.2f\n", osc->lfo_pos, sine_decimal(osc->lfo_pos));
    float lfo_amount = (sine_decimal(osc->lfo_pos)+1)*osc->lfo_amplitude;
    if (osc->lfo_is_volume) {
      val = val*lfo_amount + val*(1-osc->lfo_amplitude);
    } else {
      osc->pos += lfo_amount;
    }
    osc->lfo_pos += (1/osc->lfo_rate);
  }
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
  if (octaver.hist_pos == HISTORY_LENGTH-1) {
    //printf("recent: %.12f -- %.12f\n", recent_hist_squared_sum(), octaver.recent_hist_sq/RECENT_LENGTH);
    octaver.recent_hist_sq = recent_hist_squared_sum();
  }

  octaver.samples_since_last_crossing++;
  octaver.samples_since_attack_began++;

  int range_high = WHISTLE_RANGE_HIGH;
  int range_low = WHISTLE_RANGE_LOW;
  if (voice_iff.value == V_VOCAL_2 || voice_iff.value == V_VOCAL_1) {
    range_high = VOCAL_RANGE_HIGH;
    range_low = VOCAL_RANGE_LOW;
  }

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

      if (octaver.rough_input_period > range_high &&
          octaver.rough_input_period < range_low) {
        //Serial.printf("period: %.2f\n", octaver.rough_input_period);
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
  for (int i = 0 ; i < N_OSCS; i++) {
    val += osc_next(&oscs[i]);
  }

  if ((octaver.hist_sq/HISTORY_LENGTH < gate_squared) &&
      (octaver.recent_hist_sq / RECENT_LENGTH < gate_squared * RECENT_GATE_SQUARED_MULTIPLIER)) {
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

class WhistleSynth : public AudioStream
{
private:
  audio_block_t *inputQueueArray[1];

public:
  WhistleSynth() : AudioStream(1, inputQueueArray) {
    // any extra initialization
  }
  virtual void update(void) {
    audio_block_t* block = receiveWritable(0);
    if (!block) {
      return;
    }

    float alpha = ALPHA_LOW;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      float sample = block->data[i] / 32768.0;      
      float val = update_sample(sample);

      output += alpha * (val - output);
      float sample_out = output / alpha ; // makeup gain

      // Ideally this is never hit, but it would be really bad if it wrapped.
      sample_out = clip(sample_out / 3);

      block->data[i] = sample_out * 32768;
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

AudioControlSGTL5000     sgtl5000_1;

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
    oscs[i].lfo_pos = 0;
  }

  for (int i = 0; i < DURATION_BLOCKS; i++) {
    duration_hist[i] = 0;
  }


  voice_iff.purpose = "voice";
  voice_iff.value = V_EBASS;
  volume_iff.purpose = "volume";
  volume_iff.value = 5;
  gate_iff.purpose = "gate";
  gate_iff.value = 1;

}

void loop() {
  int potValue = analogRead(A5);
  potValue = 512-max(0, potValue - 512);

  gate_squared = (GATE_SCALAR * potValue / 512) * (GATE_SCALAR * potValue / 512);

  // Print the value to the serial monitor
  Serial.printf("Potentiometer value: %d, gs: %.5f\n", potValue, gate_squared);
  delay(100);
}