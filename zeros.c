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

#define GATE (15)
#define RANGE_HIGH (14)
#define RANGE_LOW (71)
#define ACCURACY (1)
#define ATTACK_SPEED (3.5)
#define RELEASE_SPEED (5.5)
#define OCTAVE (2)
#define SLIDE (4)
#define ALPHA (0.1)
#define VOLUME (0.9)
#define MAX_AMPLITUDE (0.3)

#define HISTORY_LENGTH (512)

#define BOOL char
#define TRUE 1
#define FALSE 0

/*******************************************************************/

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

float sine_decimal(float x) {
  x += 0.5;
  x = x - (int)x;
  return sin((x)*M_PI*2);
}

float saw_decimal(float x) {
  x += 0.5;
  x = x - (int)x;
  return x*2 - 1;
}

struct Osc {
  BOOL is_saw;
  float tuning;
  float pos;
  float vol;
  float base_period;
  float decay_rate;
  BOOL is_on;
  float decay_vol;
};

#define V_SINE 0
#define V_SINE_SAW 1
#define V_SUPERSAW 2
#define V_SINE_1_3 3

#define VOICE V_SINE_1_3

#if VOICE == V_SINE
#define N_OSCS (1)
struct Osc oscs[] =
  {
   {FALSE, 1, 0, 1, 40, 0.999, FALSE, 0},
  };
#endif

#if VOICE == V_SINE_SAW
#define N_OSCS (2)
struct Osc oscs[] =
  {
   {FALSE, 1, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 1, 0, 0.1, 40, 0.999, FALSE, 0},
  };
#endif

#if VOICE == V_SUPERSAW
#define N_OSCS (2)
struct Osc oscs[] =
  {
   {TRUE, 1.003, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 1.002, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 1.001, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 1.000, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 0.999, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 0.998, 0, 1, 40, 0.999, FALSE, 0},
   {TRUE, 0.997, 0, 1, 40, 0.999, FALSE, 0},
  };
#endif

#if VOICE == V_SINE_1_3
#define N_OSCS (2)
struct Osc oscs[] =
  {
   {FALSE, 1, 0, 1, 40, 0.999, FALSE, 0},
   {FALSE, 3, 0, 0.1, 40, 0.999, FALSE, 0},
  };
#endif


float osc_next(struct Osc* osc, float goal_period, float ramp) {
  BOOL was_on = osc->is_on;
  osc->is_on = !!goal_period && ramp > 0.9;

  if (osc->is_on) {
    osc->decay_vol = 1;

    if (was_on) {
      // Already playing, move smoothly
      osc->base_period = ((SLIDE-1)*osc->base_period + goal_period)/SLIDE;
    } else {
      // Note is starting, go directly there.
      osc->pos = 0;
      osc->base_period = goal_period;
    }
  } else {
    osc->decay_vol *= osc->decay_rate;
  }

  float val = (osc->is_saw ? saw_decimal : sine_decimal)(osc->pos);
  val *= osc->decay_vol * osc->vol;
  osc->pos += osc->tuning/(osc->base_period*OCTAVE);
  return val;
}

struct pitch_detector {
  float hist[HISTORY_LENGTH];
  int hist_pos;
  float samples_since_last_crossing;
  float samples_since_attack_began;
  BOOL positive;
  float previous_sample;
  float rough_input_period;

  // -1: ramp down
  //  1: ramp up
  float ramp_direction;

  float ramp;

  float current_volume;
  float target_volume;

  int on_count;
  int off_count;

  float amp;

  BOOL is_on;

  float gate;
  int accuracy;
  float attack_speed;
  float release_speed;
};

struct pitch_detector pd;

void populate_pd() {
  for (int i = 0; i < HISTORY_LENGTH; i++) {
    pd.hist[i] = 0;
  }
  pd.hist_pos = 0;

  pd.samples_since_last_crossing = 0;
  pd.samples_since_attack_began = 0;

  pd.positive = TRUE;
  pd.previous_sample = 0;
  pd.rough_input_period = 40;

  // -1: ramp down
  //  1: ramp up
  pd.ramp_direction = -1;

  pd.ramp = 0;

  pd.current_volume = 0;
  pd.target_volume = 0;

  pd.on_count = 0;
  pd.off_count = 0;

  pd.amp = 0;

  pd.is_on = FALSE;

  pd.gate = 1/exp(GATE);
  pd.accuracy = ACCURACY;
  pd.attack_speed = 1/exp(ATTACK_SPEED);
  pd.release_speed = 1/exp(RELEASE_SPEED);
}

float update(float s) {
  pd.hist[pd.hist_pos++] = s;
  pd.hist_pos = pd.hist_pos % RANGE_LOW;

  pd.samples_since_last_crossing++;
  pd.samples_since_attack_began++;

  if (pd.positive) {
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
      float last_positive = pd.previous_sample;
      float adjustment = first_negative / (last_positive - first_negative);
      if (isnan(adjustment)) {
        adjustment = 0;
      }
      pd.samples_since_last_crossing -= adjustment;
      pd.rough_input_period = pd.samples_since_last_crossing;

      BOOL ok = TRUE;
      float sample_max = 0;
      float sample_min = 0;
      
      if (pd.rough_input_period > RANGE_HIGH &&
          pd.rough_input_period < RANGE_LOW) {
        float sample_max_loc = -1000;
        float sample_min_loc = -1000;
        for (int j = 0; j < pd.rough_input_period; j++) {
          float histval = pd.hist[(RANGE_LOW + pd.hist_pos - j) %
				    RANGE_LOW];
          if (histval < sample_min) {
            sample_min = histval;
            sample_min_loc = j;
          } else if (histval > sample_max) {
            sample_max = histval;
            sample_max_loc = j;
          }
        }

	//printf("raw amplitude: %.2f\n", sample_max - sample_min);
	float amplitude = fmin(sample_max - sample_min, MAX_AMPLITUDE);
	// low-pass it
	pd.amp += 0.1 * (amplitude - pd.amp);
        
        /*
         * With a perfect sine wave centered on 0 and lined up with our
         * sampling we'd expect:
         *
         * history[now] == 0                         (verified)
         * history[now - input_period] == 0          (verified)
         * history[now - input_period/2] == 0        (very likely)
         * history[now - input_period/4] == max
         * history[now - 3*input_period/4] == min
         *
         * Let's check if that's right, to within a sample or so.
         */
        float error = 0;
        // You could make these better by finding the second
        // highest/lowest values to figure out which direction the peak
        // is off in, and adjusting.  Or construct a whole sine wave and
        // see how well it fits history.
        error += (sample_max_loc - pd.rough_input_period/4)*(sample_max_loc - pd.rough_input_period/4);
        error += (sample_min_loc - 3*pd.rough_input_period/4)*(sample_min_loc - 3*pd.rough_input_period/4);

	if (amplitude < 0.001) {
	  ok = FALSE;
	} else if (amplitude < 0.01 && error > 2) {
	  ok = FALSE;
	}
      } else {
        ok = FALSE;
      }

      // If we're off, require `accuracy` periods before turning on.  If we're fully on require
      // `accuracy` periods before turning off.  If we're in between, ignore accuracy and just
      // go with our current best guess.

      BOOL currently_on = pd.ramp_direction > 0 && pd.ramp > .5;
      BOOL currently_off = pd.ramp_direction < 0 && pd.ramp < 0.0001;

      if (pd.is_on && currently_off) {
        pd.is_on = FALSE;
      } else if (!pd.is_on && currently_on) {
        pd.is_on = TRUE;
      }

      if (currently_on) {
        pd.on_count = 0;
        if (ok) {
          pd.off_count = 0;
        } else {
          pd.off_count++;
        }
      } else if (currently_off) {
        pd.off_count = 0;
        if (!ok) {
          pd.on_count = 0;
        } else {
          pd.on_count++;
        }
      } else {
        pd.off_count = 0;
        pd.on_count = 0;
      }

      if (pd.on_count >= pd.accuracy) {
        pd.ramp_direction = 1;
        pd.samples_since_attack_began = 0;
	pd.amp = 0;
      }

      if (ok) {
        pd.target_volume = fmaxf(sample_max, -sample_min);
        if (pd.target_volume > 1) {
          pd.target_volume = 1;
        }
      }

      if (pd.off_count >= pd.accuracy) {
        pd.ramp_direction = -1;
      }

      pd.positive = FALSE;

      pd.samples_since_last_crossing = -adjustment;
    }
  } else {
    if (s > 0) {
      pd.positive = TRUE;
    }
  }
  
  pd.previous_sample = s;

  if (pd.ramp_direction > 0) {
    if (pd.ramp < 0.00001) {
      pd.ramp = 0.00001;
    }
    pd.ramp *= (1 + pd.attack_speed);
  } else if (pd.ramp_direction < 0) {
    if (pd.samples_since_attack_began < 44000) {
      pd.ramp *= (1 - pd.release_speed);
    } else {
      pd.ramp *= (1 - (pd.release_speed * 0.1));
    }
  }

  if (pd.ramp > 1) {
    pd.ramp = 1;
  } else if (pd.ramp < 0.000001) {
    pd.ramp = 0;
  }

  return pd.is_on ? pd.rough_input_period : 0;
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

  populate_pd();

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

  float output = 0;
  while(TRUE) {
    err = Pa_ReadStream( stream, sampleBlock, FRAMES_PER_BUFFER );
    if (err & paInputOverflow) {
      printf("ignoring input undeflow\n");
    } else if( err ) goto xrun;
    
    float debug_max_output = 0;
    for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
      float sample = sampleBlock[i];
      
      float goal_period = update(sample);
      float vol = 0;
      float val = 0;
      for (int j = 0; j < N_OSCS; j++) {
	val += osc_next(&oscs[j], goal_period, pd.ramp) / 2;
	vol += oscs[j].vol;
      }
      if (vol < 0.000001) {
	val = 0;
      } else {
	val = val/vol;
      }

      output += ALPHA * (val - output);
      sampleBlock[i] = output * VOLUME * pd.amp / ALPHA ; // makeup gain
      debug_max_output = fmax(debug_max_output, sampleBlock[i]);
    }

    //printf("debugmax_output: %.2f\n", debug_max_output);

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

