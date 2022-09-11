/*
 * $Id: patest_read_record.c 757 2004-02-13 07:48:10Z rossbencina $
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

// derived from paex_read_write_wire.c by Jeff Kaufman 2019-07 and 2021-06

#define _GNU_SOURCE  // M_PI

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "portaudio.h"

#define SAMPLE_RATE       (44100)    // if you change this, change MIN/MAX_INPUT_PERIOD too
#define FRAMES_PER_BUFFER   (128)    // this is low, to minimize latency

/* Select sample format. */
#define PA_SAMPLE_TYPE  paFloat32
#define SAMPLE_SIZE (4)
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"

#define WHISTLE_RANGE_HIGH (14)
#define VOCAL_RANGE_HIGH (50)
#define WHISTLE_RANGE_LOW (75)
#define VOCAL_RANGE_LOW (300)
#define SLIDE (4)
// We amplify in two stages: first gain, then saturate, then volume.  This lets
// us get the effect of saturation/clipping independent of the output volume.
#define GAIN (1.0)
#define VOLUME (1.0)
#define DURATION (3)

#define GATE_SQUARED (0.01*0.01)
#define RECENT_GATE_SQUARED (40*40*GATE_SQUARED)
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

#define USB_SOUND_CARD_PREFIX "USB Audio Device"

/*******************************************************************/

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

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
      float histval =
	duration_hist[(DURATION_BLOCKS + duration_pos - i)%DURATION_BLOCKS];
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
  const char* fname;
  FILE* file;
  int value;
};

struct int_from_file voice_iff;
struct int_from_file volume_iff;
struct int_from_file gate_iff;
float gain;
float ungain;
float gate_squared;

#define V_SOPRANO_RECORDER 1
#define V_SQR 2
#define V_DIST 3
#define V_LOW_DIST 4
#define V_LOW_LOW_DIST 5
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

float saturate(float v) {
  if (voice_iff.value != V_DIST &&
      voice_iff.value != V_LOW_DIST &&
      voice_iff.value != V_LOW_LOW_DIST &&
      voice_iff.value != V_RAWDIST) {
    return clip(v);
  }

  float c = sine_decimal(atan_decimal(v * 4));
  v += (SAT_1 * c);
  v += (SAT_2 * c*c);
  v += (SAT_4 * c*c*c*c);
  v += (SAT_8 * c*c*c*c*c*c*c);
  v -= SAT_BIAS;
  v *= 0.55;
  return atan_decimal(v/4);
}

void init_oscs(float adjustment) {
  long long cycles = octaver.cycles;
  long long offset = (cycles % DURATION) * N_OSCS_PER_LAYER;

  if (voice_iff.value == V_SOPRANO_RECORDER) {
    gain = 0.3;
    ungain = 1;
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
  } else if (voice_iff.value == V_SQR) {
    gain = 0.25;
    ungain = 1;
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
  } else if (voice_iff.value == V_DIST) {
    gain = 0.125;
    ungain = 1;
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
  } else if (voice_iff.value == V_LOW_DIST) {
    gain = 0.25;
    ungain = 0.7;
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
	     /*mod=*/ 4);
  } else if (voice_iff.value == V_LOW_LOW_DIST) {
    gain = 0.15;
    ungain = 1;
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
	     /*mod=*/ 8);
  } else if (voice_iff.value == V_VOCAL_2) {
    gain = 0.25;
    ungain = 0.5;
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
    gain = 0.09;
    ungain = 1;
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
    gain = 0.25;
    ungain = 1;
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 1.0/32,
	     /*cycle=*/ 1.0/8,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 2.0/32,
	     /*cycle=*/ 2.0/8,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 3.0/32,
	     /*cycle=*/ 3.0/8,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.14,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 4.0/32,
	     /*cycle=*/ 4.0/8,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+4],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.06,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 5.0/32,
	     /*cycle=*/ 5.0/8,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+5],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.06,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ FALSE,
	     /*speed=*/ 6.0/32,
	     /*cycle=*/ 6.0/8,
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
float update(float s) {
  if (voice_iff.value == V_RAW || voice_iff.value == V_RAWDIST) {
    return s * gain;
  }

  set_hist(s);
  update_duration(s);

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

  if ((octaver.hist_sq/HISTORY_LENGTH <
       GATE_SQUARED * gate_squared) &&
      (octaver.recent_hist_sq / RECENT_LENGTH <
       RECENT_GATE_SQUARED * gate_squared )) {
//  if (grace_ticks == 0) {
        val = 0;
//    } else {
//      grace_ticks--;
//  } else {
//    grace_ticks = GRACE_TICKS;
//  }
  }

  return val * GAIN * gain;
}

float bpm_to_samples(float bpm) {
  float bps = bpm/60;
  return SAMPLE_RATE / bps;
}

float delay_tempo_bpm = 118.5;
int delay_repeats = 3;
float delay_volume = 1;
#define DELAY_HISTORY_LENGTH (SAMPLE_RATE*90*10)
float delay_history[DELAY_HISTORY_LENGTH];
uint64_t delay_write_pos = 0;

float delay_update(float sample) {
   uint64_t write_pos = delay_write_pos % DELAY_HISTORY_LENGTH;
   delay_history[write_pos] = sample;

   float sample_out = 0;

   float repeat_delta_samples = bpm_to_samples(delay_tempo_bpm);
   for (int repeat = 1; repeat <= delay_repeats; repeat++) {
      float repeat_pos = write_pos - (repeat_delta_samples*repeat);
      if (repeat_pos < 0) {
         repeat_pos += DELAY_HISTORY_LENGTH;
      }

      int repeat_A_pos = (int)repeat_pos;
      int repeat_B_pos = repeat_A_pos + 1;

      float repeat_A_frac = 1 - (repeat_pos - repeat_A_pos);

      float sample_A = delay_history[repeat_A_pos];
      // We added one to repeat_A_pos above, so it could be past the
      // end of delay_history.
      float sample_B = delay_history[repeat_B_pos % DELAY_HISTORY_LENGTH];

      sample_out += (sample_A * repeat_A_frac) + (sample_B * (1-repeat_A_frac));
   }

   delay_write_pos++;
   return sample_out * delay_volume / delay_repeats;
}



int read_number(FILE* file) {
  char buf[16];
  rewind(file);
  int bytesRead = fread(buf, 1, 16, file);
  if (bytesRead > 15) {
    bytesRead = 15;
  }
  buf[bytesRead] = '\0';
  return atoi(buf);
}

void open_iff_or_die(struct int_from_file* iff) {
  iff->file = fopen(iff->fname, "r");
  if (!iff->file) {
    perror("can't open file");
    fprintf(stderr, "  in: %s", iff->fname);
    exit(-1);
  }
  return;
}

void init_gains() {
  if (voice_iff.value == V_RAW) {
    gain = 0.125;
    ungain = 0.7;
  } else if (voice_iff.value == V_RAWDIST) {
    gain = 0.5;
    ungain = 0.25;
  } else {
    gain = 0.25;
    ungain = 1;
  }
}

void init_gate() {
  gate_squared = ((volumes[9-gate_iff.value] / volumes[5]) *
                  (volumes[9-gate_iff.value] / volumes[5]));
}

void update_iff(struct int_from_file* iff) {
  rewind(iff->file);
  int new_value = read_number(iff->file);
  if (iff->value != new_value) {
    printf("%s: %d -> %d\n", iff->purpose, iff->value, new_value);
    iff->value = new_value;
    init_octaver();
    init_gains();
    init_gate();
  }
}

void* update_iffs(void* ignored) {
  open_iff_or_die(&voice_iff);
  open_iff_or_die(&volume_iff);
  open_iff_or_die(&gate_iff);

  while (1) {
    update_iff(&voice_iff);
    update_iff(&volume_iff);
    update_iff(&gate_iff);
    usleep(50000 /* 50ms in us */);
  }
}




pthread_t iff_thread;
void start_iff_thread() {
  pthread_create(&iff_thread, NULL, &update_iffs, NULL);
}

int start_audio(int device_index) {
  PaStreamParameters inputParameters;
  PaStreamParameters outputParameters;
  PaStream *stream = NULL;
  PaError err;
  const PaDeviceInfo* inputInfo;
  const PaDeviceInfo* outputInfo;
  float *sampleBlockIn = NULL;
  float *sampleBlockOut = NULL;
  int numBytesPerChannel;

  init_octaver();

  err = Pa_Initialize();
  if( err != paNoError ) goto error2;

  int numDevices = Pa_GetDeviceCount();
  if (numDevices < 0) {
    die("no devices found");
  }
  const PaDeviceInfo* deviceInfo;
  int best_audio_device_index = -1;
  int seen_good_devices = 0;
  for(int i = 0; i < numDevices && best_audio_device_index == -1; i++) {
    deviceInfo = Pa_GetDeviceInfo(i);
    printf("device[%d]: %s\n", i, deviceInfo->name);
    // Take the Nth device whose name starts with USB_SOUND_CARD_PREFIX
    if (best_audio_device_index == -1 &&
        strncmp(USB_SOUND_CARD_PREFIX,
                deviceInfo->name,
                strlen(USB_SOUND_CARD_PREFIX)) == 0) {
      if (seen_good_devices == device_index) {
        best_audio_device_index = i;
      } else {
        seen_good_devices++;
      }
    }
  }

  if (best_audio_device_index == -1) {
    if (device_index == 0) {
      printf("falling back to default\n");
      best_audio_device_index = Pa_GetDefaultInputDevice();
    } else {
      die("no good device found");
    }
  }

  inputParameters.device = best_audio_device_index;
  printf( "Input device # %d.\n", inputParameters.device );
  inputInfo = Pa_GetDeviceInfo( inputParameters.device );
  printf( "   Name: %s\n", inputInfo->name );
  printf( "     LL: %.2fms\n", inputInfo->defaultLowInputLatency*1000 );

  //inputParameters.channelCount = 2;  // stereo
  inputParameters.channelCount = 1;  // FIXME!
  inputParameters.sampleFormat = PA_SAMPLE_TYPE;
  inputParameters.suggestedLatency = inputInfo->defaultLowInputLatency ;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  outputParameters.device = best_audio_device_index;
  printf( "Output device # %d.\n", outputParameters.device );
  outputInfo = Pa_GetDeviceInfo( outputParameters.device );
  printf( "Output LL: %.2fms\n", outputInfo->defaultLowOutputLatency * 1000);
  outputParameters.channelCount = 2;  // stereo
  outputParameters.sampleFormat = PA_SAMPLE_TYPE;
  outputParameters.suggestedLatency = outputInfo->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  /* -- setup -- */

  err = Pa_OpenStream(&stream,
		      &inputParameters,
		      &outputParameters,
		      SAMPLE_RATE,
		      FRAMES_PER_BUFFER,
		      paClipOff,      /* we won't output out of range samples so dvon't bother clipping them */
		      NULL, /* no callback, use blocking API */
		      NULL ); /* no callback, so no callback userData */
  if( err != paNoError ) goto error2;

  numBytesPerChannel = FRAMES_PER_BUFFER * SAMPLE_SIZE ;
  sampleBlockIn = (float *) malloc( numBytesPerChannel );
  sampleBlockOut = (float *) malloc( numBytesPerChannel * 2);
  if( sampleBlockIn == NULL || sampleBlockOut == NULL) {
    printf("Could not allocate in and out arrays.\n");
    goto error1;
  }
  memset( sampleBlockIn, SAMPLE_SILENCE, numBytesPerChannel );
  memset( sampleBlockOut, SAMPLE_SILENCE, numBytesPerChannel * 2);

  err = Pa_StartStream( stream );
  if( err != paNoError ) goto error1;

  for (int i = 0; i < N_OSCS; i++) {
    oscs[i].active = FALSE;
    oscs[i].lfo_pos = 0;
  }

  for (int i = 0; i < DURATION_BLOCKS; i++) {
    duration_hist[i] = 0;
  }

  for (int i = 0; i < DELAY_HISTORY_LENGTH; i++) {
     delay_history[i] = 0;
  }


  float output = 0;
  while(TRUE) {
    err = Pa_ReadStream( stream, sampleBlockIn, FRAMES_PER_BUFFER );
    if (err & paInputOverflow) {
      printf("ignoring input undeflow\n");
    } else if( err ) goto xrun;

    float alpha = ALPHA_HIGH;
    if (voice_iff.value == V_EBASS) {
      alpha = ALPHA_LOW;
    }

    for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
      //float sample = sampleBlockIn[i*2];
      //float delay_sample = sampleBlockIn[i*2 + 1];
      float sample = 0;  // FIXME
      float delay_sample = sampleBlockIn[i];  // FIXME
      
      float val = update(sample);
      float delay_sample_out = delay_update(delay_sample);

      output += alpha * (val - output);
      float sample_out = output / alpha ; // makeup gain

      // never wrap -- wrapping sounds horrible
      sample_out = saturate(sample_out);
      delay_sample_out = saturate(delay_sample_out);

      sample_out *= VOLUME * volumes[volume_iff.value] * ungain;
      // Ideally this is never hit, but it would be really bad if it wrapped.
      sample_out = clip(sample_out);

      //sampleBlockOut[i*2] = sample_out; 
      sampleBlockOut[i*2] = delay_sample_out; // FIXME
      sampleBlockOut[i*2 + 1] = delay_sample_out;
    }

    err = Pa_WriteStream( stream, sampleBlockOut, FRAMES_PER_BUFFER );
    if (err & paOutputUnderflow) {
      printf("ignoring output undeflow\n");
    } else if( err ) goto xrun;
  }

xrun:
  printf("err = %d\n", err); fflush(stdout);
  if( stream ) {
    Pa_AbortStream( stream );
    Pa_CloseStream( stream );
  }
  free( sampleBlockOut );
  free( sampleBlockIn );
  Pa_Terminate();
  if( err & paInputOverflow )
    fprintf( stderr, "Input Overflow.\n" );
  if( err & paOutputUnderflow )
    fprintf( stderr, "Output Underflow.\n" );
  return -2;
 error1:
  free( sampleBlockOut );
  free( sampleBlockIn );
 error2:
  if( stream ) {
    Pa_AbortStream( stream );
    Pa_CloseStream( stream );
  }
  Pa_Terminate();
  fprintf( stderr, "An error occured while using the portaudio stream\n" );
  fprintf( stderr, "Error number: %d\n", err );
  fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
  return -1;
}

int main(int argc, char** argv) {
  if (argc != 5) {
    printf("usage: %s /device/index /voice/file /volume/file /gate/file\n",
           argv[0]);
    return -1;
  }
  int device_index = read_number(fopen(argv[1], "r"));
  voice_iff.purpose = "voice";
  voice_iff.fname = argv[2];
  voice_iff.value = V_EBASS;
  volume_iff.purpose = "volume";
  volume_iff.fname = argv[3];
  volume_iff.value = 5;
  gate_iff.purpose = "gate";
  gate_iff.fname = argv[4];
  gate_iff.value = 1;

  start_iff_thread();
  return start_audio(device_index);
}
