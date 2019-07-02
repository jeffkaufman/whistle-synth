#!/usr/bin/env python3

import math
import sounddevice as sd
from simplecoremidi import send_midi

def midi(x):
  #print(repr(x))
  send_midi(x)

samplerate = sd.query_devices(None, 'input')['default_samplerate']

frequencies = [
  (622.2540, 'Eb5', 75),
  (659.2551, 'E5',  76),
  (698.4565, 'F5',  77),
  (739.9888, 'F#5', 78),
  (783.9909, 'G5',  79),
  (830.6094, 'Ab5', 80),
  (880.0000, 'A5',  81),
  (932.3275, 'Bb5', 82),
  (987.7666, 'B6',  83),
  (1046.502, 'C6',  84),
  (1108.731, 'Db6', 85),
  (1174.659, 'D6',  86),
  (1244.508, 'Eb6', 87),
  (1318.510, 'E6',  88),
  (1396.913, 'F6',  89),
  (1479.978, 'F#6', 90),
  (1567.982, 'G6',  91),
  (1661.219, 'Ab6', 92),
  (1760.000, 'A6',  93),
  (1864.655, 'Bb6', 94),
  (1975.533, 'B6',  95),
  (2093.005, 'C7',  96),
  (2217.461, 'Db7', 97),
  (2349.318, 'D7',  98),
  (2489.016, 'Eb7', 99),
  (2637.020, 'E7',  100),
  (2793.826, 'F7',  101),
]

min_samples_per_crossing = samplerate / frequencies[-1][0]
max_samples_per_crossing = samplerate / frequencies[0][0]

note_names = {}
for _, note_name, note_number in frequencies:
  note_names[note_number] = note_name

# sines for values from 0*tau to 1*tau
n_sines = 100
sines = []
for i in range(n_sines):
  sines.append(math.sin(i/n_sines * math.tau))

def sin(v):
  # approximation of math.sin(v * math.tau)
  return sines[(int(v*n_sines + 0.5)) % n_sines]


def samples_to_frequency(samples):
  return samplerate/samples

def note_number(frequency):
  if frequency < frequencies[0][0] or frequency > frequencies[-1][0]:
    return None

  for ((f1, _, n1), (f2, _, n2)) in zip(frequencies, frequencies[1:]):
    if frequency < (f1+f2)/2:
      return n1
  return frequencies[-1][-1]

class PitchDetect:
  def __init__(self):
    self.samples_since_last_crossing = 0
    self.positive = True
    self.previous_sample = 0

    self.samples_per_crossing = 40

    self.energy = 0
    self.sample_energy = 0

    self.mod_16_loc = 0   #  four octaves down
    self.mod_8_loc  = 0   #  three octaves down
    self.mod_12_loc = 0   #  three and a half octaves down

  def update(self, sample):
    self.energy += abs(sample)
    self.samples_since_last_crossing += 1
    if self.positive:
      if sample < 0:
        # Let's say we take samples at p and n:
        #
        #  p
        #   \
        #    \
        #----------
        #      \
        #       n
        #
        # we could say the zero crossing is at n, but better would be to say
        # it's between p and n in proportion to how far each is from zero.  So,
        # if n is the current sample, that's:
        #
        #        |n|
        #   - ---------
        #     |n| + |p|
        #
        # But p is always positive and n is always negative, so really:
        #
        #        |n|            -n         n
        #   - ---------  =  - ------  =  -----
        #     |n| + |p|       -n + p     p - n
        first_negative = sample
        last_positive = self.previous_sample
        adjustment = first_negative / (last_positive - first_negative)
        self.samples_since_last_crossing -= adjustment
        self.samples_per_crossing = self.samples_since_last_crossing

        self.positive = False

        self.sample_energy = self.energy / self.samples_per_crossing

        self.samples_since_last_crossing = -adjustment
        self.energy = 0

        self.mod_16_loc = (self.mod_16_loc + 1) % 16
        self.mod_12_loc = (self.mod_12_loc + 1) % 12
        self.mod_8_loc  = (self.mod_8_loc  + 1) %  8
    else:
      if sample > 0:
        self.positive = True
    self.previous_sample = sample


    # now synthesize
    #
    # self.samples_since_last_crossing / self.samples_per_crossing is
    # approximately [0-1] and is our phase angle if we want to synthesize a
    # plain sine.
    #
    # Unfortunately the following is too slow in python.
    mod_16_note = sin(
      (self.samples_since_last_crossing + (
          self.samples_per_crossing * self.mod_16_loc)) /
      (self.samples_per_crossing * 16))

    mod_12_note = sin(
      (self.samples_since_last_crossing + (
          self.samples_per_crossing * self.mod_12_loc)) /
      (self.samples_per_crossing * 12))

    mod_8_note = sin(
      (self.samples_since_last_crossing + (
          self.samples_per_crossing * self.mod_8_loc)) /
      (self.samples_per_crossing * 8))

    if (min_samples_per_crossing <
        self.samples_per_crossing <
        max_samples_per_crossing):
      e = self.sample_energy - 0.1
      if e < 0:
        e = 0

      return e * (mod_16_note + mod_12_note + mod_8_note)
      
pd = PitchDetect()

def callback(indata, outdata, frames, time, status):
  for i, sample in enumerate(indata):
    outdata[i] = pd.update(sample)

with sd.Stream(device=None, channels=1, callback=callback, samplerate=samplerate):
  while True:
    response = input()
