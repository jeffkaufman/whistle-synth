# Bass voices

Ten bass voices and two that are not basses, all designed for **mono**
through a QSC K10.2.  Renders to listen to are in this directory (gitignored).

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
  11: drawbar           tonewheel organ, 2 down, breath drives the leslie
  12: drawbar-hi        the same organ an octave up, same leslie
  13: rhodes            tine electric piano, 2 down, struck; breath drives the tremolo
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
`PadLayer` mixer, none of which any other voice used.  `drawbar`'s leslie is
not that leslie: the pads' was one LFO on the output, and this one is two
rotors either side of a crossover, moving the partials in amplitude and in
pitch at once.  Nothing was reused.

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

## The drawbar organ

`drawbar` is the eleventh voice and the only one that is not a bass: two
octaves down, so a 550-3150Hz whistle lands at 137-787Hz, which is where a
right hand plays.  It is a tonewheel organ through a leslie, and it is the one
voice here where how hard you blow does not change how loud it is.

**Nine sines at fixed footages, and no filter anywhere.**  Every other voice
here shapes a harmonic series with `pwm`, `tilt` and a cutoff that opens up
when the player leans in.  A Hammond cannot do any of that: it adds nine sine
tones at fixed footages and the only thing that changes the timbre is a
drawbar somebody pulls.  So `drawbars[9]` replaces the pulse series outright,
the way `octave_stack_hz` and the FM pair do.

The footages are not harmonics of the note.  16' is the octave *below* it and
5 1/3' the fifth *above* it, so the series is built on half the played note:
partial n sits at n/2, the note is partial 2, and the nine drawbars land on
partials 1, 3, 2, 4, 6, 8, 10, 12 and 16.  That keeps `octave` meaning the
note you hear, as it does for every other preset, and it is why this voice
takes the per-partial phase path -- there is no oscillator whose multiples are
0.5, 1, 1.5.

The registration is 86 8643 222.  With the drive and the cabinet turned off
the render is the drawbar setting exactly, which is the check that the
mechanism is right:

```
   drawbar    16'  5 1/3'  8'    4'   2 2/3'  2'  1 3/5' 1 1/3'  1'
   setting     8     6     8     6      4     3     2     2      2
   wanted    0.0  -6.0   0.0  -6.0  -12.0 -15.0 -18.0 -18.0  -18.0
   rendered  0.0  -6.0   0.0  -6.0  -11.9 -14.8 -17.8 -17.8  -17.8
```

The ten half-partials no drawbar sounds at come out 66-79dB down, which is the
noise floor of the measurement.

The one drawbar not set by what a Hammond does is the 5 1/3'.  It sounds a
fifth above the note, and an organist playing chords hears that as the hollow
weight the drawbar is famous for; a monophonic line played at 8 instead grows
a second melody in parallel fifths, which fights whatever the arrangement is
doing.  At 6 it reads as colour on one line rather than as two.

### What the speaker said

This voice was built and measured on headphones, and the first thing the
K10.2 did was disagree with it.  It was harsh through the box and fine on the
headphones, which is a spectrum problem rather than a mono one -- `drawbar`
runs a single oscillator with `stereo_width` at zero, so its two channels are
the same samples and folding it to one speaker cannot change anything.

Octave-band energy over `recordings/whistling.f32`, in dB relative to each
voice's own total:

```
              125    250    500     1k     2k     4k     8k
  bass       -7.0  -16.0  -31.2  -42.8  -58.1  -66.9  -72.9
  square     -8.8  -14.6  -24.1  -35.1  -48.3  -64.0  -70.5
  drawbar    -5.0   -4.4   -6.7  -11.9  -14.1  -21.6  -46.4   before
  drawbar    -4.8   -4.3   -6.6  -12.2  -18.7  -32.0  -64.2   now
```

Two things there, and only one of them is a fault.  Being 30-40dB above the
basses at 2-4kHz is what a treble voice *is*, and it lands exactly where a
K10.2 crosses to its horn and is at its most forward and most directive.
Having anything at 8kHz is the fault: an organ is nine sine tones and the
tallest of them is 6.3kHz, so everything up there was the saturator, and a
real leslie could not have radiated it anyway.

So the voice now models the box it is supposed to be coming out of --
`top_hz`, 3.5kHz, two poles, after the drive because that is where the
speaker sits in the real chain.  That is worth 4.6dB at 2k, 10dB at 4k and
essentially all of the 8k.  The 1' drawbar went back to 2 at the same time:
it had been bumped to 4 on the theory that a mono PA needs the presence, and
the PA disagreed.

The band table is also a reminder that this voice peaks at 125-250Hz where
every other voice here peaks at 63.  The basses live under a K10.2's corner
and this one sits right where the box is most efficient and where a room
piles up, so it will want more care about placement than they do.

Renders for comparing on the actual speaker are in this directory, all
matched to -23.0 LUFS so the comparison is about tone and not level:
`drawbar-ab-a.wav` is what it was, `-b` is what it now is, and `-c` and `-d`
go further -- `-c` pulls the 16' and 5 1/3' back as well, `-d` takes the
whole upper half down and the cabinet to 2.5kHz.

### One volume

An organ has no dynamics.  The key is down or it is up, the tonewheel is
turning either way, and how loud it comes out is somebody's foot.  So this
voice has a key contact -- `contact_level`, a tenth of `level_full`, 0.022 in
absolute terms -- and above it the level is flat however hard the player
blows.  Measured over held tones across a five-to-one range of breath:

```
   breath (of level_full)   0.29   0.48   0.71   0.97   1.45
   rendered level          -20.9  -20.2  -19.6  -19.3  -18.9   dB
   the same voice with
     the contact off       -29.5  -25.2  -22.0  -19.5  -18.9
```

2.0dB against 10.6, and the 2 that is left is not the envelope: it is the amp
being driven harder, which is an organ getting dirtier rather than louder.

Below the contact the level falls off as the square rather than switching off,
and that is not a softening -- it is what keeps the articulation.  A glottal
stop is a 25ms dip that never silences the whistle, so with everything above
the contact flat, those dips are the only thing left that separates two
tongued notes.  A square law puts a dip to a tenth of the contact 40dB down,
which is a note ending.  It is also *stricter* than the old curve where it
matters: the spurious onsets the sustain has to defend against come in around
1% of full level, where `level^0.8` gives -32dB and this gives -52.

**The contact has to sit under the player's quietest note**, and where that is
depends on the room.  It started at 0.20 of `level_full`, which is under
everything in `recordings/whistling.f32`, and that was wrong as soon as the
voice came out of a PA instead of headphones: a player whistles more quietly
into a room that is already loud.  Read off the meter `run2-mac` prints, quiet
playing through the K10.2 peaked at 0.0596 -- a note body of 0.028-0.042,
against the peak-to-body ratio of 1.4-2.2 measured over the notes in the
recording -- and all of that is *under* a contact at 0.044.  Every quiet note
was on the skirt, ducked and moving with the breath, which is precisely what
this parameter exists to prevent.  Note-to-note rendered level, p10 to p90:

```
   at the level the recording was made at, contact 0.10    2.7 dB
   at the level it is actually played at,  contact 0.20    9.0 dB
   at the level it is actually played at,  contact 0.10    4.0 dB
```

What that costs is the knee's protection against the detector finding a note
in room noise: a hop at 0.01 came out 26dB down and now comes out 14dB down.
Quiet playing being an organ is worth more than a quiet blip being 12dB
quieter.

The contact is a fraction of `level_full`, so the two move together.  If that
is ever recalibrated -- and on the rig this was measured on it reads about
1.4x low, since full playing there is a note body around 0.30 against
`level_full`'s 0.22 -- the absolute contact moves with it, and it is the
absolute number that has to stay under the quietest note.

### Where the breath goes instead

Into the leslie.  Whistle harder and the rotors spin up, which is not a
substitute for the missing dynamics -- it is the expression the instrument
actually has.  An organist ends a phrase by kicking the speed switch, and
that gesture is on every record ever made with one.  Here it is continuous
rather than a switch, and it is on the one control a whistle has.

Two rotors: a horn for the top and a drum for the bottom, running 5.5-8.6Hz
and 4.6-7.1Hz.  330 to 510rpm and 280 to 430, against a 122's tremolo at 400
and 340 -- so the range sits around a real cabinet on fast and reaches past
what the motor does, which is the point of it being a control instead of a
switch.  Measured off the render, with the rate recovered from the pitch swing
itself so this is what the rotor did rather than what it was asked for
(`make-input.py steps`):

```
   breath (of level_full)   0.29   0.48   0.71   0.97   1.45
   horn (Hz)                6.39   7.00   7.69   8.49   8.59
   drum (Hz)                5.32   5.80   6.37   7.01   7.10
```

Over `recordings/whistling.f32`, which is a performance rather than a ladder,
the horn runs 5.5 to 8.3Hz with a median of 6.2 and a p95 of 8.1.

**The floor started at 3.9Hz** -- a genuinely moderate spin, which is what
this control was originally for -- and came up because of where the player
actually sits when the voice is coming out of a PA instead of headphones.
They whistle more quietly, and quieter whistling parked the rotors at the
bottom of the range.  Attenuating the recording to stand in for that, the
horn's median over the notes played:

```
                    as recorded    -6dB    -9dB
   floor at 3.9        5.0Hz        4.4     4.3
   floor at 5.5        6.2          5.9     5.7
```

What it costs is the bottom of the range: this is now fast to very fast rather
than moderate to very fast.  A leslie at 4Hz is a real thing, but it is not
what this voice sounds like at its best, and a control whose bottom third is
where the player lives is not a control.

The floor is therefore a calibration to a player rather than a property of a
cabinet.  If what has moved is the input level itself, `level_full` is the
number that says so -- `run2-mac` prints the level while playing for exactly
this reason -- and moving that fixes the key contact and the drive along with
the rotors.  The contact is the half of this that bit first: at the level this
is actually played at, 52% of the playing sat below a contact at 0.20 against
30% at the level the recording was made at.  Lowering it to 0.10 takes those
to 26% and 15% -- see "One volume" above.

**The rate comes off the level straight**, not off the soft-kneed `dynamics`
the drive and the cutoff use.  That knee is already at 0.47 by the quietest
breath this voice will sound at, so on it the bottom half of the rotors' range
was unreachable and the control ran from fast to very fast.

**The rotors have mass.**  `leslie_spin_s` is 0.9s, which is roughly what a
real cabinet takes to come up to tremolo, and it is what makes this playable:
the level swings 20dB inside a single note -- the scoop in, the body, the
trail off -- and a rotor that chased that would be a speed wobble on every
note.  With inertia the rotors follow the phrase and ignore the notes in it.

The target is also *held* whenever the player is not sounding a note, rather
than followed down through the trail-off.  A rest is not an instruction to
slow down: a real cabinet does not know the player has stopped, and the switch
is where it was left.  Without this the rotors coasted down through every gap
and spent the first second of the next phrase winding back up.

### The leslie itself

Each rotor gives the partials on its side of an 800Hz crossover an amplitude
swing and a doppler swing at once, and the two are in quadrature: a speaker is
loudest when it points at you and highest in pitch a quarter turn earlier,
when it is moving towards you fastest.  Amplitude on the sine of the rotor
angle, pitch on the cosine.  That is the difference between a leslie and a
tremolo and a vibrato that happen to share a rate.

The amplitude has to go *after* the power normalisation, which is the opposite
of what the tail shimmer does.  The shimmer is applied before it so that the
balance between partials moves and the level does not; here the level moving
is the entire effect, and normalising it away would leave a rotating timbre
and no rotation.

**Every partial is shared across the crossover** rather than assigned to one
rotor.  A hard assignment would put a partial parked near 800Hz between a
rotor 1.45 times its level and one 0.78 times it, and flip it between them
every time the player's pitch wandered a cent -- a flutter on that partial and
nothing a leslie does.  The split is 12dB an octave, which is steep enough
that the note itself belongs to the drum: on a 6dB slope the horn still has a
third of the fundamental at the bottom of the range and swings it 27 cents,
which reads as vibrato on the melody.  Measured over held notes at every rotor
speed:

```
                        note       partial above 1kHz
  pitch swing        6.8-8.0 cents      20-26 cents
```

The melody is steady and the colour around it turns, which is the right way
round.  Amplitude, at the listener, is the two rotors crossing: 6-9dB peak to
peak depending on where in the range the note sits, deepening as the line
climbs because more of the voice is in the horn up there.

### The amp

Set by how much it fills in between the drawbars.  The saturator's products
land on the same half-partial grid the drawbars sit on, so they can be
measured against it.  The loudest of them, at 1.75 times the note, in dB under
the note:

```
   drive_soft/loud/bias   backed off   leaning in
   0.7 / 2.0 / 0.25         -20.6        -14.5
   0.6 / 1.5 / 0.20         -22.0        -16.7    <- kept
   0.4 / 0.9 / 0.15         -26.1        -21.9
```

A step louder turns a monophonic line into a fuzz voice; a step quieter is
clean enough that the amp stops being part of the sound.  The 5dB between the
two ends matters more here than on any other voice, because with the volume
flat the drive and the rotor speed are the only two things the breath still
reaches.

### What is not modelled, and what is only half there

The percussion (the decaying 2nd or 3rd harmonic on a note's attack), the
internal vibrato/chorus scanner, and the chorale speed -- which would be this
machinery at about a tenth the rate, and belongs in a second preset rather
than in a control the breath is already using.

The key click is reached for and only partly arrives.  The gate is 2ms, the
fastest in the table, but over the onsets in `in-scale.f32` the rendered note
takes 8-13ms to go from a tenth of its level to nine tenths, against `bass`'s
11-19.  The player's own attack is what holds it up, not the gate.  A real
click would be inside 2ms.

### An octave up

`drawbar-hi` is the same organ on the upper manual: one octave down instead of
two, so a whistle lands at 275-1575Hz against `drawbar`'s 137-787.  Everything
in the preset is `drawbar`'s except the octave, and the two numbers that are
*not* copied are the point of the voice -- the crossover and the cabinet do
not move.  They belong to the box the sound comes out of, and a box does not
transpose when the organist plays further up the manual.

Two things fall out of that, and they are the whole difference between the two
voices.

**The melody climbs into the horn.**  The crossover splits half and half at
800Hz, so the bottom of this range is the drum's and the top is the horn's.
Measured over the ladder, the fundamental's own pitch swing:

```
                bottom note        top note
   drawbar      6.8 cents (165Hz)  6.2 cents (660Hz)
   drawbar-hi   6.9 cents (330Hz)  23.3 cents (1320Hz)
```

`drawbar` never leaves the drum and this one crosses.  A leslie really does
warble high organ notes; this is the register where that happens.

**The upper drawbars run off the top of the cabinet.**  The 1' sits at eight
times the note, which up here is 2.2-12.6kHz against a 3.5kHz corner, so it is
22dB down at the top of the range.  In octave bands this voice is `drawbar`
shifted up one band below 1kHz and progressively less than that above it --
1.4dB short at 2k, 3.8 at 4k, 6.9 at 8k.  Playing a real one high sounds
darker for the same reason: the box stops helping.

It is still brighter in absolute terms (-14.0 at 2k and -23.1 at 4k against
`drawbar`'s -19.4 and -32.4), because an octave up is an octave up.  That is
the band a PA horn is most forward in, so if this one turns out harsh where
`drawbar` is not, the registration is the thing to change and not the cabinet.

### The cabinet and the loudness compensation

`top_hz` is applied after the saturator, so it cannot be part of the partial
amplitudes -- which means the audibility weighting that holds a voice's
loudness steady across its range could not see it.  The whole spectrum climbs
against a corner that does not move, so the filter takes progressively more
off as the line goes up, and the compensation was holding the *unfiltered*
level steady while the filtered one tilted.  `drawbar-hi` is where that became
obvious, because it is an octave closer to the corner.

The fix is one multiply: the weighting now includes the filter's own power
response.  Ladder spread, bottom of the whistle range to the top:

```
                    before   after
   drawbar           1.0dB    0.5dB
   drawbar-hi        2.2      0.7
```

Nothing else in the table sets `top_hz`, so nothing else moved -- the ten bass
voices render bit-identically across the change.

### Loudness

The table's usual match -- equal LUFS through the K10.2 model over
`recordings/whistling.f32` -- landing at -23.0 against the rest of the table's
-22.4 to -23.1, at 0.27 peak against `subbass`'s 0.82.

The match means less here than it does elsewhere, and it is worth knowing why.
Every other voice's loudness follows the playing, so matching the integral
matches the whole curve.  This one is flat, so the integral is all there is.
On a ladder of notes played at one level it therefore measures about 3dB under
the rest of the table -- the same voice on material with no dynamics in it,
where the others are all at their loudest and this one is where it always is.
Over a real performance they land together, which is the thing that has to be
true when the player changes voice mid-set.

Across pitch it is the flattest voice here:

```
                660    933   1320   1867   2640    spread
  drawbar     -20.0  -19.9  -19.9  -20.3  -20.4      0.5
  drawbar-hi  -19.8  -19.8  -20.1  -20.5  -20.4      0.7
```

That is not the audibility compensation working hard.  It is a voice whose
lowest partial is 69Hz never asking it for anything -- plus the correction
above, which is what took these from 1.0 and 2.2.

## The tine piano

`rhodes` is a Fender Rhodes: a piano action hitting a steel tine next to an
electromagnetic pickup, into an amp.  Two octaves down, the same register
`drawbar` plays in -- a 550-3150Hz whistle lands at 137-787Hz, C#3 to G5,
which is the middle of the instrument.

It is the first voice here that is **struck**, and the breath does two
separate things: at the onset it sets the velocity and then the hammer leaves,
and after that it works the suitcase tremolo.  Between them the player has
what a keyboard player has -- how hard each note is hit, how long the key is
held, and a foot on the front panel.

### The strike

Every other preset in this table spends the player's breath continuously.
Ten of them spend it on level, which is a wind instrument; the two organs
spend it on rotor speed, because an organ has no dynamics at all.  A hammer
is in contact with the string for a few milliseconds and then it is gone, and
from that moment nothing the player does can make the note louder, brighter
or dirtier.  Only shorter, by lifting the key.

`strike_s` is how long the contact lasts.  Inside it the voice takes the peak
of the level; outside it, the level, the brightness and the drive all hold
where the strike put them.  `prototypes/in-strike.f32` is the measurement:
six notes at a ladder of velocities, then one struck quiet that swells to
full and one struck full that fades away.

```
   struck at 0.10, breath rising to 0.45   -27.0 dB   (a steady 0.10 note: -27.7)
   struck at 0.45, breath falling to 0.10  -16.1      (a steady 0.45 note: -16.1)
```

Both render as the note they were struck at, to a fraction of a dB.

**The peak over the window, not the value at the end of it.**  A whistled note
scoops in over a few tens of milliseconds, so a sample taken at a fixed
instant reads wherever the scoop had got to.  Over
`recordings/whistling.f32`, how much of its eventual peak a note has reached:

```
   30ms   median 0.76   p10 0.39
   80ms          0.96       0.59
  100ms          1.00       0.59
```

100ms is where the median note has arrived and where the p10 note -- one with
a genuinely slow attack, not one being struck hard -- has mostly arrived.

**The peak costs a calibration.**  Every other voice's `level_full` is 0.22,
set against the level the synth sees continuously; this one reads a note's
first peak, which over the same recording runs a median 1.26x its body.  0.22
x 1.26 is 0.28, and `level_full` is 0.30.  Left at 0.22 the median note would
render at 0.80 of full and the top fifth of real playing would be against the
cap, which on a piano is the instrument gone.

**Short notes do not suffer for it**, which was the thing to check: over
`in-padfast.f32` a 60ms note comes out 0.2dB under a 1.2s one, against
`bass`'s 0.9dB.  The latch reads a smoothed level and a 60ms note's peak is
still that note's peak.

**`in-steps.f32` could not do this job.**  Its 50ms gate dip does not
un-voice the detector, so six seconds in it is one held note -- two onsets in
the whole file.  That is exactly what `drawbar` wanted, since rotors answer a
breath that never stops, and exactly wrong for a voice where every note is a
separate strike.  Rendered with `rhodes` it is one note decaying for
thirty-six seconds while the player blows harder and harder at it, which is a
fair demonstration of the strike and a useless velocity ladder.

### The bark

A Rhodes note is two sounds: a bright inharmonic cluster that dies in a few
hundred milliseconds, and a near-sine body that rings for seconds.  The
cluster is the tine's own high bending modes -- it is a steel bar clamped at
one end, and a stiff bar's modes go progressively sharp rather than landing on
harmonics, which is why the attack reads as a bell.

Three things build it, and all three already existed:

- `stretch` at 0.012 puts partial 8 at 8.7 and partial 16 at 18.9.  Enough
  smear to be a bell; small enough that the second partial is only 21 cents
  sharp, where a bigger number starts sounding out of tune rather than struck.
- `pwm_center` at 0.045 with `tilt` at 1.0, which is not a timbre but a
  supply: a flat bank of partials out to about the eleventh for the filter to
  shape.  This started at 0.35, where the pulse's first null landed on the
  third partial and the body came out hollow -- partial 3 at -20dB with
  partial 4 at -5.
- `cutoff_env_octaves`, 3.4 octaves over the corner falling back with a 180ms
  time constant.

### Touch reaches the attack, not the body

The third of those needed a change, and it is the only new mechanism here
besides the strike itself.

`cutoff_env_octaves` is a fixed number of octaves above wherever the dynamics
have put the corner, so it moves the attack and the body by the same ratio.
On a struck instrument they do not move together at all: a soft note is very
nearly a sine with a hint of bell on the front, a hard one is a bell that
becomes a sine, and the body barely changes between them.  With the envelope
fixed, the attack's spectral centroid at 330Hz came out 1583, 1567 and 1604Hz
across the whole range of the strike -- a piano that does not care how it is
played.

So on a voice with `strike_s` set, `cutoff_env_octaves` is the *loud* end and
each note takes the share of it that it earned.  Re-armed rather than set
once, because at the onset itself the strike has not been measured yet, and
only ever upward, so it never holds the decay back.  Measured at 330Hz:

```
   breath      0.06  0.10  0.15  0.22  0.32  0.45   of the input
   attack       553   726   878  1096  1175  1105   Hz, centroid at 80ms
   body         344   342   353   345   356   359   Hz, centroid at 1.2s
   level      -30.1 -26.6 -23.8 -21.0 -18.1 -15.9   dB, at 50ms
```

The attack moves 2.1 to 1 across the range of the strike and the body barely
moves at all.  That is the touch of the instrument.

(These are measured with the tremolo off.  The decay, the note separation and
the partial levels below are too: they are properties of the envelope and the
oscillator, and an amplitude modulation on top of them only makes the
measurement noisy.)

One thing to know: the onset used to arm this envelope at full, which quietly
made every note's bell the same size -- the quietest note in the ladder came
out with a spectrum flat to its seventh partial, which is the *loudest*
note's attack.  A struck voice arms it from the strike instead.

The same window has to be gated on the note actually being played, not on
`note_age`, which is reset whenever the detector stops hearing anything.
Without that a tail under the sustain control sits at note_age 0 forever, the
contact re-opens, and the bell is re-armed on a note struck seconds ago.

### The decay, and what the compensation does to it

`decay_s` is 1.0s to a twentieth, which renders as -1.8dB at half a second,
-5.6 at one and -12.6 at two.

That number is set on the rendered result rather than on the parameter,
because the audibility compensation partly undoes it.  The note darkens as its
bell dies; a darker spectrum is worth less per unit of electrical power; the
compensation hands some of it back.  With the bark switched off the same
envelope renders -4.4dB at one second, and with it on -2.3.

```
   bark on,  decay 1.6     0.0   0.0  -0.4  -2.3  -4.6  -6.8  -8.3
   bark off, decay 1.6     0.0  -0.8  -2.0  -4.4  -6.7  -8.9 -10.5
                          0.05  0.20  0.50  1.00  1.50  2.00  2.40  s
```

So 2.2dB of the first second's decay is the compensation and not the
envelope.  That is the compensation doing exactly what it says -- holding what
a listener hears steady -- and it is worth writing down because it is the
reason this number is shorter than a real Rhodes' decay and still sounds like
one.  It is also the one place in the table where a voice's own timbre moving
inside a note reaches the loudness match; every other voice's spectrum is
settled by the time the note is.

### The damper

`release_s` is 25ms, short for this table, and it is the price of being
struck.  Every other voice's level collapses in the gap between two notes
because it follows the breath; this one's does not, because it is holding the
velocity it was struck at.  So the gate is the only thing separating two notes
and it has to do the whole job.  Over `in-scale.f32` -- 300ms notes with 60ms
gaps -- how far down the previous note is when the next one starts:

```
   rhodes, release 50ms    -7.6 dB        bass     -26.6
   rhodes, release 25ms   -13.9           drawbar  -23.5
   rhodes, release 18ms   -18.5           pluck    -36.2
```

Still the least separated voice in the table, which is what a felt damper
landing on a ringing tine is.  At 50ms a fast run was one sound.

### The pickup and the amp

A Rhodes pickup is an electromagnet a millimetre from a steel tine, and its
field is nothing like linear, so the instrument distorts on its own before
anything downstream does.  `drive_bias` is what puts the even harmonics in,
and with the strike latched the drive is the second of only two things the
velocity reaches.

`top_hz` is doing a different job from `drawbar`'s cabinet.  There the 8kHz
was the saturator inventing what no leslie could radiate, and the fix was to
stop radiating it.  Here the same band is the bell itself: taking the drive
from 0.7-2.2 down to 0.3-0.6, which is 7dB of level, moves the 8k octave by
0.7dB.  So this corner is shaping real content and has to be set by what the
instrument's own amplifier is rather than by where an artifact stops.  It is
2.6kHz -- see "Too harsh when played loudly" below, which is where that number
came from.

### Too harsh when played loudly

The first version of this voice was built and measured at the level the
reference recording was made at, and it was harsh on the speaker when played
hard.  That is the same mistake `drawbar` made, and this time it should have
been expected.

Three things were wrong and they are worth separating, because only one of
them is the one that shows up in a band table.

**The saturator, which was most of it.**  `drive_loud` was 2.2.  A saturator
on a *stretched* spectrum is not the same thing as one on a harmonic spectrum:
the intermodulation products land nowhere near the partials, so what a hard
drive makes here is dense inharmonic grit rather than warmth.  Measured on a
hard strike, energy sitting more than 3.5% away from any partial, as a
fraction of the note:

```
   drive 0.7-2.2   -12.8 dB        drive 0.7-1.3   -15.8 dB
```

An eighth of the note against a twenty-fifth.

**The bell opening too far.**  `cutoff_env_octaves` was 4.2 over a loud corner
of 1.6, which puts the corner at partial 28 -- 9kHz on a 330Hz note and past
Nyquist at the top of the range.  At full open a flat bank of 24 partials is
very nearly a sawtooth, and that is not a bell.  3.4 octaves over a corner of
1.2 keeps the bark and loses the buzz, and it costs nothing measurable: the
attack's velocity range is now 2.1 to 1 where it was 1.7 to 1, because the
body came down further than the attack did.

**The cabinet.**  4.5kHz was chosen on the theory that the bell needs the
room.  A suitcase Rhodes is four twelve-inch speakers in a wooden box with no
horn and no tweeter in it anywhere, and 2.6kHz is what that is.

Together, in octave bands over `recordings/whistling.f32`, relative to each
voice's own total:

```
              125    250    500     1k     2k     4k     8k
  drawbar    -8.7   -7.5   -9.8  -15.2  -21.8  -31.8  -50.6
  rhodes    -22.6  -10.9   -9.3   -8.9  -10.8  -16.5  -28.2   before
  rhodes    -21.0   -9.2   -8.4   -9.1  -12.7  -21.5  -37.3   now
```

**What could not be fixed this way is the 2k octave**, which moved 1.9dB and
will not move much further.  At the top of the range that band is the note
itself and its second partial, not the bell, so taking it out means taking out
the voice.  If this is still hot on the speaker the answer is the register --
an octave down, `octave` at 0.125 -- and not the cabinet.

One thing that is *not* a lever, and it cost a measurement to find out: the
whole band table barely moves with how hard the player is whistling.
Amplifying the reference recording by 1.6x and 2.4x moved the 2k octave by
0.4dB.  Loud playing is harsher because each *note* opens its bark further and
because the whole thing is louder, not because the voice's balance changes
with the phrase.

### Where the breath goes after the strike

`drawbar` had to answer this question because an organ has no dynamics at all.
This one has to answer it because the hammer has already left the string, and
a player leaning into a note that is still ringing has to be doing
*something*.  On a suitcase Rhodes that something is on the front panel: the
amplifier swings the note back and forth, and how far it swings is a knob.

So the breath is the **depth**.  Measured over the velocity ladder, each
note's envelope peak to trough with its own decay taken out:

```
   breath   0.06  0.10  0.15  0.22  0.32  0.45   of the input
   swing     3.4   4.3   5.6   7.8  11.8  19.3   dB
```

And on the two notes in `in-strike.f32` whose breath moves *after* the onset,
which is what this control exists for:

```
   struck at 0.10, breath rising to 0.45     7.0 dB  ->  13.6
   struck at 0.45, breath falling to 0.10   13.9     ->   8.9
```

Those same two notes still render at the level and the brightness they were
struck at, to a fraction of a dB.  The player is working the amplifier, not
the hammer.

**The rate is not on the breath.**  A suitcase has a rate knob and a depth
knob and a player sets the rate once for the tune.  This is the opposite of
the leslie, where the speed *is* the gesture -- an organist ends a phrase by
kicking the speed switch.  A tremolo whose rate moved while you played would
read as a fault rather than as expression.  5.5Hz, free-running, because an
amplifier does not restart its oscillator when a key goes down.

**`trem_soft` is zero.**  Quiet playing is a clean electric piano with no
tremolo in it at all, which is what the front panel looks like with the depth
knob down.  The effect is something the player switches on by leaning in
rather than something the voice always does.

**The depth must not become volume**, or the strike has been undone: leaning
into a ringing note would make it louder again, which is exactly what
`strike_s` exists to prevent.  A plain `1 + depth*sin` has an rms of
`sqrt(1 + depth^2/2)`, so the tremolo is divided by its own rms.  Over
`recordings/whistling.f32` the voice lands at -23.51 LUFS with the tremolo
following the breath, -23.51 with it switched off entirely, and -23.56 with
the depth pinned at 0.85.  What the breath changes is how much the note moves.

It costs peak headroom rather than level: 0.44 with the tremolo off and 0.53
with it on, because a depth of 0.85 puts about 4dB of crest on the note that
an integrating meter does not see.

### The sustain control is the sustain pedal

This is the one voice that gets that machinery for free.  The tail keeps the
strike's velocity, the decay goes on running underneath it, and what comes out
is a note still ringing down rather than a drone.  Over
`recordings/holding.f32`, a held note relative to where it was when the player
stopped:

```
                    +0.3s   +1.0s
   sustain on       -10.0   -14.9   dB
   sustain off      -59.0     gone
```

The snap to the nearest semitone, which every other voice has to be argued
into, is simply what a keyboard is.

### Loudness

The table's usual match: -23.5 LUFS through the K10.2 model over
`recordings/whistling.f32`, against `drawbar`'s -23.1 and `bass`'s -23.8, at
0.53 peak.  Unlike `pluck` it does not need the peak match instead -- its
notes ring for most of their length rather than being an attack and a gap, so
an integrating meter reads it where it belongs.  The peak is the tremolo,
which puts about 4dB of crest on a note that the integrated measurement does
not see.

Across pitch it is among the flattest things in the table, 0.3dB from the
bottom of the whistle range to the top against `drawbar`'s 0.5 and `square`'s
3.8.
Nothing here goes near the cabinet's corner and the compensation is barely
asked for anything.

### What is not modelled

**The tremolo is mono.**  A real suitcase pans between two speakers rather
than turning the level up and down, and summed to one speaker that is very
nearly nothing at all.  This table is mono through a K10.2 -- see the top of
this file -- so the effect has to be an amplitude swing, which is what every
mono recording of a suitcase sounds like anyway.

**The tonebar.**  A real tine is paired with a tuned steel bar behind it and
the two are coupled; that is where the long ring comes from.  Here the ring is
an envelope.  The beating in the second-partial region is real but arrives by
a different route -- see the stretch, above.

**Velocity reaching the decay time.**  On a real piano a hard note rings
longer than a soft one.  `decay_s` is one number.

### Renders for the speaker

The cabinet corner and the tremolo rate are settled by ear on the box this
gets played through rather than by measurement, which is how `drawbar` found
its own corner.  All six are rendered over `recordings/whistling.f32`
**amplified 1.6x**, because loud playing is the case that went wrong, and all
six are matched to -23.0 LUFS so the comparison is about tone and not level:

```
   rhodes-ab-a.wav   the preset
   rhodes-ab-b.wav   what it was: drive 2.2, bell 4.2 octaves, cabinet 4.5kHz
   rhodes-ab-c.wav   darker still: cabinet 1.9kHz, drive 1.1
   rhodes-ab-d.wav   tremolo slower, 4.0Hz
   rhodes-ab-e.wav   tremolo faster, 7.5Hz
   rhodes-ab-f.wav   tremolo deeper at the top, trem_loud 1.0
```

`-b` is the reference for how far this moved.  If `-c` wins, the 2k octave is
the thing still in the way and the register is the better answer -- see above.
`-d` through `-f` are one line each and change nothing else: the tremolo is
divided by its own rms, so depth and rate do not move the loudness match.

`whistling-rhodes-loud.wav` is the same amplified input through the preset at
its own gain, for judging it against the other voices rather than against
itself.

**The one thing a whistle cannot give it** is the strike itself.  A hammer
takes a couple of milliseconds; a whistled note is at a median 76% of its
level 30ms in.  So the bell blooms over the first few tens of milliseconds
rather than arriving, and no setting here changes that -- it is the same limit
`drawbar` ran into looking for the key click.

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
  drawbar     -20.0  -19.9  -19.9  -20.3  -20.4      0.5    0.27
  drawbar-hi  -19.8  -19.8  -20.1  -20.5  -20.4      0.7    0.27
```

`drawbar` sits about 3dB under the rest of that table and is matched to them
all the same: it is the one voice with no dynamics, so on a ladder played at
one level the others are at their loudest and it is where it always is.  The
match is the integral over a real performance, where they land together.  See
"The drawbar organ" above.

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
  python3 prototypes/make-input.py steps > prototypes/in-steps.f32
  python3 prototypes/make-input.py strike > prototypes/in-strike.f32
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
which is what the loudness match is measured over.  `in-strike.f32` is one
pitch at six velocities with real silence between the notes, plus a note that
swells after the strike and one that fades after it, which is how a struck
voice is shown to be struck.  `in-scale.f32` is a
two-octave chromatic climb repeated three times;
`in-glide.f32` is the same range as a continuous rise; `in-pad.f32` is six
steady notes spaced seconds apart.  The last two were made for the pads and
outlived them: `in-pad.f32`'s seconds of silence between notes are what
the tail was first measured in, and `in-padfast.f32` changes note every
200ms and then every 120ms.
