#!/usr/bin/env python3

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

note_names = {}
for _, note_name, note_number in frequencies:
  note_names[note_number] = note_name

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
    self.crossing = 0
    self.positive = True
    self.previous_sample = 0
    
    #self.s_window_size = 4
    #self.s_index = 0
    #self.s_window = [0]*self.s_window_size

    self.e_window_size = 20
    self.e_index = 0
    self.e_window = [0]*self.e_window_size

    self.last_note = None
    self.last_e = None

    self.energy = 0

  def update(self, sample):
    self.energy += abs(sample)
    self.crossing += 1
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
        self.crossing -= adjustment

        self.positive = False

        #self.s_window[self.s_index] = self.crossing
        #self.s_index = (self.s_index + 1) % self.s_window_size

        self.e_window[self.e_index] = self.energy / self.crossing
        self.e_index = (self.e_index + 1) % self.e_window_size

        e = int(sum(self.e_window) / self.e_window_size / 2 * 127)
        if e > 127:
          e = 127

        if e != self.last_e:
          midi((0xb0, 11, e))

        if e >= 40:
          #f = samples_to_frequency(sum(self.s_window) / self.s_window_size)
          f = samples_to_frequency(self.crossing)
          cur_note = note_number(f)

          if cur_note != self.last_note:
            if self.last_note:
              midi((0x80, self.last_note - 24, 0))  # turn off last note
            if cur_note:
              print('%.2f\t%.2f\t%s\t%s' % (f, self.crossing, note_names[cur_note], e))
              midi((0x90, cur_note - 24, 100)) # turn on new note

          self.last_note = cur_note

        self.crossing = -adjustment
        self.energy = 0
        self.last_e = e
    else:
      if sample > 0:
        self.positive = True
    self.previous_sample = sample

pd = PitchDetect()

def callback(indata, frames, time, status):
  for sample in indata:
    pd.update(sample)

with sd.InputStream(device=None, channels=1, callback=callback, samplerate=samplerate):
  while True:
    response = input()
