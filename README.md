# Usage

There are three ways in here, side by side:

* `zeros.c` -- the original single file, built as `zeros-linux` / `zeros-mac`.
* `zeros2.c` -- a rewrite split into `pitch.c`, `synth.c` and `engine.c`,
  built as `zeros2-linux` / `zeros2-mac`.  Everything below from "How it
  works" onwards describes this one.
* `mac/` -- the same engine as a Mac app, with a window instead of control
  files, CoreAudio instead of PortAudio, and stereo output.  See
  [mac/README.md](mac/README.md).

`zeros.c` and `zeros2.c` read the same control files and take the same
arguments, so `zeros2-mac` is a drop-in swap for `zeros-mac`.  The Mac app
compiles `pitch.c`, `synth.c` and `engine.c` unchanged and replaces only
`zeros2.c` -- the part that was the audio device and the control files.

## Build

1. Check out as ~/whistle-synth

2. Install dependencies:
   ```
   sudo apt install portaudio19-dev python3-evdev python3-mido python3-rtmidi
   ```

3. Build it:
   ```
    make zeros-linux     # or: make zeros2-linux
   ```

It will detect pitches and generate audio.

On a Mac, instead install `brew install portaudio` and `make zeros-mac` (or
`make zeros2-mac`).  It prefers a sound card whose name starts with
`USB_SOUND_CARD_PREFIX` and that does both input and output (a Scarlett,
say); failing that it falls back to the default input and output devices,
which on a Mac are separate.

To run on boot, `/etc/systemd/system/whistle-synth.service` should have:

```
[Unit]
Description=Pitch Detection and	Synthesis

[Service]
ExecStart=/home/jeffkaufman/whistle-synth/zeros-linux /home/jeffkaufman/whistle-synth/device-index /home/jeffkaufman/whistle-synth/current-voice /home/jeffkaufman/whistle-synth/current-volume /home/jeffkaufman/whistle-synth/current-gate /home/jeffkaufman/whistle-synth/current-fifth /home/jeffkaufman/whistle-synth/current-sustain
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

To support changing voices while headless,
`/etc/systemd/system/whistle-synth-kbd.service` should have:

```
[Unit]
Description=Keyboard Control for Pitch Synthesis

[Service]
ExecStart=/usr/bin/python3 /home/jeffkaufman/whistle-synth/kbd.py
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

Then:

```
sudo systemctl enable whistle-synth-kbd
sudo systemctl enable whistle-synth
sudo systemctl daemon-reload
```

Set levels for consistency:

```
$ alsamixer
> F6 select "USB Audio Device"
> F5 [All]
> Speaker: 100
> Mic: 100
> Capture: 100
```

## Run

1. Put on headphones, use a directional mic, or otherwise avoid letting the
   output of this program mix with the input.

2. Run it and whistle:
   ```
     make run-linux
   ```
Or
   ```
     make run-mac
   ```

It will generate audio.  `make run2-linux` and `make run2-mac` do the same
for the rewrite.

The rewrite's voices are selected by writing a number to `current-voice` (on
the Pi, keys 0-3 on the keypad):

| voice | what |
|---|---|
| 0 | raw input, passed through -- for checking mic level and gate |
| 1 | `bass` |
| 2 | `subbass` |
| 3 | `octaveless` |
| 4 | `reese` |
| 5 | `eight-oh-eight` |
| 6 | `pluck` |
| 7 | `fm` |
| 8 | `fm-sub` |
| 9 | `grind` |
| 10 | `square` |
| 11 | `flute` |
| 12 | `flute-low` |
| 13 | `flute-jet` |

These are the entries of the `presets` table in `synth.c`, in order, offset by
one.  `zeros2-mac` with no arguments prints the list, which is worth doing
rather than trusting this table: presets have come and gone.  1-10 are the
bass voices; `flute`, `flute-low` and `flute-jet` are the leads, and see "The
flute" below.  `flute-jet` is `flute-low` again as a physical model rather than
a measured spectrum -- see "The flute by physics".

Writing `1` to `current-fifth` drops every voice a just fifth, so what you
whistle is the fifth of what you hear rather than the root -- whistle a D and
it plays a G.  `0` puts it back.  It is a separate control from the voice
because the interval has nothing to do with the timbre: any voice can be
played at either.

Writing `1` to `current-sustain` makes a note you *hold* outlive the breath
that made it: it slides onto the nearest real note, settles under itself, sits
there, so one note every couple of bars holds a drone under a tune.  Notes too
short to have been meant that way are left alone, so a fast phrase sounds the
same either way and the tail only appears where you put it.  `0` puts it back.

The tail does not time out.  It ends when the next note takes it -- and since a
short note earns no tail of its own, **one short note is how you stop the
drone**: it takes the tail, then stops the way any short note stops.  A long
note moves the drone instead of ending it.  There is no clock to wait out and
nothing to top up.  Separate from the voice for the same
reason the fifth is: whether the line breathes with you or carries through is
not a question about timbre.  (`pluck` opts out -- see `no_sustain` in
`synth.h`.)

In the original, keys 0-8 select voices: 0 through 6 expect whistling, 7 and
8 singing.

## How it works

Everything from here on is about `zeros2.c`.

```
  microphone -> pitch.c -> hints -> synth.c -> speakers
```

`pitch.c` decides what the player is doing and `synth.c` makes the sound, and
they share no state.  That split is the whole design:

* The synth generates its own signal and **never reads the input**.  It only
  gets a `PitchHint`: a frequency, a confidence, a level, and a guess at
  whether a note is sounding at all.  When the detector is unsure -- which is
  always at the very start and end of a note, where the signal is weak and not
  yet periodic -- the worst it can do is hand over a slightly wrong number.
  It cannot put the microphone's own noise into the output, which is what an
  earlier version did and why notes used to garble as they started and
  stopped.
* Because the synth free-runs, **detection lag is not audio latency**.  The
  analysis window is 384 samples, so the synth learns about a pitch change
  about 4ms after it happens, but nothing is delayed on the way through; the
  round trip stays what the sound card gives us.

`pitch.c` uses YIN.  Plain autocorrelation peaks at every multiple of the true
period, which is where octave errors come from; YIN minimizes a difference
function normalized by its own running mean, which biases towards the shortest
period that explains the signal and produces a confidence number as a
by-product.  On top of that:

* a median of the last three estimates, to drop the isolated bad window
* hysteresis in both level and confidence, and separate counts of how many
  consecutive analysis hops it takes to start a note (4) versus end one (8) --
  a false start is audible, a slightly late release is not
* an octave guard that prefers continuity over a 2x jump, unless the reading
  is confident enough to be a real leap -- and not even then once the note is
  more than 12dB below its own peak, because a player leaps into a new note
  rather than out of the bottom of a dying one
* no pitch update at all from a window where nothing cleared the YIN
  threshold.  There is no periodicity in such a window to report, so the
  reported pitch holds instead

The last two are both about what happens as a note dies, and both were found
by listening rather than by measurement -- through `flute`, which is the voice
that exposes them.  A whistle does not stop, it decays, and on the way down
two different things went wrong:

* **The estimator ran out of range and was believed.**  With no lag under the
  threshold, the fallback reported the best lag anywhere in the search, which
  on near-noise tends to be the longest one -- and at a confidence around 0.55,
  comfortably over the bar for moving the pitch.  So a dying note slid to the
  lowest note the detector can express and held it: measured, a 1021Hz whistle
  handing over 551.7Hz, `min_hz` exactly, for the last 190ms of every note.
  Neither the octave guard nor the median caught it, because 1021 to 551.7 is
  not an octave and it persisted for tens of hops rather than one.
* **Confident octave errors.**  A periodic signal is genuinely periodic at half
  its frequency, so as the harmonics die out of a whistle the longer lag can be
  the first one under the threshold, and it is confidently wrong -- which is
  exactly the case the octave guard's escape hatch lets through.  What
  separates these from real leaps is level, not confidence: measured, every one
  sat 19.6 to 42.6dB below its own note's peak, while the deliberate-leap
  passage of `recordings/whistling.f32` produces no such jump at all.

Through a bass these are a thump at the end of a note; through `flute` they are
a tonal burble an octave down, and they are the last thing you hear.  Measured
over three real takes, with note counts unchanged and no gate decision altered:

```
                    readings pinned at min_hz    >300 cent jumps    of which octaves
  flute4                   121 -> 0                  14 -> 0            8 -> 0
  whistling                 21 -> 0                  15 -> 2            5 -> 0
  holding                   13 -> 0                  12 -> 5            3 -> 3
```

What is left in `holding` is a different fault and deliberately untouched: those
sit *at* the note's peak rather than in its decay, and one of them is a 4ms
round trip that corrects itself. 

The gate is **relative to the room, not absolute**.  A note has to stand a
given factor clear of the measured noise floor, and the floor is only updated
when what we're hearing doesn't look like a note -- not merely when it's
quiet, or a whistle fading in at the start of a phrase would drag the floor
up behind it and never trigger.  An absolute threshold cannot work here: how
hot the microphone runs depends on the mic, the preamp and how close you are,
and the first acoustic test of this code threw away a note the detector was
holding at 0.999 confidence purely because it was quiet.  The `current-gate`
number now means "how many times the room noise", so it means the same thing
on any rig.

Note that level and confidence are independent tests, and that's the point:
level rejects the room, periodicity rejects noise.  Opening the gate wide
still doesn't make the synth chatter at broadband noise, because noise fails
the other test.

Measured on the offline test signals: tuning within 1 cent in steady state, a
twelve-note staccato passage giving exactly twelve starts and twelve stops, a
note crawling up and back down through the gate over six seconds giving
exactly one of each, and a broadband noise burst at playing level giving
silence.  Measured through a real microphone (see below): 70 of 72 notes over
a 39dB-to-10dB signal-to-noise sweep, no false triggers, tuning within 1 cent
down to 19dB SNR and 8 cents at 10dB.  The two misses are a deliberately
quiet note buried in the worst noise, and the gate knob recovers them.

## The lead voice

The whistle is resynthesized an octave down, which puts a 588-3150Hz whistle
at 294-1575Hz, where a fiddle plays the tune.  The tone is a pulse wave built
additively -- partial n of a width-w pulse has amplitude `sin(n*pi*w)/n` --
with three detuned copies, then soft-saturated.  Additive because we can stop
before Nyquist and never alias, and because it makes the harmonic rolloff a
multiply rather than a filter to tune.

A slow LFO sweeps the pulse width so a held note never sits still.  Two
different things drive the expression, and they deliberately do different
jobs:

* **How hard you're blowing** sets loudness, brightness and drive together.
  It has to be this rather than note length: contra tunes are mostly runs of
  eighth notes, and hanging brightness off note length leaves every run dull
  and buried.
* **How long you've held the note** fades in a faster width wobble, so long
  notes grow a growl and runs stay clean.

Across an 18dB range of input level the output moves about 16dB and the
partials above the fourth open up by about 8dB, so it brightens faster than it
gets louder -- which is what makes it cut without just being loud.

`level_full` in `synth.c` is the input level that counts as playing full
tilt, and it is the one number here that is still absolute -- unlike the gate,
which is relative to the room.  It has to be: the room's noise floor says
nothing about how hard you are blowing.  Set it too high and the voice sits
permanently dark and quiet; too low and it is permanently maxed out with no
dynamics left.

You don't have to guess at it.  While running, the program prints the input
level it actually sees while you play, next to the value it is using:

```
  input level while playing: 0.0298 (level_full is 0.1000)
```

Set `level_full` to a bit above the level you see when playing hard.  Do it
by whistling, not from a `--self-test` run -- the levels there are a property
of the headphone volume knob, not of your whistling.

## The low voices

`trombone` and `bass` are the same engine pointed at different registers.
Which octave they sit in is the whole design; everything else follows.

* **trombone** drops three octaves, so a comfortable whistle lands around
  100-300Hz -- tenor trombone, with room for a fiddle above and a piano
  underneath.  Brass doesn't chorus, so the width sweep is nearly off, but it
  does growl, and the growl is already wired to note length.  Its defining
  trait is the brightness range: `cutoff_soft` 1.1 to `cutoff_loud` 14, the
  widest of any preset, because going from nothing to blazing as you lean on
  it is more recognizably brass than any particular harmonic recipe.
  Measured, the partials open up monotonically from +0dB at the fundamental
  to +8dB at the twelfth as you push.
* **bass** drops four octaves, landing around 50-160Hz, which is where an
  electric bass actually plays.  (Five octaves, which an earlier version of
  this program used, is below the low E.)  One oscillator, no detune, almost
  no movement: a wobbling bass fights the piano's left hand and turns to mud
  through a PA, and it's the note starting exactly on time that makes a bass
  line feel tight.

* **subbass** drops five octaves, which is where the old `ebass` voice lived
  and what this is here to bring back.  A comfortable whistle lands around
  25-80Hz, below the bottom of an electric bass -- felt as much as heard.

The old `ebass` had partials at 1, 2, 3.11, 4.3, 5.7 and 6.1 times the
fundamental with the **second louder than the first** (0.24 against 0.20),
and that ratio is most of its character: the octave up carries the note while
the fundamental carries the weight, which is why it read as pitched rather
than as rumble.  `subbass` reproduces that shape with a narrow pulse and an
almost flat `tilt` -- measured, the second partial leads the first by 2.4dB
against the original's 1.6dB, with the third and fifth within about 2dB of
the old ratios.  The old voice's partials 3-6 were also inharmonic, and that is where its
growl came from, so `stretch` reproduces it: partial n moves from n to
n*(1 + stretch*(n-1)), putting partial 3 at 3.12 against the original's 3.11
and partial 4 at 4.24 against 4.3.  On its own that is a static clang -- the
drive is what turns it into a growl, folding the partials together so the
difference tones land a few Hz from the harmonics, and a few Hz of beating is
what a growl is.  More stretch is not more growl: at 0.05 the beating pairs
separate and the modulation *falls*, which is why 0.02 is both the deepest
and the closest to the original.

That gets about 11% modulation, which is a throb rather than a growl, so the
width wobble carries the rest.  Measured above 100Hz, `subbass` modulates
13.2% against `bass`'s 3.8%, peaking around 4Hz.

A stretched preset can't use the Chebyshev recurrence -- partials that aren't
multiples of anything need a phase accumulator and a `sinf` each -- so it
costs 1.3% of a core against 0.9%, and only presets that ask for it pay.

Five octaves down needs a bottom limit, which `min_partial_hz` provides.
Partials below it are never synthesized -- but that alone isn't enough,
because saturating partials at 37 and 56Hz regenerates their 19Hz difference,
which measured only 4.8dB down.  So the output also gets a 12dB-an-octave
high-pass, set a little below the partial limit so it clears out what the
drive invented without thinning the lowest partial we chose to keep.  This is
what a bass amp does, for the same reason: below about 20Hz nothing in the
room reproduces it, and the headroom goes into moving woofers rather than
into sound.

A pulse wave rolls off at 6dB an octave, which is far steeper than brass, so
`tilt` is the exponent on that `1/n`.  1.0 is a true pulse; the trombone uses
0.7 to flatten the envelope and put energy above the fundamental where brass
keeps it.

Both needed one structural change: `unison` is per-preset, because three
detuned copies at equal phase spacing sum to *nothing* at the fundamental, so
a bass that wants no detune has to ask for one oscillator rather than three
with the detune set to zero.

## The flute

The one voice here that is not a bass, and the only one whose numbers all come
from recordings of the instrument rather than from a design.  `flute2.f32` is
a single held C5 and `flute3.f32` the same note whistled, which is the A/B
pair everything is checked against; `flute.f32` is forty seconds of long tones
with a crescendo in it.  The partial tables were fitted over those plus a
descending octave of held notes, a take that has since been overwritten -- the
harmonics measured off it are kept in `prototypes/flute-harmonics.tsv`, which
is now the only record of them.

It plays **one octave down**, not four.  A whistle covers 588-3150Hz, which
lands at 294-1575Hz -- D4 to G6, very nearly a concert flute's range, and a
comfortable whistle sits at 400-800Hz, the register the recordings are played
in.  So the measurements are taken where the voice actually plays.

Three things make it unlike everything else in the table.

**Its partials come from a table, not from the pulse formula.**  Setting
`register_hi_hz` switches `pwm_center`, `tilt`, `cutoff_*` and `rolloff_exp`
off entirely and reads the partial amplitudes out of `partial_lo`/`partial_hi`
instead.  The formula could not make this shape at all: the flute's seventh
partial sits *above* its sixth, and `sin(n*pi*w)/n^tilt/(1+(n/c)^k)` is
monotone by construction.  Fitted as well as it can be, the formula left the
audible partials 5.1dB rms out.

**There are two tables, because a flute is not a fixed set of ratios.**  Its
second partial measures 11.3dB under the fundamental low in the range and
24.1dB under it high up: the instrument gets dramatically purer as it climbs,
and one spectrum for the whole range makes the top far too rich.  The played
pitch blends between the two tables from 580 to 700Hz, which is D5 to F5 --
where a flute actually changes register.

A body resonance fixed in Hz was the other candidate for this and the
recordings rule it out.  Fitted against nine notes as a source-times-body
model, the body comes out flat to within 0.4dB and improves the fit by 0.04dB;
indexed by absolute frequency alone it is twice as bad as indexing by partial
number.  What is left after the register blend -- about 4.5dB rms -- is
note-to-note variation, and it is real: a flute's harmonics genuinely differ by
several dB depending on the fingering, and nothing indexed by pitch follows
that.

**It gets rounder as it gets louder, not brighter.**  Every other voice here
opens up when pushed.  Over the one real crescendo in the recordings -- 15dB
on a held C5 -- every partial above the fundamental falls about 6dB relative
to the fundamental, which is what `purity_loud` applies.  That is what a
controlled crescendo on a flute is: the player opens the embouchure and moves
more air rather than blowing faster, which is also what keeps the pitch from
rising.  `trombone` ran its filter 1.1 to 14 in the other direction, because
going from nothing to blazing is what brass does.

It is also slow to speak and quick to stop -- 60ms to arrive, 48ms to fall
20dB -- against 3-8ms attacks everywhere else, and it has neither vibrato nor
any wobble, because the recordings are straight tone: the pitch holds to 2-3
cents rms over a held note and the level to 0.20dB.

### The air

`flute` is what brought `breath` back to life; until it, no preset set it and
the noise generator and filter behind it were dead code.  Getting it wrong was
much the most audible fault in the first version of this voice, and the fault
was not the level but the **band**.

A single state variable bandpass falls at 6dB an octave on paper, and measured
its own skirt is only 10dB down at 20kHz.  That put a flat hiss shelf across
the whole top of the spectrum: against the real note the synthesized air was
10dB hot at 5kHz, 23dB at 8kHz and 29dB at 12.7kHz.  Broadband HF noise is
about the most salient thing an ear picks out, and it is why the voice read as
a synth with tape hiss over it.

What the recordings actually show is a band **fixed in Hz**: a plateau from
about 1.5 to 4kHz falling 18-20dB an octave above that, and it does not move
with the note.  Across four notes an octave apart the between-harmonic noise
sits in the same place every time, which makes sense of where it comes from --
the noise is made at the embouchure and in the player's mouth, neither of
which changes size with the note.  It also does not move with how hard the
flute is played: over that same 15dB crescendo the tone-to-air ratio reads
25.3dB at the quietest and 24.8dB at the loudest.

So the band is now a highpass at 1600Hz into two two-pole lowpasses at 2800Hz,
both edges steep, fitted to the measured curve within 2.1dB rms.  Getting the
top edge took all of that: three cascaded one-poles were tried first and gave
only 12.5dB an octave, because a one-pole's response flattens as it approaches
Nyquist -- one stage is barely 10dB down at 24kHz however far past the corner
you go, so stacking them buys much less than the 6dB an octave each promises.

### Where it lands

Against the held C5, rendered from the whistle of the same note and compared
in third-octave bands scaled to each one's own fundamental:

```
  first version   14.4 dB rms
  now              3.2 dB rms
```

Harmonic by harmonic against that note, the second is within 1.4dB, the third
2.7, the fifth 1.8, the sixth 1.9 and the seventh 0.8 -- including the seventh
sitting above the sixth.  The fourth is 8dB out, and that one is the
instrument rather than the model: this note has the strongest fourth partial
of the nine measured, at -28.7dB against a -35.0dB average.

Across the range, rendered at each pitch there is a real note for, the error
over partials 2-5 is 5.0dB rms -- which is the note-to-note variation floor,
not a modelling error.

### An octave down

`flute-low` is the same instrument an octave lower -- an alto or bass flute,
landing a whistle at 147-787Hz.  There is no recording of one to fit against,
so everything that was measured stays exactly as measured and only the two
things that are structurally wrong at that size are changed.

**The register break halves, to 290-350Hz.**  Where a flute breaks register is
a property of where a note sits in the instrument's own range, not of its
absolute frequency, and a flute an octave down breaks an octave lower.  Left
at `flute`'s 580-700Hz, all but the top fifth of this voice's range would read
as low register and a phrase would come out with a different timbral shape
from the same phrase on `flute`, which is not what "the same instrument,
lower" means.  Halved, it is exact -- measured over the same whistle through
both voices, the harmonics land within 0.2dB of each other at every pitch:

```
  whistled     660    933   1320   1867   2640
  flute h2   -13.7  -13.7  -22.5  -26.5  -26.8
  low   h2   -13.7  -13.7  -22.5  -26.4  -26.6
```

**The air band moves down**, to a 1000Hz bottom corner against `flute`'s 1600.
The band is fixed in Hz because it is made at the embouchure and in the
player's mouth, but a bass flute's embouchure hole is roughly twice the size.
Left where it was, the band would sit 8-15 partials above this voice's
fundamental instead of the 3-7 it measures on a concert flute, and air that
far above the note reads as hiss beside it rather than as breath in it.

1000Hz rather than 800 is the one number in either flute preset that is
neither measured nor derived: the instrument scales by two and the player's
mouth not at all, so the truth is between unchanged and halved.  It is the
first thing to adjust by ear.

The envelope is deliberately untouched, though the physics says a larger air
column speaks more slowly.  It surely does; by how much is a guess without a
recording, and a wrong guess costs fast playing.

## The flute by physics

`flute-jet` is `flute-low` again -- the same alto flute, a whistle landing at
147-787Hz -- built the other way round.  `flute-low` is a measured spectrum
played back: two tables of partial amplitudes, a register blend between them,
an attack in milliseconds.  `flute-jet` has no spectrum in it at all.  It is a
tube one wavelength long with a jet blowing across the end of it, and what
comes out is whatever that does.

The whole voice is one number -- how hard the tube is blown.  Nothing in the
preset says how loud the second partial should be, how fast the note should
arrive, or which way the timbre should move when the player leans in.

There is a third recording behind it, `flute5.f32`: four seconds of silence
for the room, a level check, and then a run of tongued notes on the real
flute, which is what the transients are measured against.  Its steady note
also confirms what the earlier takes said about the air -- the same plateau
from 1.5 to 4kHz -- so that band is carried over unchanged.

### The loop

One delay line and four filter stages, once round per period:

```
  the tube      a delay line, one period long
  the losses    two one-poles at 0.8 times the note
  the cutoff    two high-pass stages at 0.5 times the note
  the open end  the sign flip
  the jet       tanh of the acoustic field at the embouchure, offset
```

The sign flip is what makes it a flute rather than a clarinet: two inversions
a round trip, so the loop is non-inverting and every harmonic is supported
rather than only the odd ones.  The jet is the only thing in it that is not
linear, so it is the only thing that makes harmonics at all, and its offset --
0.7 -- is what puts the even ones in.  An exactly centred jet is an odd
function of the acoustic field and can only make odd harmonics.

**Every stage is divided by its own gain at the played pitch.**  That leaves
the round trip lossless at the fundamental however dark the filter is above
it, which is what makes the breath mean the same thing at every pitch: the
voice speaks at the same point on the dial everywhere, and every audible
partial lands within 0.8dB of itself from 233Hz to 660Hz.

**Every stage's delay comes out of the delay line**, worked out from its own
phase response rather than fitted.  With them the loop's linear resonance
lands on the requested pitch to better than a hundredth of a cent at every
frequency checked; the high-pass term alone is worth 45 cents, because
`1 - z^-1` leads by a quarter turn and a quarter turn is a fixed fraction of a
period wherever the note is.  Rendered, the voice plays +4.3 to +6.5 cents
sharp across the whole range and the whole of its dynamics.

**Both corners track the note rather than sitting fixed in Hz.**  That is a
property of the tube and not a convenience -- a low note is played on a longer
tube, so the same loss per metre compounds over more of it -- and it is also
what makes the voice play in tune.  Fixed in Hz, a low note's partials run
round a loop that barely damps them, come back with the wrong phase because a
one-pole's delay is not the same at every frequency, and the jet mixes them
back down onto the fundamental and drags it flat.  Measured that way the
bottom of the range played 30-65 cents under the top's 10.

### What had to be given up

**The jet is the whole return path**, rather than an addition to a reflection
the way it is in Cook's flute, which this started as.  A tube that reflects on
its own has a Q and goes on ringing after the breath stops; this one does not,
so its release is the breath leaving rather than the tube emptying.

That buys the thing that matters more, which is that there is exactly one loop
and so exactly one mode.  With a reflection path beside the jet the two loops
are different lengths, and which of them wins depends on how hard you blow:
measured, one note came out at 200Hz, 312Hz and 613Hz at three breath
pressures with nothing else changed.  Putting the jet's transit delay back in
to pick the mode, which is its job on the real instrument, brings the same
chaos with it -- a note asked for at 392Hz came out 250 cents off.

**The breath is compressed on the way in**, and that curve is the one number
in the voice with no physics behind it.  A waveguide near its threshold has
almost no output, so the whole musical range has to fit between speaking and
the top of the dynamics, and there is not much room in there.  Measured
against `flute-low` on the same material:

```
  exponent   crescendo   tail under the note   of the take sounding
    0.50        4.5dB          -2.0dB                  68%
    0.65        5.9dB          -4.3dB                  54%
    0.80        7.4dB         -32.2dB                  32%
    1.00        9.8dB          -8.1dB                  21%
  flute-low    11.2dB          -3.8dB                  69%
```

0.65 tracks `flute-low`'s sustain tail most closely and keeps more of the
dynamics than 0.5 does.  What it costs is the quietest playing on an
under-level input -- the take those last two columns are measured on averages
0.033 against this voice's `level_full` of 0.22 -- where this voice stops and
`flute-low` only gets quieter.  It is more sensitive to the mic level than
anything else in the table, and that is a real difference rather than a tuning
fault.  The direction of the curve is at least what the physics expects: what
the loop gain follows is the speed of the jet, and jet speed goes as the
square root of the pressure behind it.

### Where it lands

It gets the behaviour and misses the spectrum.

The note arrives when the loop has built rather than when an envelope says so,
and it stops when the tube stops:

```
                    attack     release        release
                  (to half)   (to -6dB)     (to -20dB)
  real flute       20-25ms     34-39ms        64-72ms
  flute-jet        25-30ms        39ms        73-87ms
  flute-low           36ms        26ms           62ms
```

Both are slower for a low note than a high one -- 29.5ms at the bottom of the
range against 25.2 at the top, and 87ms of release against 73 -- because the
loop is a period long and a low note's period is longer.  No number in the
preset asks for that; it is the only voice here where the envelope is a
consequence rather than a setting.

Pushed harder it gets **louder and rounder**, which is the measured
crescendo's direction and backwards from every other voice: over the same
15dB crescendo the second partial goes from -14.8dB to -18.9.  The third goes
the other way, from -27.0 to -19.9, which the real instrument does not do.

What it misses is the steady spectrum, by 9.6dB rms against the measured
table where the additive voice is exact by construction:

```
                 h2     h3     h4     h5     h6     h7
  measured    -11.3  -19.6  -35.0  -37.3  -50.6  -47.4
  flute-jet   -18.9  -19.9  -26.7  -34.4  -33.7  -54.1
```

Most of that is one thing.  A real flute's fourth partial sits 15dB under its
third, and no smooth nonlinearity in a smooth loop makes a notch: driven to
where the third partial is right, the fourth comes out within a few dB of it
every time.  That is the same wall the pulse formula hit before the measured
tables replaced it, and it is why `flute-low` exists in the form it does.

So the two are worth having side by side rather than one replacing the other.
`flute-low` is right about what a flute sounds like; `flute-jet` is right
about what one does.

## Loudness

Every preset is matched to the same loudness on a full-range system: -13.8
LUFS, integrated, measured over the same test signal.  They land within 0.0dB
of each other, so changing voice mid-tune doesn't jump.  `flute-jet` was
matched the same way and against `flute-low` directly, since the two are the
same instrument and the swap between them has to be inaudible in level:
measured over `prototypes/in-ladder.f32` at full volume they agree to 0.0dB,
with `flute-jet` peaking at 0.361 against `flute-low`'s 0.215 -- a near-sine
carries more of its level in the peak.

LUFS (ITU-R BS.1770) is used because it is the standard model for *perceived*
loudness rather than for signal level, which is the whole difficulty here: a
sub-bass and a lead at the same RMS do not sound remotely alike.  Matching it
is why `out_gain` differs so much between presets -- `subbass` is set to
0.573 and `bright` to 0.379 to arrive at the same place.  So `out_gain` is
not a taste control: changing one means re-running the match, not just
adjusting that voice.

The peaks that fall out of this range from 0.36 to 0.75 at full volume.  The
headroom target is 0.75 rather than something closer to 1.0 because
`loudness` is capped, so whistling harder than the test signal can still add
about 14% -- which puts the worst case at 0.854, and nothing clips.

Note that this matches loudness on a system that can actually reproduce the
low voices.  On small speakers, or on the headphone rig (see the rig check
below, which is 16-22dB down across `subbass`'s range), the low voices will
sound much quieter than the leads.  That is the monitoring, not the mix.

## Developing voices offline

`make zeros2-offline` builds a renderer that runs the same engine over a file
instead of a sound card:

```
  ./zeros2-offline <voice> <volume> <gate> [fifth] < in.f32 > out.f32
  ./zeros2-offline <voice> <volume> <gate> --trace < in.f32   # hints as TSV
```

Raw mono 32-bit float at `SAMPLE_RATE` in and out; `ffmpeg -f f32le -ar 48000
-ac 1` converts to and from wav.  This is much faster to iterate on than
whistling at it: `ffmpeg`'s `showspectrumpic` shows what a voice is doing, and
`--trace` shows what the detector thinks, which is usually the faster way to
find out why something sounded wrong.

A new preset is an entry in the `presets` table in `synth.c`, not a new code
path.

## Testing it against a real microphone

Point the microphone at the headphone and:

```
  ./zeros2-mac --self-test device-index recording.f32 2
```

It plays a synthetic whistle out the headphone -- clean first, then at three
increasing noise levels -- lets it come back in through the microphone, runs
it through the engine as normal, and records three channels: what it played,
what the microphone heard, and what the synth made of it.  The synth is
recorded rather than played, because the microphone is listening to the
speaker and playing the synth would feed it back into the detector.

It also writes a `.sections` file saying what was playing when, so detection
can be scored against ground truth rather than judged by ear.  Extract the
microphone channel and feed it back through `zeros2-offline --trace` to
re-examine a run under different settings without replaying it -- the
realtime and offline paths produce bit-identical output from the same input,
which is checked and worth keeping true.

This is also how to measure the acoustic round trip: cross-correlate the
stimulus channel against the microphone channel.  It came out at 5.15ms,
against the 5.0ms in the latency table below.

### Checking the rig itself

```
  ./zeros2-mac --rig-check device-index rig.f32
```

Plays tones an octave apart from 41Hz to 3520Hz and records what comes back,
which says what the speaker-to-microphone path is doing rather than what the
synth is doing.  Worth running before trusting the rig, and worth knowing
before judging the low voices by ear on headphones.  Measured on a
microphone resting against a headphone, relative to 880Hz where the whistle
lives:

| tone | relative |
|---|---|
| 41 Hz | -21.9 dB |
| 55 Hz | -16.4 dB |
| 110 Hz | -7.4 dB |
| 165-440 Hz | about -5 dB |
| 880 Hz | 0 dB |
| 1760 Hz | +5.1 dB |
| 3520 Hz | +8.6 dB |

An unsealed headphone can't couple low frequencies to the air and a dynamic
vocal mic rolls off its bottom end, so the rig under-reports the bass voice's
fundamental range by 16-22dB.  That is a property of the rig, not of the
synth: the self-test never plays the synth, only the whistle stimulus, and
the recorded synth channel comes back bit-identical to an offline render.
But it does mean these headphones will badly under-sell `bass`, and to a
lesser extent `trombone`.

## Microphone tips:

* Works best with a directional microphone with a windscreen (vocal mics like
  the E835 or SM58 have one built in).

* I use a Sennheiser E835 with an xlr to 3.5mm adapter into a USB
  sound card.  This isn't how the microphone is designed to be used
  (it wants a pre-amp) but it works well enough and it's nice not to
  have another piece of hardware.

* You want to be as close to the microphone as you can bear.

## Raspberry PI Setup

1. Install Raspberry Pi Os Lite (we don't want the desktop environment)
1. `sudo apt-get update && sudo apt-get upgrade`
1. `sudo raspi-config`
    1. "Interface Options"
        1. "Enable SSH"
    1. "Localisation Options"
        1. "WLAN Country"
    1. "System Options"
        1. "Wireless LAN"
1. Add regular public key to `~/.ssh/authorized_keys`
1. Change default password (`passwd`) 
1. `sudo apt install git emacs`
1. https://www.jefftk.com/p/you-should-be-logging-shell-history
1. `alsamixer`
    1. select sound card "USB Audio Device"
    1. Set Speaker, Mic, and Capture to 100% volume

## Latency

Tuned for a Mac with a Scarlett 2i2.  On startup it prints the latency it
actually got, and prints a running `xruns:` count if it can't keep up.

Three things matter, in order:

1. **PortAudio's callback API**, not the blocking API.  `Pa_ReadStream` /
   `Pa_WriteStream` layer their own ring buffers on top of the callback
   machinery and never got below about 27ms no matter how they were tuned.
2. **`suggestedLatency`**, with `paFramesPerBufferUnspecified` so the host
   API hands over its native buffer size.  This used to be set to the
   device's `defaultLowInputLatency`, which sounds low but isn't --
   PortAudio sizes its buffers from it.
3. **`paMacCorePro`** (mac only).  Without it CoreAudio keeps its own
   sample rate and buffer size and quietly converts; with it the device
   gets reconfigured to match us.  Note this changes the device's buffer
   size for other apps while we're running.

Measured acoustic round trip on a Scarlett 2i2, output to headphones and
back in through the mic:

| config | round trip |
|---|---|
| blocking API, device "default low" latency | 80.1ms |
| blocking API, minimum buffers | 27.6ms |
| callback API, 128-frame buffers | 16.0ms |
| callback API, frames unspecified, 44100 | 6.5ms |
| callback API + `paMacCorePro`, 48000 | **5.0ms** |

The hardware floor is 176 frames (3.7ms at 48k) of fixed converter and
safety-offset latency, from `kAudioDevicePropertyLatency` and
`kAudioDevicePropertySafetyOffset`, so there is not much left to win.
Dropping PortAudio for raw AudioUnits would be chasing the ~1ms between
5.0ms and that floor.  Asking for buffers below ~20 frames measures the
same round trip and can stall the callback outright.

`SAMPLE_RATE` is 48000 because the pipeline is a fixed number of *frames*
deep, so a faster rate is fewer milliseconds.  96k and 192k were measured
and bought only another 0.4ms for 2-4x the CPU.  The
pitch-detection range is given in Hz (`ENGINE_MIN_HZ`, `ENGINE_MAX_HZ`) and
converted to periods at startup, so changing the rate leaves it alone --
though `PITCH_MAX_PERIOD` bounds how low the range can reach.

That table is the *audio* round trip and it is still what it says: the synth
free-runs, so nothing is buffered on the way through.  On top of it the synth
learns about a pitch change about 4ms late, because the detector needs a
window to see a pitch at all (`PITCH_WINDOW`, and it prints this at startup).
A shorter window responds sooner and detects worse, and it can't go below a
couple of periods of the lowest note you want to play.

### Future

The mac-specific latency work is under `#ifdef __APPLE__` so the linux
build should still compile, but the linux builds have not been built or run
since any of it landed -- treat it as untested.  If you go back to the
Pi, `SUGGESTED_LATENCY` is the knob to raise until the xruns stop.  See http://tedfelix.com/linux/linux-midi.html and
https://wiki.linuxaudio.org/wiki/raspberrypi

