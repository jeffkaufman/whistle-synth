#include "delay.h"

#include <string.h>

void delay_init(struct Delay* d, float* history, float sample_rate) {
  d->history = history;
  d->length = (int)(DELAY_MAX_SECONDS * sample_rate);
  d->write_pos = 0;
  d->sample_rate = sample_rate;
  d->bpm = 118.5f;
  d->repeats = 3;
  d->volume = 1;
  memset(history, 0, d->length * sizeof(float));
}

float delay_process(struct Delay* d, float sample) {
  d->history[d->write_pos] = sample;

  float spacing = d->sample_rate / (d->bpm / 60.0f);
  float out = 0;
  for (int repeat = 1; repeat <= d->repeats; repeat++) {
    float pos = d->write_pos - spacing * repeat;
    while (pos < 0) {
      pos += d->length;
    }
    int a = (int)pos;
    int b = (a + 1) % d->length;
    float frac = pos - a;
    out += d->history[a] * (1 - frac) + d->history[b] * frac;
  }

  d->write_pos = (d->write_pos + 1) % d->length;
  return d->repeats > 0 ? out * d->volume / d->repeats : 0;
}
