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

# A ladder of steady notes across the whistle range, all at exactly the same
# amplitude and each long enough to earn a tail.  Nothing else here is usable
# for judging loudness: `scale` changes note too fast to measure, and a real
# recording cannot hold the input level constant while the pitch moves, which
# is precisely the confound.  Half-octave steps over the two octaves the
# detector covers.
LADDER_ON, LADDER_OFF = 1.6, 3.0
LADDER_STEPS = 5
def ladder_freq(t):
    step = int(t / (LADDER_ON + LADDER_OFF)) % LADDER_STEPS
    return BASE * 2 ** (step * OCTAVES / (LADDER_STEPS - 1))
def ladder_gate(t):
    u = t % (LADDER_ON + LADDER_OFF)
    if u > LADDER_ON: return 0.0
    return min(1.0, u / 0.010, (LADDER_ON - u) / 0.020)
LADDER_S = LADDER_STEPS * (LADDER_ON + LADDER_OFF)

# One steady note at six levels, six seconds each, for the voices where the
# input level controls something other than how loud the output is.  Constant
# pitch on purpose: `drawbar` answers the breath with rotor speed, and the
# only way to see that is to hold everything else still.  The levels bracket
# `level_full` -- the detector reads them as 0.29 to 1.45 of it, with the
# quietest under the gate and so silent.
STEP_LEVELS = [0.04, 0.09, 0.15, 0.22, 0.30, 0.45]
STEP_S = 6.0
STEPS_S = len(STEP_LEVELS) * STEP_S
def steps_freq(t):
    return 1320.0
def steps_amp(t):
    return STEP_LEVELS[min(int(t / STEP_S), len(STEP_LEVELS) - 1)] / 0.30
def steps_gate(t):
    u = t % STEP_S
    return steps_amp(t) * min(1.0, u / 0.020, (STEP_S - u) / 0.050)

# A ladder of *struck* notes: one pitch at six velocities with real silence
# between them, then two notes whose breath moves after the onset.  `steps`
# cannot do this job -- its 50ms gate dip does not un-voice the detector, so
# six seconds in it is one held note and there are two onsets in the whole
# file.  That is exactly what `drawbar` wanted (the rotors answer a breath
# that never stops) and exactly wrong for a voice where every note is a
# separate strike.
#
# The last two notes are the measurement that says a struck voice is struck:
# one starts quiet and swells, one starts loud and falls away, and both should
# render as a note whose whole shape was decided in its first tenth of a
# second.
STRIKE_LEVELS = [0.06, 0.10, 0.15, 0.22, 0.32, 0.45]
STRIKE_ON, STRIKE_OFF = 2.5, 1.0
STRIKE_SLOT = STRIKE_ON + STRIKE_OFF
# (level at the strike, level it moves to over the note)
STRIKE_MOVES = [(0.10, 0.45), (0.45, 0.10)]
STRIKE_N = len(STRIKE_LEVELS) + len(STRIKE_MOVES)
STRIKE_S = STRIKE_N * STRIKE_SLOT

def strike_freq(t):
    return 1320.0
def strike_gate(t):
    slot = int(t / STRIKE_SLOT)
    u = t % STRIKE_SLOT
    if slot >= STRIKE_N or u > STRIKE_ON:
        return 0.0
    if slot < len(STRIKE_LEVELS):
        a = STRIKE_LEVELS[slot]
    else:
        lo, hi = STRIKE_MOVES[slot - len(STRIKE_LEVELS)]
        a = lo + (hi - lo) * (u / STRIKE_ON)
    return (a / 0.30) * min(1.0, u / 0.020, (STRIKE_ON - u) / 0.050)

# How the pad is actually played: a few steady notes, seconds apart, with the
# gaps left in so the hold and the decay can be heard doing their work.  The
# last one is deliberately too short to arm anything.
PAD_EVENTS = [
    ( 0.0, 1174.66, 1.4),   # D  -- arms
    ( 6.0, 1567.98, 1.4),   # G  -- new chord, ducks the D
    (10.0, 1567.98, 1.4),   # G  -- same note again, should refresh not restack
    (16.0, 1760.00, 1.4),   # A
    (19.0, 1174.66, 1.4),   # D  -- quick change, exercises the duck
    (26.0,  880.00, 0.2),   # too short to arm; the pad should ignore it
]
PAD_S = 40.0

def pad_freq(t):
    for start, f, dur in PAD_EVENTS:
        if start <= t < start + dur:
            return f
    return PAD_EVENTS[0][1]

def pad_gate(t):
    for start, f, dur in PAD_EVENTS:
        if start <= t < start + dur:
            u = t - start
            return min(1.0, u / 0.020, (dur - u) / 0.030)
    return 0.0

# The stress case a 100ms arm makes possible: chords changing as fast as a
# player can articulate them, then a couple of very short notes.  Kept
# separate from in-pad.f32 so the loudness reference doesn't move.
FAST_EVENTS = [
    ( 0.0, 1174.66, 0.15), ( 0.3, 1567.98, 0.15), ( 0.6, 1760.00, 0.15),
    ( 0.9, 1174.66, 0.15), ( 1.2, 1567.98, 0.15), ( 1.5, 1760.00, 0.15),
    ( 2.0, 1174.66, 0.12), ( 2.2, 1567.98, 0.12), ( 2.4, 1760.00, 0.12),
    ( 2.6, 1318.51, 0.12), ( 2.8, 1174.66, 0.12),
    ( 4.0, 1567.98, 0.06),   # 60ms: under the threshold, should be ignored
    ( 4.3, 1760.00, 0.06),
    ( 5.0, 1174.66, 1.20),
]
FAST_S = 10.0

def fast_freq(t):
    for start, f, dur in FAST_EVENTS:
        if start <= t < start + dur:
            return f
    return FAST_EVENTS[0][1]

def fast_gate(t):
    for start, f, dur in FAST_EVENTS:
        if start <= t < start + dur:
            u = t - start
            return min(1.0, u / 0.008, (dur - u) / 0.012)
    return 0.0

which = sys.argv[1]
if which == 'strike':
    buf = render(strike_freq, STRIKE_S, strike_gate)
elif which == 'steps':
    buf = render(steps_freq, STEPS_S, steps_gate)
elif which == 'padfast':
    buf = render(fast_freq, FAST_S, fast_gate)
elif which == 'pad':
    buf = render(pad_freq, PAD_S, pad_gate)
elif which == 'ladder':
    buf = render(ladder_freq, LADDER_S, ladder_gate)
elif which == 'glide':
    buf = render(glide_freq, SWEEP_S * REPEATS)
else:
    buf = render(scale_freq, (NOTE_S + GAP_S) * STEPS * REPEATS, scale_gate)
sys.stdout.buffer.write(buf.tobytes())
