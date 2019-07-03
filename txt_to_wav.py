import sys
import wave
import struct

with open(sys.argv[1]) as inf:
  with wave.open(sys.argv[1] + ".wav", 'wb') as outf:
    outf.setnchannels(1) # mono
    outf.setsampwidth(2)
    outf.setframerate(44100)
    for line in inf:
      if line.endswith('\n'):
        sample = float(line.strip())
        outf.writeframesraw(struct.pack('<h', int(sample*32767)))
