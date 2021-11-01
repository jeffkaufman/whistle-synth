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
#define VOLUME (10.0)
#define DURATION (3)

#define HISTORY_LENGTH (8192)

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
  int cycles;
  float samples_since_last_crossing;
  float samples_since_attack_began;
  BOOL positive;
  float previous_sample;
  float rough_input_period;
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
}

void set_hist(float s) {
  octaver.hist[octaver.hist_pos] = s;
  octaver.hist_pos = (octaver.hist_pos + 1) % HISTORY_LENGTH;
}

float get_hist(int pos) {
  return octaver.hist[
    (HISTORY_LENGTH + octaver.hist_pos - pos) % HISTORY_LENGTH];
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
    struct Osc* osc, int cycles, float adjustment, float vol,
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

float volumes[10] = {0.00,  // 0
                     0.01,  // 1
                     0.02,  // 2
                     0.04,  // 3
                     0.08,  // 4
                     0.16,  // 5
                     0.32,  // 6
                     0.64,  // 7
                     1.28,  // 8
                     2.56}; // 9

struct int_from_file {
  const char* purpose;
  const char* fname;
  FILE* file;
  int value;
};

struct int_from_file voice_iff;
struct int_from_file volume_iff;

#define V_SOPRANO_RECORDER 1
#define V_DIST 2
#define V_LOWDIST 3
#define V_BASS_CLARINET 4
#define V_EBASS 5
#define V_VOCAL_2 6
#define V_RAW 7

#define N_OSCS_PER_LAYER 6
#define N_OSCS (N_OSCS_PER_LAYER*DURATION)
struct Osc oscs[N_OSCS];

#define ALPHA_HIGH (0.1)
#define ALPHA_MEDIUM (0.03)
#define ALPHA_LOW (0.01)

// in samples -- set to zero for off
#define LESLIE_PERIOD 0 // 4096
// in samples
#define LESLIE_DEPTH 36
#define LESLIE_SAMPLES (LESLIE_DEPTH*2)
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

#define USE_CLIPPING 1

float saturate(float v) {
  if (USE_CLIPPING) {
    return clip(v);
  }

  if (voice_iff.value == V_DIST || voice_iff.value == V_LOWDIST) {
    float c = sine_decimal(atan_decimal(v * .75));
    v += (SAT_1 * c);
    v += (SAT_2 * c*c);
    v += (SAT_4 * c*c*c*c);
    v += (SAT_8 * c*c*c*c*c*c*c);
    v -= SAT_BIAS;
    v *= 0.55;
  }

  return atan_decimal(v*2);
}

void init_oscs(float adjustment) {
  int cycles = octaver.cycles;
  int offset = (cycles % DURATION) * N_OSCS_PER_LAYER;

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
  } else if (voice_iff.value == V_BASS_CLARINET) {
    osc_init(&oscs[offset],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.4,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.25,
	     /*cycle=*/ 0.25,
	     /*mod=*/ 2);
    osc_init(&oscs[offset + 1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.4,
	     /*mode=*/ OSC_NAT,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 0.125,
	     /*cycle=*/ 0.125,
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
  } else if (voice_iff.value == V_LOWDIST) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 1.0/4,
	     /*cycle=*/ 1.0/2,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 3.0/4,
	     /*cycle=*/ 3.0/2,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 5.0/4,
	     /*cycle=*/ 5.0/2,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 7.0/4,
	     /*cycle=*/ 7.0/2,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+4],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.2,
	     /*mode=*/ OSC_SIN,
	     /*lfo_rate=*/ 0,
	     /*lfo_amplitude=*/ 0,
	     /*lfo_is_volume*/ TRUE,
	     /*speed=*/ 9.0/4,
	     /*cycle=*/ 9.0/2,
	     /*mod=*/ 2);
  } else if (voice_iff.value == V_EBASS) {
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


float update(float s) {
  if (voice_iff.value == V_RAW) {
    return s;
  }

  set_hist(s);
  update_duration(s);

  octaver.samples_since_last_crossing++;
  octaver.samples_since_attack_began++;

  int range_high = WHISTLE_RANGE_HIGH;
  int range_low = WHISTLE_RANGE_LOW;
  if (voice_iff.value == V_VOCAL_2) {
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
  return val * VOLUME * volumes[volume_iff.value];
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

void update_iff(struct int_from_file* iff) {
  rewind(iff->file);
  int new_value = read_number(iff->file);
  if (iff->value != new_value) {
    printf("%s: %d -> %d\n", iff->purpose, iff->value, new_value);
    iff->value = new_value;
  }
}

void* update_iffs(void* ignored) {
  open_iff_or_die(&voice_iff);
  open_iff_or_die(&volume_iff);

  while (1) {
    update_iff(&voice_iff);
    update_iff(&volume_iff);
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
  float *sampleBlock = NULL;
  int numBytes;

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

  inputParameters.channelCount = 1;  // mono
  inputParameters.sampleFormat = PA_SAMPLE_TYPE;
  inputParameters.suggestedLatency = inputInfo->defaultLowInputLatency ;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  outputParameters.device = best_audio_device_index;
  printf( "Output device # %d.\n", outputParameters.device );
  outputInfo = Pa_GetDeviceInfo( outputParameters.device );
  printf( "Output LL: %.2fms\n", outputInfo->defaultLowOutputLatency * 1000);
  outputParameters.channelCount = 1;  // mono
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

  numBytes = FRAMES_PER_BUFFER * SAMPLE_SIZE ;
  sampleBlock = (float *) malloc( numBytes );
  if( sampleBlock == NULL )
    {
      printf("Could not allocate record array.\n");
      goto error1;
    }
  memset( sampleBlock, SAMPLE_SILENCE, numBytes );

  err = Pa_StartStream( stream );
  if( err != paNoError ) goto error1;

  for (int i = 0; i < N_OSCS; i++) {
    oscs[i].active = FALSE;
    oscs[i].lfo_pos = 0;
  }

  for (int i = 0; i < DURATION_BLOCKS; i++) {
    duration_hist[i] = 0;
  }


  float leslie_hist[LESLIE_SAMPLES];
  for (int i = 0; i < LESLIE_SAMPLES; i++) {
    leslie_hist[i] = 0;
  }
  int leslie_write_offset = 0;
  float leslie_index = 0;

  float output = 0;

  while(TRUE) {
    err = Pa_ReadStream( stream, sampleBlock, FRAMES_PER_BUFFER );
    if (err & paInputOverflow) {
      printf("ignoring input undeflow\n");
    } else if( err ) goto xrun;

    float alpha = ALPHA_HIGH;
    if (voice_iff.value == V_LOWDIST) {
      alpha = ALPHA_MEDIUM;
    } else if (voice_iff.value == V_BASS_CLARINET ||
	       voice_iff.value == V_EBASS) {
      alpha = ALPHA_LOW;
    }

    for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
      float sample = sampleBlock[i];
      float val = update(sample);

      output += alpha * (val - output);
      float sample_out = output / alpha ; // makeup gain

      if (LESLIE_PERIOD > 0) {
	leslie_hist[(leslie_write_offset++) % LESLIE_SAMPLES] = sample_out;
	float leslie_read_pos =
	  leslie_write_offset +
	  LESLIE_DEPTH/2*(sine_decimal(leslie_index++/LESLIE_PERIOD)+1);
	int leslie_read_posA = (int)leslie_read_pos;
	int leslie_read_posB = (int)(leslie_read_pos+1);
	float leslie_read_amtA = leslie_read_pos - leslie_read_posA;
	float leslie_read_amtB = 1 - leslie_read_amtA;

	sample_out =
	  leslie_hist[leslie_read_posA % LESLIE_SAMPLES]*leslie_read_amtA +
	  leslie_hist[leslie_read_posB % LESLIE_SAMPLES]*leslie_read_amtB;
      }

      // never wrap -- wrapping sounds horrible
      sampleBlock[i] = saturate(sample_out);
    }

    err = Pa_WriteStream( stream, sampleBlock, FRAMES_PER_BUFFER );
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
  free( sampleBlock );
  Pa_Terminate();
  if( err & paInputOverflow )
    fprintf( stderr, "Input Overflow.\n" );
  if( err & paOutputUnderflow )
    fprintf( stderr, "Output Underflow.\n" );
  return -2;
 error1:
  free( sampleBlock );
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
  if (argc != 4) {
    printf("usage: %s /path/to/device/index /path/to/voice/file"
           " /path/to/volume/file\n", argv[0]);
    return -1;
  }
  int device_index = read_number(fopen(argv[1], "r"));
  voice_iff.purpose = "voice";
  voice_iff.fname = argv[2];
  voice_iff.value = V_EBASS;
  volume_iff.purpose = "volume";
  volume_iff.fname = argv[3];
  volume_iff.value = 5;

  start_iff_thread();
  return start_audio(device_index);
}
