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
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>


#define SAMPLE_RATE       (44100)    // if you change this, change MIN/MAX_INPUT_PERIOD too
#define FRAMES_PER_BUFFER   (128)    // this is low, to minimize latency

/* Select sample format. */
#define PA_SAMPLE_TYPE  paFloat32
#define SAMPLE_SIZE (4)
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"

#define MIN_INPUT_PERIOD (14)
#define MAX_INPUT_PERIOD (71)

#define BOOL char
#define TRUE 1
#define FALSE 0

/*******************************************************************/

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void attempt(OSStatus result, char* errmsg) {
  if (result != noErr) {
    die(errmsg);
  }
}

#define PACKET_BUF_SIZE (3+64) /* 3 for message, 32 for structure vars */
void send_midi(char actionType, int noteNo, int v, MIDIEndpointRef endpoint) {
  //printf("Sending: %x / %d / %d\n", actionType, noteNo, v);

  Byte buffer[PACKET_BUF_SIZE];
  Byte msg[3];
  msg[0] = actionType;
  msg[1] = noteNo;
  msg[2] = v;

  MIDIPacketList *packetList = (MIDIPacketList*) buffer;
  MIDIPacket *curPacket = MIDIPacketListInit(packetList);
  if (!curPacket) {
    die("packet list allocation failed");
  }
  MIDIPacketListAdd(packetList,
                            PACKET_BUF_SIZE,
                            curPacket,
                            AudioGetCurrentHostTime(),
                            3,
                    msg);

  attempt(MIDIReceived(endpoint, packetList), "error sending midi");
}

int remap(int noteNo) {
  // Whistling is too high pitched.
  return noteNo - 24;
}

void midi_on(int noteNo, MIDIEndpointRef endpoint) {
  noteNo = remap(noteNo);
  printf("on: %d\n", noteNo);
  send_midi(0x90, noteNo, 100, endpoint);
}

void midi_off(int noteNo, MIDIEndpointRef endpoint) {
  noteNo = remap(noteNo);
  printf("off: %d\n", noteNo);
  send_midi(0x80, noteNo, 0, endpoint);
}

void midi_bend(int bend, MIDIEndpointRef endpoint) {
  // We need to send bend as two 7-bit numbers, first the LSB then the MSB.
  // Since we only ever bend by 0.5 we're only using 13 bits of range, but
  // we can ignore that.
  int lsb = bend & 0b00000001111111;
  int msb = (bend & 0b11111110000000) >> 7;

  send_midi(0xE0, lsb, msb, endpoint);
}

void determine_note(float input_period_samples, int current_note,
                    int* chosen_note, int* chosen_bend) {
  // input_period is in samples, convert it to hz
  float input_period_hz = SAMPLE_RATE/input_period_samples;

  float midi_note = 69 + 12 * log2(input_period_hz/440);

  //if (current_note != -1) {
  //  printf("delta: %.2f\n", (midi_note - current_note));
  //}

  if (current_note != -1 &&
      midi_note - current_note < 2 &&
      current_note - midi_note < 2) {
    // We can keep the same note and just bend our way there.
    *chosen_note = current_note;
  } else {
    // No current note, or too far to bend.  Take the closest midi note as a
    // best guess.
    *chosen_note = (int)(midi_note + 0.5);
  }

  // Between -2 and 2
  float rough_bend = (midi_note - *chosen_note);
  //printf("rough bend: %.2f\n", rough_bend);

  // The full range of pitch bend is from -2 to 2 and is expressed by 0 to
  // 16,383 (2^14 - 1).  We use the whole range.
  *chosen_bend = (int)((1 + rough_bend/2) * 8192 - 0.5);

  //printf("%.2f samples   %.2fhz  note=%d  bend=%.4f  intbend=%d\n", input_period_samples, input_period_hz,
  // *chosen_note, rough_bend, *chosen_bend);
}

float sine(float v) {
  return sin(v*M_PI*2);
}

int main(void);
int main(void)
{
    PaStreamParameters inputParameters;
    PaStream *stream = NULL;
    PaError err;
    const PaDeviceInfo* inputInfo;
    float *sampleBlock = NULL;
    int numBytes;

    MIDIClientRef midiclient;
    attempt(MIDIClientCreate(CFSTR("whistle-pitch"),
                             NULL, NULL, &midiclient),
            "creating OS-X MIDI client object." );

    MIDIEndpointRef endpoint;
    attempt(MIDISourceCreate(midiclient,
                             CFSTR("whistle-pitch"),
                             &endpoint),
            "creating OS-X virtual MIDI source." );

    float history[MAX_INPUT_PERIOD];
    for (int i = 0; i < MAX_INPUT_PERIOD; i++) {
      history[i] = 0;
    }
    int history_pos = 0;

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

    /* -- setup -- */

    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,
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

    float samples_since_last_crossing = 0;
    BOOL positive = TRUE;
    float previous_sample = 0;
    float instantaneous_period = 40;
    float recent_period = -1;

    float rms_energy = 0;

    int current_note = -1;
    int good_samples = 0;
    int bad_samples = 0;

    while(TRUE) {
      err = Pa_ReadStream( stream, sampleBlock, FRAMES_PER_BUFFER );
      if( err ) goto xrun;


      for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
        float sample = sampleBlock[i];
        rms_energy += sample*sample;
        history[history_pos] = sample;
        history_pos = (history_pos + 1) % MAX_INPUT_PERIOD;

        samples_since_last_crossing++;

        int chosen_note = -1;
        int chosen_bend = -1;

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
            instantaneous_period = samples_since_last_crossing;

            if (instantaneous_period > MIN_INPUT_PERIOD &&
                instantaneous_period < MAX_INPUT_PERIOD) {
              float sample_max = 0;
              float sample_max_loc = -1000;
              float sample_min = 0;
              float sample_min_loc = -1000;
              for (int j = 0; j < instantaneous_period; j++) {
                float histval = history[(MAX_INPUT_PERIOD + history_pos - j) %
                                        MAX_INPUT_PERIOD];
                if (histval < sample_min) {
                  sample_min = histval;
                  sample_min_loc = j;
                } else if (histval > sample_max) {
                  sample_max = histval;
                  sample_max_loc = j;
                }
              }
              /**
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
              error += (sample_max_loc - instantaneous_period/4)*(sample_max_loc - instantaneous_period/4);
              error += (sample_min_loc - 3*instantaneous_period/4)*(sample_min_loc - 3*instantaneous_period/4);

              BOOL ok = TRUE;
              float rough_period_rms_energy = rms_energy/instantaneous_period;

              //printf("rough_period_rms_energy: %.9f\n", rough_period_rms_energy);
              if (rough_period_rms_energy < 0.0000001) {
                //if (current_note != -1) {
                //  printf("low energy (%.6f)\n", rough_period_rms_energy);
                //}
                ok = FALSE;
              } else if (error > 5 && rough_period_rms_energy < 0.000001) {
                //printf("high error (%.2f, %.6f)\n", error, rough_period_rms_energy);
                ok = FALSE;
              } else if (recent_period > 0 &&
                         (instantaneous_period/recent_period > 1.2 ||
                          instantaneous_period/recent_period < 0.8)) {
                //printf("too much shift\n");
                ok = FALSE;
              }

              if (ok) {
                if (recent_period < 0) {
                  recent_period = instantaneous_period;
                } else {
                  recent_period = (0.9*recent_period +
                                   0.1*instantaneous_period);
                }

                determine_note(recent_period, current_note, &chosen_note, &chosen_bend);
              } else {
                chosen_note = -1;
              }
            } else {
              chosen_note = -1;
            }

            BOOL is_on = current_note != -1;

            if (chosen_note == -1) {
              good_samples = 0;
              bad_samples++;
            } else {
              good_samples++;
              bad_samples = 0;
            }

            BOOL should_on = is_on ? bad_samples < 3 : good_samples > 1;

            if (should_on && chosen_note == -1) {
              // We can't detect a pitch right now, but we'd like to stay on
              // because we think this is probably a momentary blip.
            } else {
              BOOL note_changed = (current_note != chosen_note);

              //printf("cur=%d, chos=%d, is_on=%d, should_on=%d, note_changed=%d\n",
              //       current_note, chosen_note, is_on, should_on, note_changed);

              if (!should_on) {
                recent_period = -1;
              }

              if (is_on && note_changed) {
                midi_off(current_note, endpoint);
                current_note = -1;
              }

              if (should_on) {
                if (note_changed) {
                  midi_on(chosen_note, endpoint);
                }
                midi_bend(chosen_bend, endpoint);
                current_note = chosen_note;
              }
            }

            positive = FALSE;

            samples_since_last_crossing = -adjustment;
            rms_energy = 0;
          }
        } else {
          if (sample > 0) {
            positive = TRUE;
          }
        }
        previous_sample = sample;
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

