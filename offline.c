// Offline renderer: runs the same engine the audio callback runs, over a file
// instead of a sound card, so voices can be developed and measured without a
// microphone in the loop.
//
//   make zeros2-offline
//   ./zeros2-offline <voice> <volume> <gate> [fifth] [sustain] < in.f32 > out.f32
//
// Raw mono 32-bit float at SAMPLE_RATE in and out; use ffmpeg to get to and
//
// With --trace it instead prints the pitch hints as TSV, which is how you see
// what the detector thinks rather than guessing from the audio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"

#define SAMPLE_RATE 48000
#define CHUNK 1024

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr,
            "usage: %s voice volume gate [fifth] [sustain] [--trace]"
            " < in.f32 > out.f32\n",
            argv[0]);
    return -1;
  }
  // The trailing arguments are optional and --trace may go anywhere among
  // them; the numbers are positional, the fifth control and then the sustain
  // control.  --trace asks for hints instead of audio.
  bool trace = false;
  int fifth = 0;
  int sustain = 0;
  int numbers = 0;
  for (int i = 4; i < argc; i++) {
    if (strcmp(argv[i], "--trace") == 0) {
      trace = true;
    } else if (numbers++ == 0) {
      fifth = atoi(argv[i]);
    } else {
      sustain = atoi(argv[i]);
    }
  }

  static struct Engine engine;
  engine_init(&engine, SAMPLE_RATE);
  engine_set_voice(&engine, atoi(argv[1]));
  engine_set_volume(&engine, atoi(argv[2]));
  engine_set_gate(&engine, atoi(argv[3]));
  engine_set_fifth(&engine, fifth);
  engine_set_sustain(&engine, sustain);

  if (trace) {
    printf("sample\tseconds\tvoiced\tonset\tfreq\tconfidence\tlevel\n");
  }

  float in[CHUNK];
  float out[CHUNK];
  size_t n;
  long long sample_index = 0;
  bool onset_pending = false;
  while ((n = fread(in, sizeof(float), CHUNK, stdin)) > 0) {
    for (size_t i = 0; i < n; i++) {
      out[i] = engine_process(&engine, in[i]);

      // onset is true for a single sample and the trace prints every
      // PITCH_HOP, so latch it or it is never seen.
      if (engine.detector.hint.onset) {
        onset_pending = true;
      }
      if (trace && sample_index % PITCH_HOP == 0) {
        const struct PitchHint* h = &engine.detector.hint;
        printf("%lld\t%.4f\t%d\t%d\t%.1f\t%.3f\t%.5f\n",
               sample_index, sample_index / (double)SAMPLE_RATE,
               h->voiced, onset_pending, h->freq, h->confidence, h->level);
        onset_pending = false;
      }
      sample_index++;
    }
    if (!trace && fwrite(out, sizeof(float), n, stdout) != n) {
      perror("write");
      return -1;
    }
  }
  return 0;
}
