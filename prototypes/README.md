# Bass voices

Ten bass voices, all designed for **mono** through a QSC K10.2.
Renders to listen to are in this directory (gitignored).

```
   1: bass              4 octaves down, the original
   2: subbass           5 down
   3: octaveless        Shepard stack, bell fixed at 82Hz
   4: reese             detuned saws, now mono-safe
   5: eight-oh-eight    near-sine, pitch drop, short tail
   6: pluck             the first voice with a note shape of its own
   7: fm                two-operator FM
   8: fm-sub            the same, 5 octaves down
   9: grind             asymmetric saturation, valve-style
  10: square            a plain hollow square
```

Plus two controls that are not voices: `current-fifth` and `current-sustain`,
below.  `reese-hold` used to be a voice here; it is now `reese` with the
sustain switched on.  `octaveless-half` is gone -- one register-following
variant of the Shepard stack was one more than the table needed.

`make run2-mac`.  The keypad reaches 1-9; past that,
`echo 10 > current-voice`.

`acid` is gone -- the portamento and the per-note filter sweep were too much
to control from a whistle.  Its two mechanisms are still in use: `pluck` has
the cutoff envelope, and both it and `reese` have the resonance.

The two pads are gone as well.  They worked -- the measurements below the fold
in git history are all still true -- but a latching drone turned out not to be
what this wants to be.  Their machinery went with them: `partial_mask`, the
two bells, the drift, the sweep, the leslie, the snap and the fold, and the
`PadLayer` mixer, none of which any other voice used.

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

## The fifth

`fm-fifth` is gone and the interval it was built around is now a control of
its own: `current-fifth`, 0 or 1, applying to every voice.

The old preset divided by 24 rather than by 16, so what sounded was the
fundamental whose *third harmonic* was what you whistled -- you play the fifth
of the chord and hear its root.  That is a genuinely useful thing to be able
to do, and there was no reason it should be welded to one timbre: dropping a
fifth and being an FM bass are unrelated decisions, and having them in the
same knob meant you could have either but not both.  So `octave` is now
multiplied by 2/3 on the way out, once, in the one place a played pitch turns
into a frequency.

It survives a voice change, because which voice you are playing and what
interval you are playing it at are separate things to want.

A just fifth rather than a tempered one -- dividing by exactly three is the
harmonic relationship, and it lands about 2 cents flat of equal temperament,
which is 0.05Hz of beating against a mandolin down here, one beat every twenty
seconds.  Verified end to end through `bass` with `current-fifth` at 1:

```
  whistle D 1174.7Hz -> G1 48.96Hz  -1.4 cents
  whistle G 1568.0Hz -> C2 65.35Hz  -1.5 cents
  whistle A 1760.0Hz -> D2 73.38Hz  -0.9 cents
```

(The residual is the detector, not the interval: the ratio measures 0.6665
against 2/3.)

One voice behaves differently under it and it is worth knowing why.  The
`octaveless` pair weight their partials by a bell fixed in *Hz*, so moving the
stack a fifth while the bell stays put does not transpose them -- it re-voices
them.  That is the same mechanism that makes them octaveless working as
designed, not a bug, but it means the fifth is a timbre change there and a
transposition everywhere else.

## The sustain control

`current-sustain`, 0 or 1, applying to every voice.  This was `reese-hold`, a
voice of its own, until it turned out that nothing in it is about `reese`:
what it does is decide what happens when the player stops, and that is a
question you can ask of any timbre.  `reese` with the control on renders as
the old preset did, scaled by the `out_gain` the two presets differed by.

With it on, when the whistle stops the note does not: over 250ms it
slides onto the nearest real note and settles 6dB under itself, sits there for
the rest of two seconds, and then fades over about four more.  Nothing about
the tone is different -- same detune, same resonance, same
`mono_partials = 2`.

A tail is something a note *earns*, not something every note gets: half a
second of playing.  That is what lets this be a switch rather than a mode.
With it on, a run of staccato notes is the voice you already had -- measured
over the fast sections of `recordings/holding.f32`, the envelope moves a
median 0.02-0.8dB depending on the voice, and the tail only appears where a
note was deliberately held.  The first version gave one to every note, which
is fine on the sparse synthetic input it was built against and wrong on a real
phrase: see "What earns a tail", below.

**`pluck` opts out.**  Its whole shape is a note speaking and getting out of
the way, which is the opposite instruction, and what came out was neither:
its tails land a median 8.2dB under the end of the note that made them against
`reese`'s 6.1, with a p10 of -22dB -- a fifth of them inaudible -- and 19dB
duller, because the per-note filter sweep has long since closed.  It is the
one voice `current-sustain` does nothing to.  See `no_sustain` in `synth.h`.

It is a different instrument to play rather than a different sound.  Every
other voice here sounds for exactly as long as you whistle, so the bass line
stops when you take a breath; this one carries through the breath and under
the next phrase.  It is still monophonic, so a new note takes the tail with
it: what you get is a line that never stops, not two notes at once.

Held and *then* released, which is two parameters rather than one long
`release_s`.  A slow release alone starts fading the instant the note ends, so
staying up means re-whistling; the flat stretch in the middle is what makes
the tail feel deliberate.  Measured on `in-pad.f32`, whistle ending at 1.38s:

```
  1.38s   -15.2 dB   whistle stops
  1.40s              starts moving
  1.65s   -20.9      arrived: 5.8dB down, 250ms
  3.30s   -21.4      sustaining, 1.95s after the whistle stopped
  3.75s   -26.0      releasing, 13.6 dB/s
  5.90s   -57.3      60dB down 4.1s after the sustain ended
```

## The sustain

Two things happen in that 250ms, and both are there because a tail is not a
note and should not pretend to be one.

**It drops 6dB.**  A note holding at exactly the level it was played at was
the obvious thing to build first and the wrong thing to play -- a bass line
and its own tail at the same volume is two bass lines.  Six under is the tail
sitting beneath the next phrase instead of competing with it.  Six under
*what* is the question answered in "What the tail inherits", below: the note,
not the fade the note ended on.

**It slides onto the nearest equal-tempered semitone, A=440.**  This is the
one place in the whole program that has an opinion about absolute pitch, and
the asymmetry is deliberate.  A bass line wants every cent of what was played:
the scoops, the slides, and the fact that a whistle is fretless are the
expression.  A note left ringing under the tune has no such freedom -- it is
either in tune with the mandolin or it is a beat, and nothing about it is
moving to disguise that.  So what you play is exactly what you whistled, and
what it settles into is a real note.

Verified end to end, whistling deliberately out of tune:

```
  whistled       while playing        the sustain      + the fifth
  D +38 cents    D2 +29.9 cents       D2  -0.1         G1  +0.2
  G -42          F#2 +49.7            G2  +0.1         C2  -0.3
  A +22          A2 +14.2             A2  +0.1         D2  +0.1
  D -31          D2 -39.6             D2  +0.2         G1  +0.1
  G +47          G2 +38.9             G2  +0.2         C2  +0.0
```

Two things in that table are worth more than they look.

The middle column is 9 cents flat of everything, and that is `reese`, not the
detector: `mono_partials = 2` takes the bottom two partials from a single
copy, and that copy is the flat one of the three by construction.  Snapping
the pitch the voice is *playing* therefore lands the sustain 9 cents under
concert pitch -- a beat every 2.6 seconds at 73Hz, two of them inside one
sustain.  So the snap is applied to what a listener actually hears.  The fifth
is the same problem again and worse: 2/3 is 2.0 cents off the tempered fifth,
so the right-hand column would be out too.

But *which* semitone to land on is a different question, and it is asked of
the played pitch rather than the heard one.  A whistle 42 cents flat of G is a
G; it should not come out an F# because this voice happens to detune.  Row two
is that distinction doing its job -- the voice is sounding F#2 +49.7 while
playing, and still settles on G2.

### How in tune do you have to be?

More than this recording is.  Measured over `whistling.f32`, how far the
whistling itself sits from the tempered grid:

```
  median 25 cents off, p90 42 cents      (long notes: median 30, p90 46)
```

Which means **about a fifth of notes land within 10 cents of a semitone
boundary**, where the snap is a coin flip: 23% of all note ends, 22% of the
ones over 300ms.  On those, a slightly different estimate of the same note
picks the neighbour -- and the two estimates the code has available disagree
on 20% of long notes for exactly that reason.

That is a fact about the input rather than about the code, and there is no
setting that fixes it: 45 cents off has no determinate semitone.  It is worth
knowing because it is the one place where snapping makes a failure *louder* --
an ambiguous smear becomes a definite wrong note.  It is also the reason the
sustain slides in over 250ms rather than jumping: a quarter-second slide onto
a note reads as intent even when the note is not the one you meant.

There is no hysteresis, unlike the pad that used to do this.  It does not need
any: the average is frozen when the note ends, so there is exactly one snap
per note and nothing to dither between.

## Level

1.8dB under `reese` (0.407 against 0.501).  The tail still fills gaps that
`reese` leaves silent, so at equal gain it measures louder integrated over
`whistling.f32` through the K10.2 model, and this is the table's own rule
applied rather than a taste setting.

It was 3.5dB before the sustain went to -6dB, and the difference is the point:
most of what the gain was correcting for was the tail sitting at the same
level as the note, which is now something the voice does on purpose rather
than something the loudness match had to paper over.  What is left is the
honest part -- this voice really does sound more of the time than `reese`
does.

The two measurements that disagreed by a dB before now agree.  Integrated
loudness over the dense `whistling.f32` puts it level with `reese`; total
energy over the sparse notes of `in-pad.f32`, which is how it would actually
be played, puts it +0.1dB.  It peaks at 0.37.

**The integrated match has moved, and that is correct rather than a drift.**
Now that a tail is earned rather than given, the voice sounds less of the
time, so an integrated meter reads it lower.  Energy through the K10.2 model
against `reese` on the same input, both figures including the 1.8dB the table
already puts between them:

```
                        before      now
  whistling.f32       +0.96 dB    -1.33 dB    dense playing, few tails now
  in-pad.f32          +0.10       -0.13       sparse notes, tails unchanged
```

The rule this voice is matched by is not the integrated one, though.  It is
that **a sustaining voice matches itself while the player is whistling**, and being
quieter the rest of the time is the point rather than an error to correct.
Measured that way -- envelope against `reese` over the hops where the player is
actually playing -- it sits a median 1.8dB under, which is exactly the two
voices' `out_gain` difference and nothing else.

So `out_gain` stays where it is.

## Following the whistle

While there is input, this voice is `reese`.  Not approximately -- the same
attack, the same fall, the same everything, and measured over
`recordings/holding.f32` its envelope sits a median 1.8dB under `reese`'s
while the player is whistling, which is exactly the two voices' `out_gain`
difference and nothing else.  It drops out for 1.77s of the 62.7s whistled,
against `reese`'s own 1.85s.

That is worth stating as a rule, because an earlier version of this voice
broke it and the way it broke was instructive.  It second-guessed the
detector: an input that had fallen 6dB under the level being held counted as
the player letting go, whatever `voiced` said, and the level, the timbre, the
pitch and the gate all froze together on that judgement.  The reasoning was
sound as far as it went -- a whistle does not stop, it trails off, and by the
last hop the detector still calls voiced the input is a median 22-30dB under
the note, having got there in about 40ms.  Park on that for six seconds and
you get a quiet, dark, out-of-tune drone.

But it answered the wrong question.  "What should the tail hold?" is not "has
the player stopped?", and deciding the second in order to answer the first
meant the voice could rule a note over while it was still being whistled.  It
did, routinely.  A whistle starts with a push and settles under it, so the
body of a perfectly ordinary note sits about 6dB below its own first 50ms --
and the test measured each note against its own attack transient, because the
level it compared to rises in 8ms and falls in 500ms.  On `scoop-up.f32` it
gave up 167ms into a note and the bass vanished from under a note the player
held for another 700ms.  1.35s of silence in 3.4s of whistling.

So the test is gone, and with it the slow fall it needed.  What replaced it is
not a better judgement about when a note ends -- it is not making that
judgement at all.

### What the tail inherits

The level follows a trail-off all the way down, like `reese`; the tail simply
does not inherit it.  It inherits the note, via the same power-weighted
running average that already carried the pitch, asked for level rather than
for pitch.  A trail-off is quiet and the weight is level squared, so the fade
contributes almost nothing to it.

That one change is what makes the rule affordable, and it does two jobs at
once, because the level is the filter cutoff as well as the volume.  Measured
over `recordings/holding.f32`, against the rendered level and timbre across
the body of the note that made each tail:

```
                              tail level     1-3kHz vs 80-500Hz
  the old freeze-early voice     -6.1 dB          -1.1 dB
  following the whistle, no
    average to inherit          -11.5            -17.3
  inheriting the note            -6.0             -4.0
```

The middle row is what "just follow the whistle" costs on its own: the tail
holds at the bottom of the fade and, because the cutoff hangs off the same
level, 17dB duller than the note that made it.  The bottom row is with the
average.  The residual 4dB of darkening is real and is left alone -- an
instrument does get duller as it decays.

It is eased onto over `SYNTH_HOLD_SETTLE_S` rather than jumped to, for the
same reason the pitch is: this is thirty-odd harmonic amplitudes as well as a
volume, and sweeping those in a few milliseconds splatters.

### Moving into the tail

The level, the pitch and the drop are one gesture and have to move together,
and getting that wrong is audible on every single note.  Two things had to be
fixed before it was.

**They all ride `settle`.**  The level recovery first went on a one-pole of
its own, which started climbing the moment the player stopped while the drop
waited out a hold-off -- so a note held as level as it can be held went *up*
into its own tail and then came back down.  On the shared ramp it is
monotonic.

**The slide sets out from the note's average, not from the pitch the note
ended on.**  Same idea as the level, and the reason is the same: the pitch
follows the trail-off too, and the last thing played is a median 62-83 cents
from what the note was, drifting as the whistle gives out.  Sliding from there
means the tail arrives *by moving*, and the move is audible because the level
is swelling back up underneath it at the same time.  Setting out from the
average leaves it only the snap to travel, fifty cents at the outside.

That does put a step in the pitch at the moment the player stops.  It costs
nothing: that moment is the bottom of the fade, 20dB or more under the note,
which is the quietest the voice ever is while still sounding.

Measured over the isolated note ends in `recordings/holding.f32` and
`recordings/up-down-tail.f32` -- how far the pitch wanders from where it
finally settles, and how far the level rises above where it settles before
coming back:

```
                    pitch wander            level overshoot
                  median      p90         median      p90
  before             55      1085           +2.2      +3.8
  now                40        80           +1.3      +2.3
```

The p90 of 1085 cents is not a typo and is not this voice's fault -- see the
octave guard, below.

### Movement in the tail

A note that sounds for six seconds has to be worth listening to for six
seconds, and only `reese` gets that for free.  Its three copies are detuned
against each other, so its partials wax and wane on their own; every other
voice here is one oscillator with nothing to beat against, and held still it
is an organ note.  Measured as how much each band's level wanders inside a
tail:

```
                    still      moving        still      moving
  bass               0.25        2.15    fm      0.02      0.96
  subbass            2.07        2.83    fm-sub  0.29      1.20
  octaveless         0.17        5.53    grind   0.08      0.86
  eight-oh-eight     0.04        2.36    square  0.00      1.96
  reese              4.44        4.55
```

`octaveless` is set deeper than the rest, at 0.9 against the table's 0.35:
nine partials an octave apart move far more audibly than thirty-two in a
harmonic series, and it is the one voice with no register of its own to hold
the interest.

(Measured over a window that is mostly the LFOs still accelerating, so it
understates what the later part of a tail does; the table below has the shape.)

`reese` is the target and the left column is the problem: everything but it
and `subbass`, which has a little growl on it, sits between 0.00 and 0.29dB.

So the partials are moved directly, in three groups on three slow
incommensurate LFOs, at 0.31, 0.53 and 0.79Hz.

**They start stopped.**  Fading the depth in was the obvious way to make the
movement arrive and it is not as good, because it fades in movement that is
already underway: free-running LFOs are at whatever phase they happen to be at
when the player stops, so the partials get pulled towards it and the note
leans somewhere for no reason.  Accelerating from rest instead means the first
motion is the slowest motion, which is what "coming from nowhere" actually
requires.

The second reason is the one that makes it exact rather than merely gentle.
All three start at phase zero, so at that instant they are equal -- and
multiplying every partial by the same number is precisely what the power
normalisation divides back out.  At the moment the player stops the movement
is not small, it is identically zero, and it emerges only as the LFOs drift
apart.  Nothing has to be faded at all.

Measured by differencing against the same render with the depth at zero, so
what is left is only the movement -- how far it has moved the spectrum, by
seconds since the player stopped:

```
                 0.00  0.25  0.50  0.75  1.00  1.25  1.50  1.75
  fading depth   0.00  1.33  2.52  2.66  1.89  3.17  2.31  3.53
  from rest      0.00  0.11  0.39  0.42  0.46  1.60  2.99  3.03
```

Half a second to come up to speed, not the second or two it first was.  What
this trades is bought back further along: the tail is two seconds at full
level and then a 0.6s release, so what is audible is about three, and every
extra moment spent accelerating comes out of the part that is meant to move.

```
                   0.0s   0.5s   1.0s   1.5s   2.0s   (acceleration time)
    bass           3.94   2.11   1.57   0.91   0.51
    eight-oh-eight 3.38   2.33   2.75   2.46   1.75
    square         2.11   1.95   1.66   1.35   0.79
```

The left column is what the movement is worth with no ramp at all and it is
not what to aim at -- that is the version that switches on.  Half a second
keeps most of it while still moving the spectrum only a tenth of a dB in the
first quarter second, which is the part that has to be seamless.

Two more things about where it is applied matter more than the rate or the
depth:

**Before the power normalisation**, so what moves is the balance between
partials rather than the level.  A drone that pumps is worse than one that
sits still, and the normalisation exists precisely so the timbre can move
without the loudness following it.  It works: the broadband envelope inside a
tail wanders 0.31-0.92dB with this on against 0.18-0.97dB with it off, and
`reese`'s own natural figure is 0.67dB.

**Depth scaled by the settle ramp**, so this belongs to the tail and not to
the voice.  Measured against the same renders with it switched off, over the
notes the player is actually playing, the largest single sample differs by
3e-06 across all ten voices -- and that residual is `settle` unwinding
after a mid-note dip in voicing, the same 8ms unwind that takes the 6dB drop
back off.

The FM voices take it on the index instead, since their whole spectrum is one
number and there are no partial amplitudes to move.  It is tuned separately --
0.30 against 0.35 -- because the two do not mean the same thing: an index
wobble moves energy across the entire Bessel envelope at once.

`grind` moves least, and that is its saturator doing what a saturator does:
`atan` compresses the partial movement along with everything else.  It is the
one voice that might want a depth of its own.

### What pitch it holds

Unchanged in mechanism and now the only thing of its kind here: a running
average over the last half second weighted by how loud the note was, level
squared, so the scoop in and the fade out both count for almost nothing.  It
never needed the freeze-early test to work -- the old README recorded that
switching that test off cost 7.0 cents against 5.4, which is nothing -- and
now it does the same job for the level as well.

The average resets at every onset, so it never averages across two notes, and
it is slid onto over 250ms rather than jumped to.

### What earns a tail

The remaining question is which notes should get one, and that is not
something the first version had an opinion about at all -- it was built and
measured against `in-pad.f32`, six synthetic notes seconds apart with clean
onsets, no scoop, no trail-off and no breath.  Real playing has all four.

Measured over `recordings/holding.f32`, 187 seconds of it, the first version:

```
  166 clicks                 energy above 8kHz, where the voice is 114dB down
  62 tails                   on a recording with maybe 30 deliberate notes
  47/61 land on the semitone nearest what was played
  p90 33 cents off the whistle while the player was still playing
```

and now 3 clicks, 31 tails, 27/33, and p90 11 cents.  Each mechanism measured
with the others left in:

```
                            clicks   tails   tail level   wander p90
  everything                     3      31      -6.0 dB      80 cents
  without the envelope fix      91      31      -6.0          80
  without the onset floor        1      31      -6.1          56
  without the minimum            5      57      -6.8          81
  without the level average      1      31     -24.6          36
  without the pitch anchor       2      31      -6.3         284
  with the old octave guard      4      31      -6.0         351
```

Read the click column carefully: three against one is noise at this level, and
two of these rows trade a click count for something that matters more.
Dropping the level average takes the tail to 24dB under the note; dropping the
pitch anchor triples the wander.

**An onset must not step the output.**  `s->loudness_env = s->loudness` on the
onset sample starts a note at its own dynamics instead of ramping into them,
which every other voice can do because it is at gate 0 when a note starts.
This one is sounding.  Measured on the render, the envelope stepped down a
median of 8.7dB at an onset, p10 19.5dB, worst 47.1dB; 62 of 198 onsets
dropped more than 12dB from an audible tail.  Leaving it alone when the gate
is open takes the median to 0.4dB and the worst to 19.3dB.  It is the single
biggest source of clicks: 68 against 6.

**An onset at nothing is not an onset.**  After a note the detector goes on
finding pitches in the room.  Eight times in this recording it called an onset
on the noise floor of a rest, at 0.8-1.8% of the level being held.  An onset
resets everything at once, so each of those cratered the tail *and* parked it
on the detector's reading of the room -- 1736Hz and 2063Hz against a whistle
around 1080Hz.  The two populations are nowhere near each other: the next
onset up from those eight was at 8.1%, so `0.04` sits in an empty gap.

**A note has to be long enough to have meant it.**  Otherwise every note in a
fast run gets a two second tail and drags the next phrase around.  Swept:

```
                 holds wanted     holds unwanted
    0.35s          30/34              9/117
    0.50           23/34              8/117
    0.75           15/34              3/117
```

There is no clean separation, because a deliberate note and a long note inside
a phrase are the same thing played with different intent.  0.5s is the strict
end of the useful range.  The steadiness of the pitch, which would be the
other half of "held at one note", is not worth testing: the deliberate notes
sit a median 7.4 cents from their own median pitch and the continuous ones
12.0, which is not a separation you can threshold on.

There was a fourth for a while -- a 200ms hold-off before the sustain could
start moving -- and it is worth saying why it is gone.  It was put in to
absorb the false trips of the freeze-early test, and once that test went it
had nothing to absorb: it changed no summary number, and what it did do was
pin the level at the bottom of the fade for a quarter second so the tail
arrived as a hole followed by a swell.  It cost clicks too, 6 against 3.

A note that never earns a tail releases at 60ms rather than at the tail's own
600ms, because `reese` does, and a note the player has already left should not
smear across the next one.

### The octave guard

One fix for this voice is not in this voice.  `pitch.c` rejects an estimate an
octave from where it already is unless it is confident enough to be a real
leap, and that bar was at 0.75.  It was too low, and the way it failed is
specific: a whistle does not stop, it decays, and the estimator's confidence
decays with it -- but not monotonically, and it passes back through the high
0.70s while the signal is already 4 to 27dB under the note.  An exact halving
arriving right there reads as confident enough to be a leap.

```
   guard   holding   scoop-up   up-down   whistling
   0.75       39         0          5         26      octave glitches
   0.85       17         0          2         17
   0.90        3         0          0          4
   0.95        3         0          0          2
```

Every one of the 22 disagreements between 0.75 and 0.90 over these four takes
is an exact 2:1 error at low level in a burst of 1 to 32ms, and none is a real
leap: the deliberate-leap passage covers the same 1072-2198Hz either way.

Most of the time a 30ms octave blip is ridden out.  A sustaining voice cannot,
because the blip can be the last thing heard before the player stops, and then
it is held for six seconds -- a whole note an octave down, sliding back up as the
tail swells in.  That is the 1085-cent p90 in the table above.

**This changes every voice**, unlike everything else here: the synthetic
inputs are clean enough that every voice still rendered bit-identical, but on a
real recording each voice's envelope moves a median 0.2-0.5dB.  It is a fix
rather than a tuning -- there is no case in these takes where 0.75 was right --
but it is the one change that is not confined to the sustain.

### Beating

The other thing to check on a voice that sustains this long is the one that
cost `reese` 31dB -- three detuned copies have six seconds here to drift in
and out of phase.  `mono_partials = 2` is doing its job: measured across the
two second hold the level swings 1.1dB, against the 1.6dB `bass` manages while
being held perfectly still.  The churn stays where it was put, above the
octave.

## Loudness

Two things have to be true, and only one of them used to be.

**Every voice the same, for the same input.**  The table was already matched
this way, through a model of the K10.2 -- flat to 55Hz, then a ported
cabinet's 24dB/octave cliff -- rather than full-range, because every voice
here is a bass voice and matching on a system that reproduces the bottom two
octaves is matching the wrong thing.

**And every voice the same whatever the note.**  This was not true at all, and
it is the bigger of the two.  Measured over `in-ladder.f32` -- five notes
across the whistle range at *identical* input level, which is the one thing a
real recording cannot hold still -- how much louder a voice was at the top of
the range than the bottom:

```
  square 12.1   eight-oh-eight 13.9   bass 10.2   pluck 9.9   subbass 7.2
  grind 6.5     fm-sub 6.4            reese 4.8   fm 3.2      octaveless 0.3
```

`octaveless` is the tell: its bell is fixed in Hz, so its spectrum does not
move with the pitch, and it was the only voice that already held still.  For
everything else the spectrum slides down with the note until the cabinet stops
reproducing the bottom of it.

### Weighting the normalisation by what is audible

The synth already held its level steady as the timbre moved, by normalising
the partials to constant power.  Constant *electrical* power, which is not the
same thing once a note is low enough that its fundamental has fallen off the
bottom of the cabinet.  So the same normalisation is computed a second way,
weighting each partial by `synth_audibility` -- the cabinet's response times
the ear's, the latter fitted to BS.1770's own filters to 0.66dB worst case
over 20-5000Hz -- and the ratio between the two becomes an output gain.

Three things about that were not obvious and cost a measurement each:

**It has to be applied after the saturator.**  Applied to the partials before
it, the drive simply eats it: more amplitude into an `atan` comes back out as
about the same amplitude, and `square` went from 12.1dB to 11.7dB for it.  The
partials still normalise on plain power, because that is what sets where the
saturator is working, and a drive that sees a different level at every pitch
is a different voice at every pitch.

**The FM voices need their own case.**  They have one entry in the partial
table and a whole spectrum in the oscillator, so the weighting saw only the
carrier, found it inaudible at low pitch, and asked for the full 12dB -- which
made them *worse*, `fm` going from 3.2dB to 8.0.  A two-operator FM signal has
constant envelope, so its power is 1 whatever the index does and all the index
moves is where that power sits: J_n(index)^2 of it at the carrier plus n
modulators.  Summing the audibility over that is exact rather than fitted, and
takes `fm` to 1.3dB and `fm-sub` to 2.6.

**The first version of the weighting was eyeballed** and its 2.2dB error at
41Hz came straight back out as loudness that still moved with the pitch.
Fitting it to the real filters was worth 1-2dB on half the table.

### Where it lands

```
                660    933   1320   1867   2640    spread   peak
  bass        -19.6  -17.1  -16.3  -16.1  -15.9      3.7    0.59
  subbass     -17.4  -17.1  -16.9  -16.8  -16.7      0.6    0.76
  octaveless  -17.0  -17.0  -17.0  -17.0  -17.0      0.0    0.42
  reese       -17.6  -17.1  -16.8  -16.8  -16.6      1.0    0.42
  808         -19.7  -17.1  -16.4  -16.0  -15.8      3.9    0.85
  pluck       -27.8  -24.6  -23.6  -23.0  -23.0      4.8    0.85
  fm          -17.5  -17.1  -16.9  -16.7  -16.6      0.9    0.34
  fm-sub      -17.6  -17.6  -17.2  -16.5  -15.9      1.7    0.59
  grind       -16.8  -17.2  -17.2  -16.9  -16.8      0.4    0.57
  square      -19.1  -17.2  -16.5  -16.2  -16.0      3.1    0.70
```

Every voice lands on -17.0 LUFS averaged across the range.  What is left is at
660Hz, the very bottom of what the detector covers: `bass`, `808`, `square`
and `pluck` are still 3-5dB down there, and that is the compensation running
out of room rather than failing.  A 41Hz near-sine through a box that is 17dB
down at 41Hz has very little to give, and asking for the rest of it costs
excursion nobody hears.

Over the range actually played it does not arise.  On
`recordings/up-down-tail.f32`, alternating deliberate notes at 1043Hz and
2101Hz -- corrected for the 1.9dB the two were whistled at, since the synth
follows level^0.8 -- the gap between low and high:

```
  bass +0.3   subbass -0.2   octaveless -0.3   reese +0.0   808 +0.6
  pluck -0.2  fm -0.5        fm-sub +0.9       grind +0.2   square +0.5
```

Two things worth knowing:

- **The whole table came down 5-7.5dB.**  Making a low note as loud as a high
  one means either lifting the low one, which clips, or dropping everything
  else, which is what a peak ceiling forces.  The ceiling is now shared
  between `808` and `pluck` at 0.85.  Make the difference up on the amp.
- **`pluck` sits 7.4dB under the others** on the integrated measurement, and
  is matched by peak instead, as it always was.  A plucked envelope has a high
  crest factor -- that is what an attack is -- so an integrating meter reads
  it low against a voice that sits at one level.  Its attacks arrive at the
  same height as everything else.
- **The sustain control is outside this match.**  It was a preset once, set
  1.8dB under `reese` because its tail filled gaps the rest of the table left
  silent.  As a control it does not get its own gain: a voice sounds the same
  whether or not the switch is on, and being quieter the rest of the time is
  the point rather than something to correct.

## Dead machinery

`wobble_hz`/`wobble_octaves`, `vibrato_*`, `breath` (with the state variable
filter and noise generator behind it), `stereo_width`, and now
`octave_stack_track`/`octave_stack_ref_hz` -- which went dead with
`octaveless-half`, the only preset that ever set them -- are no longer used
by any preset.  All are gated on zeros so they cost nothing at runtime, but
they are dead code and should come out with the rest of the cleanup.

The pad parameters that used to be on this list are gone: they came out with
the pads.

## Regenerating

```
  make zeros2-offline
  python3 prototypes/make-input.py glide > prototypes/in-glide.f32
  python3 prototypes/make-input.py scale > prototypes/in-scale.f32
  ./zeros2-offline <voice> 9 5 < prototypes/in-scale.f32 > /tmp/o.f32
  ffmpeg -y -f f32le -ar 48000 -ac 1 -i /tmp/o.f32 out.wav
```

The trailing numbers are the fifth and then the sustain:
`./zeros2-offline <voice> 9 5 0 1` is that voice with a sustain and no fifth.

The recording the sustain measurements are taken over is made rather than
generated, because none of the generated inputs have a real note-end in them:

```
  ./zeros2-mac --record-hold device-index recordings/holding.f32
```

Three minutes of prompts -- long notes with real rests, notes too short to
deserve a tail, fast playing with none at all -- recorded as mono float ready
for `zeros2-offline`, with a `.sections` file naming what was being asked for
when.  `--record` is the same thing for the voices that follow the whistle.

`in-ladder.f32` is five steady notes across the range at identical amplitude,
which is what the loudness match is measured over.  `in-scale.f32` is a
two-octave chromatic climb repeated three times;
`in-glide.f32` is the same range as a continuous rise; `in-pad.f32` is six
steady notes spaced seconds apart.  The last two were made for the pads and
outlived them: `in-pad.f32`'s seconds of silence between notes are what
the tail was first measured in, and `in-padfast.f32` changes note every
200ms and then every 120ms.
