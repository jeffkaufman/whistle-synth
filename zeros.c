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
#include "portaudio.h"

#define SAMPLE_RATE       (44100)    // if you change this, change MIN/MAX_INPUT_PERIOD too
#define FRAMES_PER_BUFFER   (128)    // this is low, to minimize latency

/* Select sample format. */
#define PA_SAMPLE_TYPE  paFloat32
#define SAMPLE_SIZE (4)
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"

#define RANGE_HIGH (14)
#define RANGE_LOW (71)
#define SLIDE (4)
#define VOLUME (0.9)
#define DURATION (3)

#define HISTORY_LENGTH (8192)

#define BOOL char
#define TRUE 1
#define FALSE 0

#define DURATION_UNITS (400) // samples
#define DURATION_BLOCKS (100) // in DURATION_UNITS

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
    duration_val = duration_val/DURATION_BLOCKS;
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

  BOOL is_square;
  float speed;
  float polarity;
  float vol;

  float lfo_pos;
  float lfo_speed;
  float lfo_vol;
};

void osc_init(
    struct Osc* osc, int cycles, float adjustment, float vol,
    BOOL is_square, float lfo_speed, float lfo_vol,
    float speed, float cycle, int mod) {
  osc->active = TRUE;
  osc->amp = 0;
  osc->pos = -adjustment;
  osc->samples = 0;
  osc->total_amplitude = 0;
  osc->duration = DURATION;

  osc->lfo_speed = lfo_speed;
  osc->lfo_vol = lfo_vol;

  osc->is_square = is_square;
  osc->speed = speed;
  if (mod == 0) {
    osc->polarity = 1;
  } else {
    osc->polarity = ((int)(cycle * cycles)) % mod ? 1 : -1;
  }
  osc->vol = vol;
}

void osc_diff(struct Osc* osc1, struct Osc* osc2) {
  if (osc1->pos != osc2->pos) {
    printf("pos mismatch %.5f %.5f\n", osc1->pos, osc2->pos);
  }
}

#define V_SIMPLE_SINE 0
#define V_SIMPLE_SQUARE 1
#define V_BASS_CLARINET 2
#define V_VIOLA 3
#define V_MAIN_LEAD 5
#define V_MAIN_BASS 6

#define VOICE 6

#define N_OSCS_PER_LAYER 6
#define N_OSCS (N_OSCS_PER_LAYER*DURATION)
struct Osc oscs[N_OSCS];

#define ALPHA (0.1)

#if VOICE == V_MAIN_BASS
#define ALPHA (0.01)
#endif

#if VOICE == V_BASS_CLARINET
#define ALPHA (0.01)
#endif

#if VOICE == V_VIOLA
#define ALPHA (0.01)
#endif

// in samples -- set to zero for off
#define LESLIE_PERIOD 0 // 4096
// in samples
#define LESLIE_DEPTH 36
#define LESLIE_SAMPLES (LESLIE_DEPTH*2)
float sine_decimal(float v) {
  return sin((v+0.5)*M_PI*2);
}

void init_oscs(int cycles, float adjustment) {
  int offset = (octaver.cycles % DURATION) * N_OSCS_PER_LAYER;
  
  if (VOICE == V_SIMPLE_SINE) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 1,
	     /*is_square=*/ FALSE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
  } else if (VOICE == V_SIMPLE_SQUARE) {
    osc_init(&oscs[offset],
	     cycles,
	     adjustment,
	     /*vol=*/ 1,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 3,
	     /*mod=*/ 2);
  } else if (VOICE == V_BASS_CLARINET) {
    osc_init(&oscs[offset],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.4,
	     /*is_square=*/ FALSE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.25,
	     /*cycle=*/ 0.25,
	     /*mod=*/ 2);
    osc_init(&oscs[offset + 1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.7,
	     /*is_square=*/ FALSE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.125,
	     /*cycle=*/ 0.125,
	     /*mod=*/ 2);
  } else if (VOICE == V_VIOLA) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.5,
	     /*is_square=*/ FALSE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 0.5,
	     /*mod=*/ 4);
  } else if (VOICE == V_MAIN_LEAD) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 1 - (duration_val * 200),
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/ duration_val * 100,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 9000,
	     /*lfo_vol=*/ 0.5,
	     /*speed=*/ 0.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/ duration_val * 10,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 1234,
	     /*lfo_vol=*/ 1,
	     /*speed=*/ 2,
	     /*cycle=*/ 1,
	     /*mod=*/ 0);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/ duration_val * 10,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 995,
	     /*lfo_vol=*/ 1,
	     /*speed=*/ 3,
	     /*cycle=*/ 1,
	     /*mod=*/ 0);
    osc_init(&oscs[offset+4],
	     cycles,
	     adjustment,
	     /*vol=*/ duration_val * 100,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 15234,
	     /*lfo_vol=*/ 1,
	     /*speed=*/ 2.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 0);
    osc_init(&oscs[offset+5],
	     cycles,
	     adjustment,
             /*vol=*/ duration_val * 100,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 14267,
	     /*lfo_vol=*/ 1,
	     /*speed=*/ 3.5,
	     /*cycle=*/ 1,
	     /*mod=*/ 0);
  } else if (VOICE == V_MAIN_BASS) {
    osc_init(&oscs[offset+0],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.3,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.9,
	     /*cycle=*/ 0.0625,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+1],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.05,
	     /*is_square=*/ FALSE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 0.83,
	     /*cycle=*/ 0.25,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+2],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.05 + duration_val * 12,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 2,
	     /*cycle=*/ 0.5,
	     /*mod=*/ 2);
    osc_init(&oscs[offset+3],
	     cycles,
	     adjustment,
	     /*vol=*/ 0.05 + duration_val * 12,
	     /*is_square=*/ TRUE,
	     /*lfo_speed=*/ 0,
	     /*lfo_vol=*/ 0,
	     /*speed=*/ 1,
	     /*cycle=*/ 3,
	     /*mod=*/ 0);
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
  if (osc->is_square) {
    val = val > 0 ? 1 : -1;
    val *= (osc->total_amplitude / osc->samples);
  }

  osc->pos += osc->speed;
  val = osc->amp * val * osc->polarity * osc->vol;

  if (osc->lfo_vol > 0) {
    //printf("%.2f %.2f\n", osc->lfo_pos, sine_decimal(osc->lfo_pos));
    val =
      val*(sine_decimal(osc->lfo_pos)+1)*osc->lfo_vol +
      val*(1-osc->lfo_vol);    
    osc->lfo_pos += (1/osc->lfo_speed);
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
  set_hist(s);
  update_duration(s);

  octaver.samples_since_last_crossing++;
  octaver.samples_since_attack_began++;

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

      if (octaver.rough_input_period > RANGE_HIGH &&
          octaver.rough_input_period < RANGE_LOW) {
        init_oscs(octaver.cycles, adjustment);
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
  return val * VOLUME;
}

int main(void) {
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
  for(int i = 0; i < numDevices; i++) {
    deviceInfo = Pa_GetDeviceInfo(i);
    printf("device[%d]: %s\n", i, deviceInfo->name);
    if (strcmp(deviceInfo->name, "USB Audio Device") == 0) {
      best_audio_device_index = i;
    }
  }
  
  if (best_audio_device_index == -1) {
    best_audio_device_index = Pa_GetDefaultInputDevice();
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
		      paClipOff,      /* we won't output out of range samples so don't bother clipping them */
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
    
    for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
      float sample = sampleBlock[i];
      float val = update(sample);
      output += ALPHA * (val - output);
      float sample_out = output * VOLUME / ALPHA ; // makeup gain
      
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
      
      sampleBlock[i] = sample_out;
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

