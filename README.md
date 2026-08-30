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
| 1 | `lead` |
| 2 | `trombone` |
| 3 | `bass` |
| 4 | `subbass` |

These are the entries of the `presets` table in `synth.c`, in order, offset by
one.  `zeros2-mac` with no arguments prints the list, which is worth doing
rather than trusting this table: presets have come and gone.

Writing `1` to `current-fifth` drops every voice a just fifth, so what you
whistle is the fifth of what you hear rather than the root -- whistle a D and
it plays a G.  `0` puts it back.  It is a separate control from the voice
because the interval has nothing to do with the timbre: any voice can be
played at either.

Writing `1` to `current-sustain` makes a note you *hold* outlive the breath
that made it: it slides onto the nearest real note, settles under itself, and
stays there until the next note or until you write `0` -- it does not expire,
so one note every couple of bars holds a drone under a tune and one note left
alone holds it all day.  Notes too short to have been meant that way are left
alone, so a fast phrase sounds the same either way and the tail only appears
where you put it.  `0` puts it back.  Separate from the voice for the same
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
  analysis window is 384 samples over the ordinary whistle range, so the synth
  learns about a pitch change about 4ms after it happens, but nothing is
  delayed on the way through; the round trip stays what the sound card gives
  us.  The window follows the bottom of the range it has been pointed at --
  see `pitch_set_trigger_range` -- so a range reaching down to F3 uses 1152
  samples and hears about a change 12ms late instead.  The command-line build
  never moves it and is always the first case.

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
  is confident enough to be a real leap

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

## Loudness

Every preset is matched to the same loudness on a full-range system: -13.8
LUFS, integrated, measured over the same test signal.  All six land within
0.0dB of each other, so changing voice mid-tune doesn't jump.

LUFS (ITU-R BS.1770) is used because it is the standard model for *perceived*
loudness rather than for signal level, which is the whole difficulty here: a
sub-bass and a lead at the same RMS do not sound remotely alike.  Matching it
is why `out_gain` differs so much between presets -- `subbass` is set to
0.573 and `bright` to 0.379 to arrive at the same place.  So `out_gain` is
not a taste control: changing one means re-running the match, not just
adjusting that voice.

Three voices are then deliberately turned down from the matched value:
`reese` by one volume step and the two drawbars by two, in the same
3.5dB-a-step units the volume knob uses.  Equal LUFS is equal loudness for
material of the same kind, and a sustained organ pad through a leslie is not
doing the job a plucked bass line is -- matched on the meter it sits on the
ear as a wall.  The offsets are the only taste in these numbers, they are
written down where the values are, and re-running the match means re-applying
them rather than replacing them.

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
window to see a pitch at all (`d->window_len`, and it prints this at startup).
A shorter window responds sooner and detects worse, and it can't go below a
couple of periods of the lowest note you want to play -- which is why the
window is now chosen from the range rather than fixed: ask the detector for
notes an octave lower and it takes the window, and the lag, that finding them
requires.

### Future

The mac-specific latency work is under `#ifdef __APPLE__` so the linux
build should still compile, but the linux builds have not been built or run
since any of it landed -- treat it as untested.  If you go back to the
Pi, `SUGGESTED_LATENCY` is the knob to raise until the xruns stop.  See http://tedfelix.com/linux/linux-midi.html and
https://wiki.linuxaudio.org/wiki/raspberrypi

