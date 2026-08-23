# Bass voices

Fourteen voices: twelve bass and two pads, all designed for **mono** through a QSC K10.2.
Renders to listen to are in this directory (gitignored).

```
   1: bass              4 octaves down, the original
   2: subbass           5 down
   3: octaveless        Shepard stack, bell fixed at 82Hz
   4: octaveless-half   the bell follows the pitch at half rate
   5: reese             detuned saws, now mono-safe
   6: eight-oh-eight    near-sine, pitch drop, short tail
   7: pluck             the first voice with a note shape of its own
   8: fm                two-operator FM
   9: fm-fifth          the same, but you whistle the fifth
  10: fm-sub            the same, 5 octaves down
  11: grind             asymmetric saturation, valve-style
  12: square            a plain hollow square
  13: pad               a latching drone: root, fifth, octaves, no third
  14: pad-ninth         the same with the ninth added
```

`make run2-mac`.  The keypad reaches 1-9; past that,
`echo 13 > current-voice`.

`acid` is gone -- the portamento and the per-note filter sweep were too much
to control from a whistle.  Its two mechanisms are still in use: `pluck` has
the cutoff envelope, and both it and `reese` have the resonance.

## Mono

`zeros2.c` was already mono -- it calls `engine_process` and copies it to both
channels -- so the left channel you were listening to is exactly what the wav
renders contain.  Nothing was wrong there.  But two things were designed for
stereo and are now designed for one speaker:

**`reese` had a 31dB hole in it.**  Three copies detuned nine cents apart beat
against each other; spread across an image that is the sound of a Reese, and
summed to one speaker it is the fundamental cancelling itself every couple of
seconds.  Measured over four seconds of held note:

```
  reese, before   fundamental swings 31.3 dB
  reese, now       fundamental swings  2.2 dB
  bass (reference) fundamental swings  1.6 dB
```

The fix is the standard one: the bottom two partials come from a single
undetuned copy (`mono_partials = 2`), scaled by sqrt(unison) so the balance
holds, and everything above stays detuned.  The churn is still there, it just
lives above the octave now.  `stereo_width` is gone from the table entirely.

## octaveless-half

The bell now follows the pitch at a settable rate, `octave_stack_track`.  At 0
it is the original Shepard -- no register at all.  At 0.5, an octave of
whistle moves the bass a fifth:

```
  octaveless        whistle +1 octave -> register +0.00 octaves
  octaveless-half   whistle +1 octave -> register +0.43 octaves
  bass              whistle +1 octave -> register +1.15 octaves
```

So your 2.5 octaves of whistle map into ~1.25 octaves of bass, which fits
between the 55Hz the K10.2 gives up at and the 196Hz where the mandolin's low
G starts.  The bell runs 61-147Hz across everything you can play.

The timbre still repeats *exactly*, just every 1/(1-track) octaves instead of
every one -- measured, similarity 1.000 at two octaves apart and 0.950 at one.

`octaveless-low` is gone, replaced by this.

## The other three mechanisms

**`pluck`** is the note-shape one: `decay_s`/`sustain_level` fall from the
attack to a held level, so a note speaks and gets out of the way.  150ms to a
tenth, which puts a note 11-12dB down by the time the next eighth lands at
110bpm.  Measured through one note: 0, -0.6, -2.0, -2.6, -8.8, -12.2 dB.  Any
other preset can have this by adding the two lines.

**`fm`** is the only voice here that isn't a pulse wave.  Its partials on a
41Hz note:

```
  partial  1    2    3    4    5     6     7     8
   dB     0  +2.9 -2.9 -4.1 -18.3 -27.4 -33.4 -29.6
```

Second partial *above* the first, eighth above the seventh -- that is the
Bessel envelope, and no setting of `cutoff` and `tilt` produces it.  It is
also why it opens up differently: the index moves energy around rather than
uncovering partials in order.

**`fm-fifth`** divides by 24 rather than 16 or 32, so what sounds is the
fundamental whose *third harmonic* is what you whistled -- you play the fifth
of the chord and hear its root.  Dividing by three rather than by a power of
two is also what puts it between its two neighbours in register (log2 1/24 is
-4.585), so the interval and the position are one decision.  Verified:

```
  whistle C 2093.0Hz -> F  87.35Hz  +0.6 cents
  whistle D 2349.3Hz -> G  98.00Hz  -0.0
  whistle G 1568.0Hz -> C 130.63Hz  -2.2
  whistle A 1760.0Hz -> D  73.35Hz  -1.3
  whistle B 1975.5Hz -> E  82.30Hz  -2.1
```

It is a just fifth rather than a tempered one, which is where those couple of
cents come from -- 0.05Hz of beating against a mandolin down here, one beat
every twenty seconds, and well inside how accurately anyone whistles.

**`fm-sub`** is the same five octaves down, in `subbass`'s register, with a
wider and higher index range (1.2-4.0 against 0.8-3.0).  The reason it wants
more index rather than less is that at 25-80Hz the fundamental is gone on
anything it will be played through, so the sidebands *are* the note and the
index is the direct control over how far up they reach.  On a 73.5Hz note,
against `subbass` at the same pitch:

```
              partial  1     2     3     4     5     6
  fm-sub             +0.0  -3.5 -11.3  -3.2 -11.4 -14.7
  subbass            +0.0  +3.6  +3.4  -1.3 -24.3 -18.5
```

The scallop -- partial 4 louder than partial 3 -- is the Bessel envelope
again, and it is what makes this a different voice from `subbass` rather than
a retuning of it.

**`grind`** biases the saturator off centre before it clips.  `atan` is an odd
function, so however hard it is driven it makes only odd harmonics; the bias
breaks that.  Against `square`, which nulls its evens by construction:

```
                partial 2   partial 4   partial 6
  grind           -4.0        -18.5       -24.2
  square         -35.8        -46.9       -53.4
```

Those even harmonics land in the octave between the bass and the mandolin's
low G, which was otherwise the emptiest part of the arrangement.

## The pads

A different instrument sharing the same engine.  Every other voice sounds for
exactly as long as you whistle; this one is *armed* by a whistle and then
holds and fades on its own, so a drone can go under a tune with both hands on
a mandolin and both feet on drums.

75ms of steady whistling starts a chord.  It swells over 150ms, holds 1.2
seconds, then halves every second.  Sound begins 75ms after you whistle and
is at full by 225ms.  Whistling the note
it is already on refreshes the hold; whistling a different one starts a new
chord and ducks the old out in 0.2s, so only one is ever really sounding.

Four layers.  The number falls out of the timings: a chord can be armed every
0.10s and a ducking one is inaudible after about four times the 0.12s duck, so
several overlap in the worst case -- by the fourth the first is 29dB down,
where three layers would have left it at 22dB.  A new chord takes whichever
layer is quietest, and if that layer is still sounding its partial levels and
phases are *kept* rather than zeroed: zeroing them takes whatever was left to
nothing in one sample, which is a click, while keeping them slides the old
partials onto the new chord's frequencies with their levels continuous.

Stress-tested with chords changing every 200ms and then every 120ms: the
largest sample-to-sample step in the output is 0.022, against the 0.059 a
smooth 1.5kHz signal at that peak would produce anyway.  No discontinuities.

Chords are accurate from notes that short.  Isolated 120-200ms notes:

```
  120ms  whistled D -> D  73.5Hz  +3.1 cents
  120ms  whistled G -> G  98.1Hz  +1.5 cents
  150ms  whistled A -> A 109.9Hz  -1.7 cents
  150ms  whistled E -> E  82.2Hz  -4.7 cents
  200ms  whistled G -> G  97.9Hz  -2.1 cents
```

A 60ms note is still ignored.  But the "short notes don't count" filter is
now essentially gone -- anything deliberate arms a chord -- so the pad can no
longer be played over with a melody.

Measured on `in-pad.f32`, notes at 0/6/10/16/19/26s:

```
   0.00s  silent      arming
  0.075s              first sound
  0.225s              full
   2.00s  -18.9 dB    holding (each 0.35s of held note refreshes it)
   2.5-5.5s            decaying, 5.5dB/s = a 1.1s half-life
   7.00s               the 6s note has taken over
  16.5s                mid-duck, A rising as G leaves
  26.0s                the 0.2s note is correctly ignored
```

### How short the arm can be

75ms is close to the floor, and the floor is the player rather than the code.
A whistled note scoops into pitch over a few tens of milliseconds; an arm
shorter than the scoop fires partway up it and then again when the note
settles, so one note becomes several chords.  Snapping to concert pitch makes
that louder rather than quieter, because a chord caught mid-scoop lands on a
definite wrong semitone instead of an ambiguous smear.

Measured over the 104s whistling recording, counting chords that arrive within
150ms of the previous one -- the signature of one note heard as several:

```
   arm     chords   stutter
  0.150s     80        0%
  0.100s    102        3%
  0.075s    144       18%   <- shipped
  0.050s    202       29%
  0.020s    324       55%
  0.010s    426       65%
```

Tightening `pad_steady_cents` does not rescue the short end: at a 10ms arm,
a 12-cent tolerance still leaves 52% stutter, because the detector's estimate
is smoothed over an 8ms window and has flat spots during a scoop that satisfy
any tolerance.  And it is not free -- at a 100ms arm, a 12-cent tolerance
drops legitimate chords from 102 to 35.

There is a second floor underneath: `PITCH_WINDOW` is 384 samples, 8ms, and a
pitch change is only visible around the middle of it.  An arm near 10ms is
asking for roughly one independent pitch estimate.

Chords stay accurate at 75ms.  Isolated short notes:

```
  120ms  D -> D  73.5Hz  +2.2 cents      150ms  E -> E  82.2Hz  -4.6 cents
  120ms  G -> G  98.1Hz  +1.0 cents      200ms  G -> G  97.9Hz  -2.1 cents
  150ms  A -> A 109.9Hz  -2.2 cents
```

### The movement

Four things, none deep enough to name on its own.  A pad is the one voice the
ear will sit and listen to for a while, so anything that repeats or holds
still gets found.

- **Per-partial level drift**, rates spread by the golden ratio so no two
  partials share a period.
- **Per-partial pitch drift**, +/-4 cents.  This is the ensemble, and it is
  done this way because the conventional way does not survive mono: detuned
  *copies* cancel, which cost `reese` 31dB, and a pad is all sustain so the
  cancellation would have time to sweep every partial in turn.  A single
  partial nudged a few cents has nothing to beat against.
- **A sweep** of the shimmer bell, 0.55 octaves on a 18s clock, plus 1.1
  octaves that it climbs through as the chord arrives -- a filter opening on
  a clock and on the note at the same time.
- **A leslie**: amplitude and pitch in quadrature, the partials below 300Hz
  on the slower, shallower bass rotor and the rest on the horn.

Measured over twelve seconds of held chord, against a build with all four
switched off:

```
                total level   above 300Hz   centroid
  moving          1.2 dB        11.2 dB      0.77 octaves
  all off         0.7 dB         0.0 dB      0.00 octaves
```

(The 0.7dB with everything off is the measurement window, not the synth.)
Partial 4 wanders -3.9 to +5.7 cents.

The leslie is applied *after* the per-block power normalization and the drift
before it, and that ordering is not a detail.  Normalizing to constant power
divides out anything that scales the whole chord, so a leslie inside the
normalization measures as a change in spectrum and as nothing at all in level
-- which is the one thing a rotating horn is for.  The drift stays inside on
purpose: there the intent really is to move energy between partials rather
than to move the pad's level around.

### The chord

Root, fifth and octaves of both, and nothing else.  Built as partials
2, 3, 4, 6, 8, 12, 16, 24, 32 of a single series -- so partial 5 and partial
10, the major third, are simply absent, and the pad never says whether the
tune is major or minor.

One series rather than separate oscillators is also why it fuses into one rich
tone instead of reading as a stack of pitches.  That is exactly how an organ
mixture works and for the same reason.  On a whistled D:

```
   2F   73.4Hz  root          +0.0 dB      6F  220.2Hz  oct+fifth   -3.7
   3F  110.1Hz  fifth         +2.3         8F  293.6Hz  2 octaves  -10.8
   4F  146.8Hz  octave        +0.1        12F  440.4Hz  2oct+5th   -23.7
   5F  183.5Hz  MAJOR THIRD  -75.2        24F  880.8Hz  3oct+5th   -14.0
```

The fifth being the loudest single partial is the bell's doing and is fine:
every partial is a member of one series, so the root the ear hears is still D.

`pad-ninth` adds partials 9 and 18, a major second two and three octaves up.
A second is as noncommittal as a fifth -- it belongs to the major and minor
scale alike.  Whether it reads as air or as mud is the thing to listen for.

### Concert pitch

The pad's root snaps to the nearest equal-tempered semitone, A=440.  The bass
voices do not, and the asymmetry is the point: a bass line wants every cent of
what was played, because the scoops and slides and the fact that a whistle is
fretless are the expression.  A chord has no such freedom -- it is either in
tune with the mandolin or it is wrong, and 30 cents flat under a fretted
instrument is a beat rather than a colour.

Whistling deliberately off-pitch, measured on the same input through both:

```
  whistled          pad (snapped)      bass (free)
  D  +38 cents      D   +2.2 cents     D  +41.3 cents
  G  -42 cents      G   +2.6 cents     G  -39.7 cents
  A  +22 cents      A   -0.9 cents     A  +24.1 cents
  D  -31 cents      D   -2.1 cents     D  -24.9 cents
  G  +47 cents      G   -2.8 cents     G  +50.4 cents
```

(The pad's residual few cents is the +/-4 of ensemble drift, not the snap.)

Snapping brings its own failure: a note parked near a semitone boundary would
otherwise alternate between two chords, and at a 100ms arm it would do so
several times a second -- a 100-cent jump is well past the 60-cent threshold
that tells a new chord from a refresh.  So the snap has hysteresis: to leave
the note already sounding you have to get 65 cents away from it, not 50.
Parked exactly on the D/D# boundary with a +/-18 cent wobble, the root holds
within 4.4 cents for five seconds.

`pad_snap_hz` is a frequency rather than a flag, so a band that tunes to 442
can say so.

### Register: folded, not compressed

The root is folded into a single octave, C2-B2 (65-130Hz), so whistling a D
anywhere in your range gives the same D chord in the same voicing.  Contra
keys land where you would want them: D at 73Hz, G at 98, A at 110.

This is **not** the half-tracking `octaveless-half` uses, and it can't be.
Compressing the register halves every interval too:

```
  whistle D -> G   intended +5.0 semitones, half-tracking plays +2.5
  whistle D -> A   intended +7.0 semitones, half-tracking plays +3.5
```

Quarter-tones, out of tune with the band, and no way to state I-IV-V.  That is
tolerable in a bass line, where a compressed contour still reads as a line.
It is not tolerable in a chord.  Folding keeps the pitch class exact and moves
only the octave.  Verified end to end: whistled D/G/A/D come out as D 73.4Hz,
G 98.1, A 110.0, D 73.4, each within a cent.

### Where it sits, and why there's no drive

Body under the mandolin, shimmer above it, and 440-590Hz left ~20dB down for
the tune to live in.  The shimmer bell carries more weight than it first did
-- `shimmer_level` 0.42 rather than 0.25, and a little wider -- which puts the
energy above 800Hz at -11.3dB against the body rather than -16.7dB:

```
   2F    73.4Hz  root         +0.0 dB      12F  440.4Hz  2oct+5th  -20.4
   3F   110.1Hz  fifth        +2.2         16F  587.2Hz  3 octaves -18.7
   4F   146.8Hz  octave       +1.2         24F  880.8Hz  3oct+5th  -10.3
   5F   183.5Hz  MAJOR THIRD -73.1         32F 1174.4Hz  4 octaves  -5.9
```

The saturator is switched off entirely, which no other voice does.  Any
amount of drive intermodulates the partials it is given, and partials 2 and 3
sum to partial 5 -- the major third, the one interval this chord exists in
order not to state.  It measured -17.8dB with the drive at 0.9/1.4, and again
at -19.7 when the level was left hot enough to clip in `engine_process`.  With
no drive and no clipping it is at -75dB.

Richness comes from `drift` instead: every partial's level wanders on its own
slow cycle, at rates spread by the golden ratio so no two ever come back into
step.  That is deliberate rather than conventional -- the usual way to make a
pad rich is a stack of detuned copies, and detuned copies cancel in mono, as
`reese` demonstrated at 31dB.  A pad is all sustain, so the cancellation would
have time to sweep every partial in turn.  Drifting the levels of partials
that stay exactly in tune gives movement with nothing to cancel.

### Level

The pads can't join the -21.0 match: `recordings/whistling.f32` has almost no
notes long enough to arm them, so their reference is `prototypes/in-pad.f32`
instead, and the two recordings measure 6.4dB apart for the same preset.  They
are set **9dB under** what `bass` measures on that same material -- a pad
sounds nearly all the time where a bass line sounds half of it, and this one
is meant to be felt rather than heard.  Peaks land at 0.36 and 0.39, which is
the quietest anything in the table runs.

On the real whistling recording the pad is sounding 48% of the time -- most of
those notes are too short to arm it, which is the mechanism working.

## Loudness

The whole table is now matched **through a model of the K10.2** -- flat to
55Hz, then a ported cabinet's 24dB/octave cliff -- rather than full-range.
Every voice is a bass voice now and the lead voices that used to be the loud
reference are gone, so matching on a system that reproduces the bottom two
octaves is matching the wrong thing.  All ten land on -21.0 LUFS through
that filter.

Two things worth knowing:

- **The ceiling is `subbass`.** It peaks at 0.82 for -21.0, so the table can't
  go louder than this without it clipping.  If you dropped `subbass`,
  everything else could come up about 2dB.  Make the difference up on the amp
  instead.
- **`pluck` sits ~3.5dB under the others** on that measurement, deliberately.
  A plucked envelope has a high crest factor -- that is what an attack is --
  so an integrated meter always reads it low against a voice that sits at one
  level.  It is set by peak instead (0.80, same headroom as the rest).  Its
  attacks arrive at the same height as everything else.

## Dead machinery

`wobble_hz`/`wobble_octaves`, `vibrato_*`, `breath` (with the state variable
filter and noise generator behind it), and `stereo_width` are no longer used
by any preset.  All are gated on zeros so they cost nothing at runtime, but
they are dead code and should come out with the rest of the cleanup.

## Regenerating

```
  make zeros2-offline
  python3 prototypes/make-input.py glide > prototypes/in-glide.f32
  python3 prototypes/make-input.py scale > prototypes/in-scale.f32
  ./zeros2-offline <voice> 9 5 < prototypes/in-scale.f32 > /tmp/o.f32
  ffmpeg -y -f f32le -ar 48000 -ac 1 -i /tmp/o.f32 out.wav
```

`in-scale.f32` is a two-octave chromatic climb repeated three times;
`in-glide.f32` is the same range as a continuous rise; `in-pad.f32` is six
steady notes spaced seconds apart, the last deliberately too short to arm;
`in-padfast.f32` changes chord every 200ms and then every 120ms.
