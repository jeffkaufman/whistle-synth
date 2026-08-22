# Synthetic whistle inputs, for auditioning voices on material that covers the
# whole range on purpose.  A whistle is nearly a sine; 8% of a second harmonic
# is about what a real one has and is enough for the detector to behave the
# way it does on the recording.
import array, math, sys

SR = 48000
BASE = 660.0          # start of every sweep
OCTAVES = 2.0         # 660 -> 2640Hz, inside the 550-3150 the detector covers
REPEATS = 3

def render(freq_at, seconds, gate_at=None):
    n = int(seconds * SR)
    out = array.array('f', [0.0]) * n
    phase = 0.0
    for i in range(n):
        t = i / SR
        f = freq_at(t)
        phase += f / SR
        if phase >= 1: phase -= int(phase)
        a = 0.30 * (gate_at(t) if gate_at else 1.0)
        th = 2 * math.pi * phase
        out[i] = a * (math.sin(th) + 0.08 * math.sin(2 * th))
    return out

# A continuous exponential rise that restarts at the bottom.  For a voice with
# no octave the restart should be inaudible and the rise should never end.
SWEEP_S = 8.0
def glide_freq(t):
    return BASE * 2 ** (OCTAVES * ((t % SWEEP_S) / SWEEP_S))

# The same climb in semitones, so it can be heard as notes rather than as a
# siren.  Each note is 0.30s with a 0.06s gap, which is enough silence for the
# detector to call a new onset.
NOTE_S, GAP_S = 0.30, 0.06
STEPS = int(OCTAVES * 12)
def scale_freq(t):
    step = int(t / (NOTE_S + GAP_S)) % STEPS
    return BASE * 2 ** (step / 12.0)
def scale_gate(t):
    u = t % (NOTE_S + GAP_S)
    if u > NOTE_S: return 0.0
    # Short fades so the input itself has no clicks for the detector to chase.
    return min(1.0, u / 0.010, (NOTE_S - u) / 0.020)

which = sys.argv[1]
if which == 'glide':
    buf = render(glide_freq, SWEEP_S * REPEATS)
else:
    buf = render(scale_freq, (NOTE_S + GAP_S) * STEPS * REPEATS, scale_gate)
sys.stdout.buffer.write(buf.tobytes())
