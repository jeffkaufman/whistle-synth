// A tempo-synced delay for the second input channel, so a second instrument
// can be echoed in time with the tune.
#ifndef DELAY_H
#define DELAY_H

// Longest delay we can produce.  Three repeats at 40bpm is 4.5s, so 8s of
// history is comfortable.  (This used to be 900s, which cost 173MB.)
#define DELAY_MAX_SECONDS 8

struct Delay {
  float* history;
  int length;
  int write_pos;
  float sample_rate;
  float bpm;
  int repeats;
  float volume;
};

// Takes ownership of nothing; `history` must have room for
// DELAY_MAX_SECONDS*sample_rate floats and outlive the Delay.
void delay_init(struct Delay* d, float* history, float sample_rate);
float delay_process(struct Delay* d, float sample);

#endif  // DELAY_H
