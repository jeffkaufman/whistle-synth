# Bass voices

Ten voices, all bass, all designed for **mono** through a QSC K10.2.
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
   9: grind             asymmetric saturation, valve-style
  10: square            a plain hollow square
```

`make run2-mac`.  The keypad reaches 1-9; for `square`,
`echo 10 > current-voice`.

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
`in-glide.f32` is the same range as a continuous rise.
