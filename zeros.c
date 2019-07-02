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

// derived from paex_read_write_wire.c by Jeff Kaufman 2019-07

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portaudio.h"

#define SAMPLE_RATE       (44100)
#define FRAMES_PER_BUFFER   (8)      // this is absurdly low, to minimize latency

/* Select sample format. */
#define PA_SAMPLE_TYPE  paFloat32
#define SAMPLE_SIZE (4)
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"

#define BOOL char
#define TRUE 1
#define FALSE 0

/*******************************************************************/

float sine(float v) {
  return sin(v*M_PI*2);
}

int main(void);
int main(void)
{
    PaStreamParameters inputParameters, outputParameters;
    PaStream *stream = NULL;
    PaError err;
    const PaDeviceInfo* inputInfo;
    const PaDeviceInfo* outputInfo;
    float *sampleBlock = NULL;
    int numBytes;

    printf("zeros.c\n"); fflush(stdout);

    err = Pa_Initialize();
    if( err != paNoError ) goto error2;

    inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    printf( "Input device # %d.\n", inputParameters.device );
    inputInfo = Pa_GetDeviceInfo( inputParameters.device );
    printf( "   Name: %s\n", inputInfo->name );
    printf( "     LL: %.2fms\n", inputInfo->defaultLowInputLatency*1000 );

    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    printf( "Output device # %d.\n", outputParameters.device );
    outputInfo = Pa_GetDeviceInfo( outputParameters.device );
    printf( "   Name: %s\n", outputInfo->name );
    printf( "     LL: %.2fms\n", outputInfo->defaultLowOutputLatency*1000 );

    inputParameters.channelCount = 1;  // mono
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = inputInfo->defaultLowInputLatency ;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    outputParameters.channelCount = 1; // mono
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    /* -- setup -- */

    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              NULL, /* no callback, use blocking API */
              NULL ); /* no callback, so no callback userData */
    if( err != paNoError ) goto error2;

    float min_samples_per_crossing = SAMPLE_RATE / 2793.826; // F7
    float max_samples_per_crossing = SAMPLE_RATE / 622.2540; //  Eb5

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

    float samples_since_last_crossing = 0;
    BOOL positive = TRUE;
    float previous_sample = 0;
    float samples_per_crossing = 40;
    float energy = 0;
    float sample_energy = 0;

    float last_out = 0;

    int mod_32_loc = 0;  // five octaves down
    int mod_16_loc = 0;  // four octaves down
    int mod_8_loc  = 0;  // three octaves down
    int mod_12_loc = 0;  // three and a half octaves down

    while(TRUE) {
      // You may get underruns or overruns if the output is not primed by PortAudio.
      err = Pa_WriteStream( stream, sampleBlock, FRAMES_PER_BUFFER );
      if( err ) goto xrun;
      err = Pa_ReadStream( stream, sampleBlock, FRAMES_PER_BUFFER );
      if( err ) goto xrun;

      for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
        float sample = sampleBlock[i];

        energy += sample > 0 ? sample : -sample;
        samples_since_last_crossing++;

        if (positive) {
          if (sample < 0) {
            /**
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
            float first_negative = sample;
            float last_positive = previous_sample;
            float adjustment = first_negative / (last_positive - first_negative);
            samples_since_last_crossing -= adjustment;
            samples_per_crossing = samples_since_last_crossing;

            positive = FALSE;

            sample_energy = energy / samples_per_crossing;

            samples_since_last_crossing = -adjustment;
            energy = 0;

            mod_32_loc = (mod_32_loc + 1) % 32;
            mod_16_loc = (mod_16_loc + 1) % 16;
            mod_12_loc = (mod_12_loc + 1) % 12;
            mod_8_loc  = (mod_8_loc  + 1) %  8;
          }
        } else {
          if (sample > 0) {
            positive = TRUE;
          }
        }
        previous_sample = sample;

        /**
         * now synthesize
         *
         * samples_since_last_crossing / samples_per_crossing is
         * approximately [0-1] and is our phase angle if we want to synthesize a
         * plain sine.
         */

        float mod_32_note = sine((samples_since_last_crossing + (samples_per_crossing * mod_32_loc)) /
                                 (samples_per_crossing * 32));

        float mod_16_note = sine((samples_since_last_crossing + (samples_per_crossing * mod_16_loc)) /
                                 (samples_per_crossing * 16));

        float mod_12_note = sine((samples_since_last_crossing + (samples_per_crossing * mod_12_loc)) /
                                 (samples_per_crossing * 12));

        float mod_8_note = sine((samples_since_last_crossing + (samples_per_crossing * mod_8_loc)) /
                                (samples_per_crossing * 8));

        float val = 0;
        if (min_samples_per_crossing <
            samples_per_crossing <
            max_samples_per_crossing) {
          float e = sample_energy - 0.0003;
          if (e > .1) {
            e = .1;
          }
          if (e > 0) {
            val = 5 * e * (mod_32_note + mod_16_note*.6 + mod_12_note*.1 + mod_8_note*.3);
          }
        }

        // This averaging makes the output mostly continuous.  It's kind of a
        // low-pass filter, which keeps us from getting pops and crackles as
        // frequency changes would normally give us non-continuous sine waves.
        val = (val + 15*last_out) / 16;

        sampleBlock[i] = val;
        last_out = val;
      }
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

