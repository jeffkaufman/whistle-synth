// Whistle synthesizer: listens to a microphone, works out what note is being
// whistled, and plays a synth lead following it.
//
// Structure:
//
//   pitch.c   input signal -> hints about what the player is doing
//   synth.c   hints -> sound
//   engine.c  wires those together with volume
//   zeros2.c  audio device, control files, main
//
// The detector and the synth do not share state.  The synth free-runs and
// never reads the input, so a shaky detector costs a wrong hint -- which the
// synth can smooth, hold, or fade out -- rather than putting the input's own
// noise into the output.
//
// Audio I/O derived from paex_read_write_wire.c (PortAudio, Ross Bencina and
// Phil Burk).  Jeff Kaufman 2019-07, 2021-06, rewritten 2026-08.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "portaudio.h"
#ifdef __APPLE__
#include "pa_mac_core.h"
#endif

#include "engine.h"
#include "selftest.h"

// The Scarlett's native rate.  The pipeline is a fixed number of *frames*
// deep, so a faster rate is fewer milliseconds.  96k and 192k were measured
// and bought only another 0.4ms for 2-4x the CPU.
#define SAMPLE_RATE 48000

// How much buffering to ask PortAudio for.  Asking for less gets a smaller
// CoreAudio buffer but measures exactly the same round trip, and small enough
// buffers stall the callback outright.
#define SUGGESTED_LATENCY 0.001

#define USB_SOUND_CARD_PREFIX "Scarlett"

/* ------------------------------------------------------------ controls --- */

// Voice, volume, gate and fifth are read from files so they can be changed
// while running (see kbd.py).  The reader thread only ever publishes an int;
// the audio thread picks it up and applies it.  Nothing but the audio thread
// touches the engine, which is what keeps this safe without a lock.

struct Control {
  const char* purpose;
  const char* fname;
  FILE* file;
  int reported;             // last value we logged
  volatile int published;   // written by the reader, read by the audio thread
  int applied;              // last value the audio thread acted on
};

static struct Control voice_control = { .purpose = "voice", .published = 2 };
static struct Control volume_control = { .purpose = "volume", .published = 5 };
static struct Control gate_control = { .purpose = "gate", .published = 5 };
// 0 or 1: whether every voice is dropped a just fifth.  See synth_set_fifth.
static struct Control fifth_control = { .purpose = "fifth", .published = 0 };
// 0 or 1: whether a note the player holds outlives the breath that made it.
// See synth_set_sustain.
static struct Control sustain_control = { .purpose = "sustain", .published = 0 };

static struct Control* controls[] = {
  &voice_control, &volume_control, &gate_control, &fifth_control,
  &sustain_control };
#define N_CONTROLS ((int)(sizeof(controls)/sizeof(controls[0])))

static struct Engine engine;

// Non-NULL only in self-test mode, where we play a synthetic whistle instead
// of the synth and record what comes back.  See selftest.h.
static struct SelfTest* self_test;

static void die(const char* message) {
  fprintf(stderr, "%s\n", message);
  exit(-1);
}

static int read_number(FILE* file) {
  char buf[16];
  rewind(file);
  size_t read = fread(buf, 1, sizeof(buf) - 1, file);
  buf[read] = '\0';
  return atoi(buf);
}

// The device index may be given as a literal number or as a path to a file
// containing one.  The controls have to be files because they are re-read
// while running, but the device index is read once at startup, and making
// someone write it to a file first is just friction.
static int read_number_arg(const char* arg) {
  const char* p = arg;
  if (*p == '-' || *p == '+') {
    p++;
  }
  bool numeric = *p != '\0';
  for (; *p; p++) {
    if (*p < '0' || *p > '9') {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    return atoi(arg);
  }

  FILE* file = fopen(arg, "r");
  if (!file) {
    fprintf(stderr, "can't open device index file: %s\n", arg);
    fprintf(stderr, "  (or pass the number directly, e.g. 0)\n");
    exit(-1);
  }
  int value = read_number(file);
  fclose(file);
  return value;
}

static void* read_controls(void* ignored) {
  (void)ignored;

  for (int i = 0; i < N_CONTROLS; i++) {
    controls[i]->file = fopen(controls[i]->fname, "r");
    if (!controls[i]->file) {
      perror("can't open control file");
      fprintf(stderr, "  in: %s\n", controls[i]->fname);
      exit(-1);
    }
  }

  while (1) {
    for (int i = 0; i < N_CONTROLS; i++) {
      int value = read_number(controls[i]->file);
      if (value != controls[i]->reported) {
        printf("%s: %d -> %d", controls[i]->purpose,
               controls[i]->reported, value);
        if (controls[i] == &voice_control) {
          printf(" (%s)", engine_voice_name(value));
        }
        printf("\n");
        fflush(stdout);
        controls[i]->reported = value;
        controls[i]->published = value;
      }
    }
    usleep(50000);
  }
  return NULL;
}

// Called from the audio thread, once per buffer rather than once per sample.
static void apply_controls(void) {
  if (voice_control.published != voice_control.applied) {
    voice_control.applied = voice_control.published;
    engine_set_voice(&engine, voice_control.applied);
  }
  if (volume_control.published != volume_control.applied) {
    volume_control.applied = volume_control.published;
    engine_set_volume(&engine, volume_control.applied);
  }
  if (gate_control.published != gate_control.applied) {
    gate_control.applied = gate_control.published;
    engine_set_gate(&engine, gate_control.applied);
  }
  if (fifth_control.published != fifth_control.applied) {
    fifth_control.applied = fifth_control.published;
    engine_set_fifth(&engine, fifth_control.applied);
  }
  if (sustain_control.published != sustain_control.applied) {
    sustain_control.applied = sustain_control.published;
    engine_set_sustain(&engine, sustain_control.applied);
  }
}

/* --------------------------------------------------------------- audio --- */

static int input_channels = 1;
static int output_channels = 1;

// The callback can't printf, so it counts and main reports.
static volatile int xruns = 0;

// Set by ctrl-C during a recording.  A take is only worth making if stopping
// it keeps it: the interesting thing usually happens partway through, and
// waiting out the rest of the script to get the file is a good way to lose
// the take you wanted.  Only installed for the recording modes -- the live
// instrument should still die immediately when it is interrupted.
static volatile sig_atomic_t interrupted = 0;

static void on_interrupt(int sig) {
  (void)sig;
  interrupted = 1;
  // A second ctrl-C kills it outright, so a wedged stream is still escapable.
  signal(SIGINT, SIG_DFL);
}

static int audio_callback(const void* input_buffer,
                          void* output_buffer,
                          unsigned long frames,
                          const PaStreamCallbackTimeInfo* time_info,
                          PaStreamCallbackFlags status_flags,
                          void* user_data) {
  (void)time_info;
  (void)user_data;

  const float* in = (const float*)input_buffer;
  float* out = (float*)output_buffer;

  if (status_flags & (paInputOverflow | paOutputUnderflow)) {
    xruns++;
  }

  // The self-test sets the voice itself and runs without the control thread.
  if (!self_test) {
    apply_controls();
  }

  for (unsigned long i = 0; i < frames; i++) {
    float in_main = in ? in[i * input_channels] : 0;
    float out_main = engine_process(&engine, in_main);

    if (self_test) {
      // Normally play the test whistle rather than the synth: the microphone
      // is listening to the speaker, so playing the synth would feed it back
      // into the detector.  MONITOR is the exception -- there the player
      // needs to hear the synth to provoke what we're hunting for, and they
      // are wearing the headphones rather than pointing a mic at them.
      float stimulus = selftest_stimulus(self_test);
      float played = self_test->mode == SELFTEST_MONITOR ? out_main : stimulus;
      selftest_record(self_test, stimulus, in_main, out_main);
      out[i * output_channels] = played;
      if (output_channels > 1) {
        out[i * output_channels + 1] = played;
      }
      continue;
    }

    out[i * output_channels] = out_main;
    if (output_channels > 1) {
      out[i * output_channels + 1] = out_main;
    }
  }
  return paContinue;
}

// The Nth device whose name starts with USB_SOUND_CARD_PREFIX and which has
// at least the requested channels, or -1.
static int find_device(int device_index, int min_input, int min_output) {
  int seen = 0;
  for (int i = 0; i < Pa_GetDeviceCount(); i++) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info->maxInputChannels < min_input ||
        info->maxOutputChannels < min_output) {
      continue;
    }
    if (strncmp(USB_SOUND_CARD_PREFIX, info->name,
                strlen(USB_SOUND_CARD_PREFIX)) == 0) {
      if (seen == device_index) {
        return i;
      }
      seen++;
    }
  }
  return -1;
}

static int start_audio(int device_index) {
  PaStream* stream = NULL;
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    goto error;
  }

  int device_count = Pa_GetDeviceCount();
  if (device_count < 0) {
    die("no devices found");
  }
  for (int i = 0; i < device_count; i++) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    printf("device[%d]: %s (in: %d, out: %d)\n", i, info->name,
           info->maxInputChannels, info->maxOutputChannels);
  }

  // On the Pi the USB sound card does both directions, which is what we want:
  // one clock for both.  On a Mac the built-in mic and speakers are separate
  // devices, so fall back to spanning two, which PortAudio is happy to do.
  int input_device = find_device(device_index, 1, 1);
  int output_device = input_device;
  if (input_device == -1) {
    input_device = find_device(device_index, 1, 0);
    output_device = find_device(device_index, 0, 1);
  }
  if (input_device == -1 || output_device == -1) {
    if (device_index != 0) {
      die("no good device found");
    }
    printf("falling back to default\n");
    if (input_device == -1) {
      input_device = Pa_GetDefaultInputDevice();
    }
    if (output_device == -1) {
      output_device = Pa_GetDefaultOutputDevice();
    }
  }
  if (input_device == paNoDevice || output_device == paNoDevice) {
    die("no input or no output device available");
  }

  const PaDeviceInfo* input_info = Pa_GetDeviceInfo(input_device);
  const PaDeviceInfo* output_info = Pa_GetDeviceInfo(output_device);
  printf("input:  #%d %s (%d ch)\n", input_device, input_info->name,
         input_info->maxInputChannels);
  printf("output: #%d %s (%d ch)\n", output_device, output_info->name,
         output_info->maxOutputChannels);

  // Only channel 0 is used, but a device may insist on handing us two.
  input_channels = input_info->maxInputChannels >= 2 ? 2 : 1;
  output_channels = output_info->maxOutputChannels >= 2 ? 2 : 1;

  PaStreamParameters input_params = {
    .device = input_device,
    .channelCount = input_channels,
    .sampleFormat = paFloat32,
    .suggestedLatency = SUGGESTED_LATENCY,
    .hostApiSpecificStreamInfo = NULL,
  };
  PaStreamParameters output_params = {
    .device = output_device,
    .channelCount = output_channels,
    .sampleFormat = paFloat32,
    .suggestedLatency = SUGGESTED_LATENCY,
    .hostApiSpecificStreamInfo = NULL,
  };

#ifdef __APPLE__
  // Without this CoreAudio keeps its own sample rate and buffer size and
  // quietly converts, which costs real latency.  paMacCorePro reconfigures
  // the device to match us instead.  The buffer size is a property of the
  // device, so this affects other apps using it while we run.
  PaMacCoreStreamInfo mac_core;
  PaMacCore_SetupStreamInfo(&mac_core, paMacCorePro);
  input_params.hostApiSpecificStreamInfo = &mac_core;
  output_params.hostApiSpecificStreamInfo = &mac_core;
#endif

  // paFramesPerBufferUnspecified lets the host API hand us its native buffer
  // size, a big latency win over imposing our own blocking on top.
  err = Pa_OpenStream(&stream, &input_params, &output_params, SAMPLE_RATE,
                      paFramesPerBufferUnspecified, paClipOff,
                      audio_callback, NULL);
  if (err != paNoError) {
    goto error;
  }
  err = Pa_StartStream(stream);
  if (err != paNoError) {
    goto error;
  }

  const PaStreamInfo* stream_info = Pa_GetStreamInfo(stream);
  printf("Stream latency: in %.2fms + out %.2fms = %.2fms\n",
         stream_info->inputLatency * 1000,
         stream_info->outputLatency * 1000,
         (stream_info->inputLatency + stream_info->outputLatency) * 1000);
  printf("Detection lag: about %.2fms on top of that\n",
         500.0 * PITCH_WINDOW / SAMPLE_RATE);
  fflush(stdout);

  int reported_xruns = 0;
  int ticks = 0;
  const char* last_label = NULL;
  while (Pa_IsStreamActive(stream) == 1) {
    Pa_Sleep(self_test && selftest_is_record(self_test->mode) ? 100 : 500);

    if (self_test && selftest_done(self_test)) {
      printf("\nfinished\n");
      break;
    }
    if (interrupted) {
      printf("\nstopped -- keeping what was recorded\n");
      break;
    }
    if (self_test && (selftest_is_record(self_test->mode) ||
                      self_test->mode == SELFTEST_MONITOR)) {
      const char* label = selftest_label(self_test);
      if (label && label != last_label) {
        printf("\n>> %s\n", label);
        last_label = label;
      }
      // A meter, so a badly placed microphone is obvious while there is
      // still time to move it rather than after the take.
      float peak = selftest_take_peak(self_test);
      int bars = (int)(28 * (peak > 0 ? sqrtf(peak / 0.5f) : 0));
      if (bars > 28) bars = 28;
      printf("\r   %4.0fs [", selftest_remaining(self_test));
      for (int i = 0; i < 28; i++) {
        putchar(i < bars ? '#' : ' ');
      }
      printf("] %.3f%s ", peak, peak > 0.9f ? " CLIPPING" : "        ");
      fflush(stdout);
    }
    if (xruns != reported_xruns) {
      reported_xruns = xruns;
      printf("xruns: %d\n", reported_xruns);
      fflush(stdout);
    }
    // Report what the microphone is actually delivering, so level_full in
    // synth.c can be set from a measurement rather than a guess.  Quiet
    // stretches aren't worth a line.
    if (++ticks % 8 == 0) {
      float peak = engine_take_peak_level(&engine);
      if (peak > 0.002f) {
        printf("input level while playing: %.4f (level_full is %.4f)\n",
               peak, engine.synth.params->level_full);
        fflush(stdout);
      }
    }
  }

  // Only reached when the self-test finishes or the stream stops on its own.
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  return 0;

 error:
  if (stream) {
    Pa_AbortStream(stream);
    Pa_CloseStream(stream);
  }
  Pa_Terminate();
  fprintf(stderr, "portaudio error %d: %s\n", err, Pa_GetErrorText(err));
  return -1;
}

// Plays a synthetic whistle out the speakers, listens to it come back in, and
// records what the engine made of it.  Point the microphone at the headphone
// first.  This makes sound.
static int run_self_test(int argc, char** argv, enum SelfTestMode mode) {
  if (argc != 5 && !((mode == SELFTEST_RESPONSE || selftest_is_record(mode))
                     && argc == 4)) {
    printf("usage: %s --self-test /device/index /recording.f32 <voice>\n",
           argv[0]);
    printf("       %s --rig-check /device/index /recording.f32\n", argv[0]);
    return -1;
  }

  int device_index = read_number_arg(argv[2]);

  static struct SelfTest test;
  float seconds = selftest_init(&test, SAMPLE_RATE, mode);
  if (seconds < 0) {
    die("out of memory");
  }
  self_test = &test;

  int voice = argc == 5 ? atoi(argv[4]) : 1;
  engine_init(&engine, SAMPLE_RATE);
  engine_set_voice(&engine, voice);
  engine_set_volume(&engine, mode == SELFTEST_MONITOR ? 8 : 9);
  engine_set_gate(&engine, 5);

  if (mode == SELFTEST_MONITOR) {
    printf("recording %.0fs of voice %s, playing so you can hear it.\n",
           seconds, engine_voice_name(voice));
    printf("Wear the headphones; keep the mic on you, not on them.\n");
    printf("Provoke the noise -- both sides are recorded.\n\n");
  } else if (selftest_is_record(mode)) {
    printf("recording %.0fs.  Nothing is played -- point the microphone at\n",
           seconds);
    printf("yourself, not at the headphone, and whistle what it asks for.\n\n");
  } else if (mode == SELFTEST_RESPONSE) {
    printf("rig check: %.1fs of tones from 41Hz to 3520Hz\n", seconds);
    printf("measuring what the speaker-to-microphone path does, not the synth\n");
  } else {
    printf("self-test: %.1fs, voice %s\n", seconds, engine_voice_name(voice));
    printf("playing a test whistle -- the synth is recorded, not played\n");
  }
  fflush(stdout);

  signal(SIGINT, on_interrupt);

  int result = start_audio(device_index);
  if (selftest_write(&test, argv[3]) != 0) {
    result = -1;
  }
  selftest_free(&test);
  return result;
}

int main(int argc, char** argv) {
  if (argc >= 2 && strcmp(argv[1], "--self-test") == 0) {
    return run_self_test(argc, argv, SELFTEST_NOTES);
  }
  if (argc >= 2 && strcmp(argv[1], "--rig-check") == 0) {
    return run_self_test(argc, argv, SELFTEST_RESPONSE);
  }
  if (argc >= 2 && strcmp(argv[1], "--record") == 0) {
    return run_self_test(argc, argv, SELFTEST_RECORD);
  }
  if (argc >= 2 && strcmp(argv[1], "--record-hold") == 0) {
    return run_self_test(argc, argv, SELFTEST_RECORD_HOLD);
  }
  if (argc >= 2 && strcmp(argv[1], "--record-playing") == 0) {
    return run_self_test(argc, argv, SELFTEST_MONITOR);
  }

  if (argc != 7) {
    printf("usage: %s /device/index /voice/file /volume/file /gate/file"
           " /fifth/file /sustain/file\n",
           argv[0]);
    printf("       %s --self-test /device/index /recording.f32 <voice>\n",
           argv[0]);
    printf("       %s --record /device/index /whistling.f32\n", argv[0]);
    printf("       %s --record-hold /device/index /holding.f32\n", argv[0]);
    printf("       %s --record-playing /device/index /out.f32 <voice>\n",
           argv[0]);
    printf("       %s --rig-check /device/index /recording.f32\n", argv[0]);
    printf("voices:\n");
    printf("  0: %s\n", engine_voice_name(0));
    for (int i = 1; i <= synth_preset_count(); i++) {
      printf("  %d: %s\n", i, engine_voice_name(i));
    }
    return -1;
  }

  int device_index = read_number_arg(argv[1]);

  voice_control.fname = argv[2];
  volume_control.fname = argv[3];
  gate_control.fname = argv[4];
  fifth_control.fname = argv[5];
  sustain_control.fname = argv[6];

  engine_init(&engine, SAMPLE_RATE);

  pthread_t control_thread;
  pthread_create(&control_thread, NULL, &read_controls, NULL);

  return start_audio(device_index);
}
