# Whistle Synth for macOS

The engine from `zeros2.c`, with a window instead of control files and
CoreAudio instead of PortAudio.

`pitch.c`, `synth.c` and `engine.c` are compiled **unchanged** from the
repository root and shared with the command-line build.  Only the outer layer
is new: what used to be `zeros2.c` -- the audio device, the control files, and
`main` -- is now `mac/core/` plus a SwiftUI front end.

```
  pitch.c  synth.c  engine.c        the engine, shared verbatim
        |
  mac/core/whistle.c                owns the engine; UI <-> audio thread handoff
  mac/core/whistle_audio.c          CoreAudio device list and AUHAL streams
        |
  mac/WhistleSynth/*.swift          the window
```

## Building

**Without Xcode** (Command Line Tools are enough):

```
./mac/build.sh
open mac/build/WhistleSynth.app
```

That produces a universal (arm64 + x86_64), sandboxed, ad-hoc signed bundle.
Ad-hoc signing is real signing as far as the sandbox is concerned, so the
entitlements and the microphone prompt behave exactly as they will in the
shipped app -- but the signature changes on every build, so macOS treats each
build as a new app and asks for the microphone again.

**With Xcode**, which is what you need for the App Store:

```
open mac/WhistleSynth.xcodeproj
```

The project compiles the same files with the same settings.  `mac/build.sh`
exists so the app can be built and read as one file on a machine with no Xcode
on it; the two are meant to stay equivalent, and if you change one, change the
other.

Two settings exist only to keep them equivalent, and both are invisible until
they bite.  `GCC_OPTIMIZATION_LEVEL = 2` is set explicitly in the Release
configuration because an Xcode Release that sets none does **not** inherit
`-O2` -- it falls through to `-Os`, which would compile the pitch detector and
the synth engine a step below what `build.sh` gives them.  And
`ENABLE_APP_SANDBOX = YES` is set because that is the setting Signing &
Capabilities reads: the entitlements file is what actually sandboxes the app,
but a pane showing no App Sandbox capability on a sandboxed app is a
disagreement someone eventually resolves in the wrong direction.

## Before submitting to the App Store

**Signing is set up.**  `DEVELOPMENT_TEAM = C58DMTY76R` (an Individual, paid
membership), `CODE_SIGN_STYLE = Automatic`, bundle identifier
`com.jefftk.WhistleSynth`.  Setting the team is also what issues the
certificate: there were no signing identities on this machine until it was
set, and the next archive created one with no other action.

`CODE_SIGN_IDENTITY` is deliberately left unset.  With a team present it
resolves to Apple Development on its own, and pinning it to Apple Distribution
would break ordinary builds and buy nothing, because the Organizer's
**Distribute App ▸ App Store Connect** step re-signs with the distribution
identity and a Mac App Store profile at export time.

**The project has been archived**, so it is no longer a parallel truth nobody
has run:

```
xcodebuild -project mac/WhistleSynth.xcodeproj -scheme WhistleSynth \
  -configuration Release -destination 'generic/platform=macOS' archive
```

Universal, signed to the team, exactly the two entitlements with no
`get-task-allow` (archives strip it, which is what the store requires),
hardened runtime on, and no warnings.  Two things about that command are
worth knowing, because both are invisible until they bite:

* **A plain `build` action is not a rehearsal for an archive.**  With the
  default destination it produces **arm64 only**.  It is `archive`, or an
  explicit `-destination 'generic/platform=macOS'`, that gets both slices.
* **The scheme is autocreated, not shared.**  There is no
  `xcshareddata/xcschemes` in the project, so `WhistleSynth` exists on
  whatever machine has opened the project and is not in the repository.
  Check "Shared" in Manage Schemes if the archive ever needs to run from a
  clean clone.

What is left is all outside this repository.  **Every field of it, written out
and ready to paste, is in `mac/store/app-store-connect.md`**, with the
screenshots beside it in `mac/store/screenshots/`; what follows is the summary
of why each one is there.

1. **The App ID is not registered, and no provisioning profile exists.**
   Automatic signing registers `com.jefftk.WhistleSynth` and fetches the
   profile the first time Distribute needs them.
2. **Distribution needs two certificates**, and the second is the one that
   gets forgotten: `Apple Distribution` signs the `.app`, and **Mac Installer
   Distribution** signs the `.pkg` that is actually uploaded.
3. **App Store Connect**: a support URL, macOS screenshots, and the App
   Privacy questionnaire, which can honestly be answered **Data Not
   Collected** -- that is what `PrivacyInfo.xcprivacy` says and what the usage
   string says.
4. **App Review notes, which this app should not be submitted without.**  It
   refuses the built-in microphone into the built-in speakers (see
   "Headphones, and the one route it refuses"), so a reviewer who opens it on
   a bare MacBook sees "Headphones needed" and cannot make it produce a
   sound.  Say so in the notes in as many words -- connect any headphones and
   whistle -- and attach a demo video.
5. **The icon** is generated by `mac/tools/make-icon.swift` -- a sine wave
   turning into the pulse wave the synth is built from.  It is honest about
   what the app does and it is legible at 16px, but it is a placeholder, not a
   designed icon.  Re-run that script after editing it; it writes both the
   asset catalog Xcode uses and the `.icns` that `build.sh` uses, so the two
   cannot drift.

Everything else is submission-ready: the sandbox is on, the entitlement list
is minimal, the usage string says what the microphone is for, there is no
private-framework link and no dynamic library to embed, the app ships a
privacy manifest, and the Help menu opens a real window.

## Sandbox and permissions

The entitlements are two lines:

```
com.apple.security.app-sandbox        required for the App Store
com.apple.security.device.audio-input the only capability the app needs
```

No network, no file access, no user-selected files, no hardened-runtime
exceptions.  Dropping PortAudio is part of why: it lived in
`/opt/homebrew/lib`, which a sandboxed app cannot load from, and the
command-line build linked it against `/System/Library/PrivateFrameworks`,
which the App Store rejects outright.  `whistle_audio.c` uses only public
AudioUnit and CoreAudio HAL calls.

The entitlement is permission to *ask*.  `AVCaptureDevice.requestAccess`
is the asking, and until it is granted the input renders silence rather than
failing, so the app checks the authorization status itself and shows an
explanation instead of a dead meter.  If it was denied, the button goes to the
right pane of System Settings, because an app cannot re-prompt once refused.

That URL is version-specific and easy to get wrong.  Ventura moved the anchor
into an extension:

```
x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Microphone
```

The pre-Ventura `com.apple.preference.security?Privacy_Microphone` lands on
the top of Privacy & Security at best.  The deployment target is 13.0, so the
new one is used, with the old one tried only if the new URL will not open at
all -- a button that appears to do nothing is the worst thing to hand someone
who is already stuck.

Granting the microphone in System Settings prompts "Quit & Reopen" or "Later",
and someone who picks Later comes back to this window.  So the status is
re-checked on `NSApplication.didBecomeActive` and the stream starts if it
flipped, and there is a "Check again" button next to the Settings one.  Read
once at launch, that screen stayed dead for the rest of the run with nothing
on it but the button that had already been pressed.

## Headphones, and the one route it refuses

**The app will not run on the built-in microphone into the built-in
speakers.**  Not a warning -- it does not start, and the window says why until
something changes.

A Mac's speakers are a few inches from its microphone and pointed at it, so
the synth hears its own output, tracks it as if it were a whistle, and howls
inside a second.  Every voice does it and no gate setting helps: the detector
is following a real signal, it is just the wrong one.  Passthrough is the
worst case, being an unconditional microphone-to-speaker loop.  The root
`README.md` has always opened its Run section by telling you to use headphones;
this is the app enforcing what that sentence asks for.

The test is on the CoreAudio **data source**, not the transport type.  A
built-in output device reports `'ispk'` when it is the speakers and `'hdpn'`
when there are headphones in the jack -- same device ID, same `'bltn'`
transport, source changing underneath.  Keying on the transport would refuse to
run with wired headphones, which is the fix rather than another case of the
problem.  So `whistle_audio.c` checks for exactly `'imic'` into `'ispk'`, and
a device that will not answer the question at all is treated as fine: guessing
wrong in the permissive direction costs a howl someone can stop, and guessing
wrong the other way refuses to run on hardware that works.

`whistle_resolve_route` answers this without starting anything, so the window
can say "connect headphones" the whole time it is true.  `whistle_start`
checks again and fails, so there is one place the rule lives whichever way the
call arrives.  The banner sits above the tabs rather than replacing them,
because an interface that is plugged in but not selected is fixed on the Audio
tab, two clicks away.  Plug headphones in and it starts on its own.

That last part needs a poll, which is worth knowing about before someone
deletes it.  A USB interface or a pair of AirPods changes the device list, and
`kAudioHardwarePropertyDevices` fires.  **Headphones in the jack fire
nothing**: the device list is the same, the default output device is the same,
the device ID is the same, and only the data source underneath it changed.
There is no notification to subscribe to.  So while the route is refused --
only then, and only once a second -- `SynthController` re-asks.

## The device is borrowed, not taken

The obvious reading of the CoreAudio API is that the sample rate and the
buffer size are both device-wide, so setting either one sets it for every
other app on that device.  That is half right, and the half that is wrong
matters, so it was measured rather than assumed.

With this code holding the default output device at 64 frames and 44100Hz, a
**separate process** reading the same device's properties sees:

| property | what the other process saw | scope |
|---|---|---|
| `kAudioDevicePropertyBufferFrameSize` | 512, unchanged, throughout | **per client** |
| `kAudioDevicePropertyNominalSampleRate` | 44100 while held, 48000 after | **device-wide** |

The HAL keeps a buffer size per client and adapts between them.  So:

* **The buffer size defaults to 64**, which is the point of the app.  It is
  this app's own; nothing else on the device gets a different block size
  because of it.  The device's hardware I/O cycle does get shorter, which
  costs a little power, but no other app's latency or block size changes.
* **The sample rate defaults to "Device default"** -- 0, don't touch it.  That
  one really is shared: a device runs at one rate for everything using it, and
  changing it interrupts whatever is playing.  Picking a rate is a decision
  about the whole machine, and not one to make for someone who never asked.
* **Whatever is changed is put back.**  `request_sample_rate` and
  `request_buffer_frames` remember what the device was doing the first time
  they change it, and `teardown` restores it once the units are closed.  Only
  the first time, so a restart cannot overwrite the original with our own
  value from the previous run.  For the buffer size this is insurance rather
  than a correction -- the measurement above is one OS version on one device,
  and putting back what you changed costs nothing.

The measurement is worth repeating if this ever seems to matter again: build
`hold.c` and `peek.c` style harnesses against `mac/core/`, run one holding the
device and the other reading it.  The command-line build's comments assert the
device-wide reading for both, and for the buffer size they are wrong.

## Where settings live

`UserDefaults`, which for a sandboxed app means
`~/Library/Containers/com.jefftk.WhistleSynth/Data/Library/Preferences/`.
That is the standard place, it is per-user, the system backs it up with the
app, and nothing needs permission to write it.  The control files the
command-line build reads (`current-voice` and friends) could not have been
reached from inside the sandbox anyway.

Voice, volume, gate, the fifth, the sustain, the two device UIDs, the sample
rate and the buffer size are stored as plain values.  The mute is not; see
above.  Edited voice parameters are stored as JSON under
`voiceOverrides`, keyed by voice name and holding **only the fields that
differ from the built-in preset**.  Two consequences, both wanted:

* Improving a preset in a later version still reaches anyone who never touched
  that field.
* Reordering the `presets` table in `synth.c` does not shuffle someone's edits
  onto the wrong voice.

Devices are remembered by UID rather than by CoreAudio's `AudioDeviceID`,
which is only good for one launch.  A UID that is no longer present falls back
to the system default rather than refusing to start -- unplugging an interface
should not leave the app unable to make a sound.

## Audio

Two shapes of stream, picked automatically:

* **Duplex** -- input and output are the same device, so one AUHAL unit does
  both and the callback already has the input in hand.  This is the fast path.
  Measured on a Scarlett 2i2 at 48kHz with 64-frame buffers: 4.4ms in + 1.9ms
  out.
* **Split** -- different devices, so two clocks that drift, and the input
  reaches the output thread through a lock-free ring buffer.  Built-in
  microphone to built-in speakers measures 5.5ms in (including the ring) +
  3.6ms out.

Split mode is only tolerable because of how the engine is built: the synth
free-runs and never reads the input, so the two sides do not have to stay
sample-aligned.  A resynced sample perturbs the pitch detector for one
analysis window and never reaches the output directly.  The ring is primed
before the output starts, so its startup does not show up as dropouts -- which
matters, because the dropout count is how you decide whether to raise the
buffer size.

### Stereo

The output is real stereo, not a widener. The synth already runs up to three
detuned copies of the oscillator and sums them; the stereo comes from panning
those copies, so the two channels carry genuinely different signals rather
than a delayed or phase-shifted copy of one.

It is carried as a side signal -- left is mid+side, right is mid-side -- which
means **the mono fold-down is exact**. Measured over a real whistling take,
`(L+R)/2` differs from the mono output by 7.5e-09, 141dB below peak, which is
float rounding and nothing else. That matters because summing detuned copies
that have been panned apart is normally how a stereo patch disappears when
someone plays it through one speaker.

A voice running a single oscillator has nothing to spread and stays centred
whatever its width says, which is what `bass` and `subbass` want anyway.

`stereo_width` is on the Voice tab like everything else. Width 1.0 means the
outermost copies are panned hard apart, which is honest but further than it
sounds like it should be: three copies four cents apart really do null in the
middle, so at full spread the difference between the outer two is larger than
their sum and the channels end up anti-correlated. Widths were set by
measuring L/R correlation rather than by ear -- `lead`, at 3 copies and 0.30,
measured +0.68, wide and still solid; `trombone`, at 2 copies and 0.35,
measured +0.85, just off centre.

**No voice in the current table sets a width**, so as it stands every voice
comes out centred and the two channels are identical. That is not the stereo
path having been removed -- it is what a table of ten bass voices wants. Nine
of them run a single oscillator and so have nothing to spread whatever their
width says, and `reese`, the one that runs three, is a bass line: a wandering
low end is the one thing that will not survive being played through a PA, and
its churn is handled by `mono_partials` instead. Give any voice a width and
the machinery below applies to it again unchanged.

A mono output device gets `(L+R)/2`, which by the above is exactly the mono
signal.

**A PA that takes one channel and ignores the other is also fine**, which is a
separate question from fold-down and worth having checked rather than assumed.
That case gets `mid+side`, not the average, so nothing about the exact
fold-down guarantees it.  Measured against the mono output over a real
whistling take, on the voices that had a width at the time, one channel on its
own was:

| voice | level | worst third-octave deviation |
|---|---|---|
| `lead` | +0.8 dB | +0.98 dB |
| `trombone` | +0.4 dB | +1.32 dB |
| centred voices | +0.0 dB | 0.00 dB |

Flat within about a decibel from 40Hz to 10kHz, and the deviation is a level
offset rather than a tilt or a notch.  Every voice in the current table is the
last row exactly, by construction rather than by measurement: with the width
at zero the side signal is multiplied by zero.  Nothing clips that would not have
clipped in mono, at volume 9 or below.

That is not luck, it is the technique: the stereo here is *panning*, which is
amplitude only.  Every channel is some weighted sum of the same oscillators,
so no channel can suffer cancellation.  A Haas delay or an all-pass widener
would measure fine on one channel and comb badly on the sum; this combs on
neither.

So there is no "mono PA" switch, and adding one would be a control that does
nothing.  If a player does want a voice dead centre -- for a mono desk they do
not trust, or for a bass line they want tight -- `stereo_width` already goes
to 0 and reads "centred", which is that switch.

The **Audio** tab reports what the stream actually got rather than what was
asked for, because devices clamp both the sample rate and the buffer size and
say nothing about it.

## The Play tab

The tab the app opens on is the one that has to work while someone is
playing, so it is a page of controls rather than a `Form`: voice, volume,
gate, the octave, the range, the fifth, the sustain and a mute, all of them
above the fold at the window's minimum size, none of them scrolling, and the ones
touched while playing big enough to hit without aiming.  The three switches
share a row and the range is a single line, which is what pays for the grid of
voices and the two rows of ten being as big as they are.  A control you have to go looking for mid-tune
is a control you do not use.

Two things follow from that, and both are the reason the layout is hand-built
rather than a list of rows:

* **Selection is a filled block, not a dot.**  Voices are a grid of tiles and
  the current one is solid accent; the 0-9 knobs are ten tiles you click
  directly and they fill like a meter, so the setting reads from across a room
  without reading a number.  `TileButtonStyle` and `StepTileStyle` are the
  same idea twice.
* **The readouts left.**  The meters, the detected note and the playing level
  are on the **Voice** tab now, beside the full-blow level they exist to set.
  What stays on Play is one line -- input meter and detected note -- which is
  the only readout worth a glance while playing: is it hearing me, and does it
  agree with me about what note that was.

`Passthrough` is beside the heading rather than in the grid.  It is a tool for
setting the input level and the gate, not a voice, and a tile in the grid is
something you land on by accident.

It is also a **round trip**: pressing it again puts back the voice that was
playing, rather than leaving you to pick one.  That is not tidiness.
Passthrough is an unconditional microphone-to-speaker loop, so it is the one
thing in the app that can start howling on a rig where nothing else does --
and the way out of a howl has to be the control that is already under the
pointer, pressed again, not a different control found while the room is
screaming.  `SynthController.comeBackTo` follows every voice change, so it is
whatever was last playing; a launch that comes up in passthrough falls back to
the voice a first run would have started on.  The label stays "Passthrough"
either way, because a label that grew would move the button.  The Voice tab's
"Switch to ..." button, which is the same escape hatch in the other place it
is reachable, makes the same trip.

### Full blow

The third 0-9 bar, under Volume and Gate, and the odd one out: it edits a
field of `SynthParams`.  It is here because it is not a property of a sound.
`level_full` is an input RMS in input units, so what it describes is the
microphone, the preamp and how hard this particular person whistles -- and
the table has been saying so all along, since **every preset in it asks for
the same 0.22**.  Two players swapping rigs want to change one number, not
twelve.

So `synth_set_level_full` overrides it for every voice at once, 0 leaving each
voice on its own -- which is where the command-line build stays, so nothing
about it changes.  `contact_level` follows along, being a fraction of
`level_full` by construction.  Step 5 *is* 0.22, bit for bit: measured, the
same whistle through step 5 and through an untouched engine both come out at
0.1691 RMS.  3dB a step, so the ten of them span 27dB, which is about the
difference between a laptop microphone across the room and a vocal mic at the
lip.

It has therefore left the Voice tab, which is no longer quite exhaustive over
`SynthParams` -- the one field it omits is the one that was never about the
voice.  A slider that silently did nothing because a player control overrode
it would be worse.

**The mark under the bar is the point.**  "Whistle your loudest and set this
to the number you measured" is two numbers and a tab change; the same
instruction with a caret on the bar is "whistle your loudest and click where
the caret is".  It is the playing level -- the same RMS-while-a-note-sounds
that the Voice tab reports -- held for about eight seconds so that it is still
there after you take your mouth off the microphone and reach for the mouse.

It appears only for **30 seconds after the bar is last touched**, and so never
at startup.  A live readout twitching under a bar is exactly the right thing
to look at while calibrating and exactly the wrong thing to have on screen for
the rest of a set, on a page whose whole point is that there is little to
read.  Touching the bar is what asks for it -- clicking the step you are
already on counts, since that is how someone who wants the mark back asks for
it without changing anything -- and it is drawn twice the size it would be if
it lived there permanently, because something that appears for half a minute
and then goes may as well be seen from across the room.

### Octave

Two buttons and a number, moving every voice by whole octaves on top of the
octave its preset already plays at, within `SYNTH_OCTAVE_SHIFT` either way.
"Default" appears when it is not zero.

It is the one transposition that is musically free.  An octave is a power of
two, so the tuning does not move, the sustain's snap lands on the same grid,
and `synth_musical_offset` -- which is what the snap sees through -- is
already `log2f(synth_octave(s))` and needs nothing added to it.  Everything
that turns a played pitch into a frequency goes through `synth_octave`, so the
shift is one multiply in one place.

It lives beside the fifth in the synth rather than in the params, for the same
reason the fifth does: where a line sits is a decision about the arrangement
-- the same bass voice down one to sit under a tune, up two to play it -- and
not a property of the timbre, so it survives a voice change and is stored once
rather than per voice.  The two are kept as separate fields and multiplied
into `octave_mul`, so that switching the fifth cannot undo the octave someone
is playing in, and vice versa.  Verified directly: `octave_mul` is 2^n times
2/3-or-1 across every combination, clamps beyond +/-3, and each control
survives the other being changed and a voice change on top of that.  Measured
at the output, +1, +2 and +3 come out at exactly 1.0000, 2.0000 and 3.0000
octaves.

Whole octaves rather than semitones because that is the control it is: an
instrument that transposes by an interval is a different instrument, and the
fifth is already the one exception to that.

### Range

The lowest and highest notes that will trigger, as two menus of note names on
one line.

The menus offer **F3 to E9** -- the lowest and highest notes anyone has been
recorded whistling -- and start on **C♯5 to G7**, the ordinary whistle range,
which is what the detector has always been pointed at.  Both pairs come from
`whistle_lowest_note` and `whistle_default_low_note` rather than being written
down in Swift, so the menu and the default follow `ENGINE_LOWEST_HZ` and
`ENGINE_MIN_HZ` instead of repeating them.  "Default" appears beside the menus
whenever either end has been moved, and goes back to C♯5-G7.

At the top the range is purely a **veto on the answer**, and that is the whole
of why it works.  Narrowing YIN's search does not stop a whistle above the top
from being heard -- it makes it come out an octave *down*, because with the
true period excluded the first lag that explains the signal is the
subharmonic, and if that lands inside the range it is played as a note nobody
whistled.  So `min_period` always reaches `ENGINE_HIGHEST_HZ`, whatever the
range, and the veto refuses the committed pitch instead: out of range a
whistle is heard clearly, understood correctly, and not played.  It is folded
in with the confidence test, so leaving the range ends a note the way losing
the pitch does, after `OFF_HOPS` rather than on the first analysis window that
says so.  Raising the top therefore costs nothing at all -- shorter lags are
cheaper -- which is why E9 is simply offered.

At the bottom it is **also the search**, because it has to be: a lag can only
be measured against what is left of the window behind it, so finding F3 at
48kHz means comparing lags out to 275 samples inside a window three times
that.  The window is chosen per range (`window_for`, four times the longest
lag) and the detection lag is half of it:

| range | window | detection lag | detector CPU |
|---|---|---|---|
| C♯5-G7, the default | 384 | 4.0 ms | 1.1% of a core |
| F3-E9, the widest | 1152 | 12.0 ms | 8.1% |

Measured, rendering 10s of audio through `engine_process`.  So the player who
wants those notes pays for them and nobody else does -- and the Audio tab's
detection lag is read from the live window rather than from a constant, so it
tells the truth about which case you are in.  The command-line build never
moves the range and is always the first row.

Two things to know before reaching for the bottom of it.  The voices transpose
*down* from what you whistle -- four octaves for `bass` -- so a whistled F3
comes out near 11Hz, which is cone excursion rather than a note.  And at
88.2/96kHz the window is in samples, so it doubles to hold the same amount of
signal; that is a fix rather than a cost, since before this the detector
silently could not hear below 1kHz at 96kHz at all (`PITCH_MAX_PERIOD` clamped
it), and now it can.

A refused note does **not** raise the noise floor, which would otherwise be
the cruel failure: the floor is tracked on periodicity rather than on level,
and a refused whistle is still plainly periodic, so it cannot pull the gate up
behind it and take the in-range notes with it.

The ends are inclusive by half a semitone each, applied in `whistle.c` where
the note numbers become Hz.  The choice is a note, so the note named has to
trigger whether the player lands 40 cents under it or over it -- and a range
of one note is a legitimate thing to ask for, which it would not be if the
ends met exactly.

Measured against tones: with the range set to C5-C6, 700Hz plays and 1400Hz
and 2500Hz are silent; with it set to C6 alone, C6 and C6 ±40 cents play and a
semitone above does not; with only the bottom set, below it is silent and
everything above plays.  At the extremes, F3 is tracked at 174.6Hz and E9 at
5305Hz -- ten cents sharp, which is one sample of lag quantisation at a period
of nine, and not a note anyone is checking the intonation of.

When something is being refused the Play tab's bottom line says which note it
was.  "Nothing is happening" is the hardest thing to work out while playing,
and a range set and forgotten is one of the ways to arrive at it.

### Mute

Not a volume step and not a stop.  `whistle_set_mute` is a gain on the very
last thing before the buffer, so the engine keeps running behind it: the
detector still tracks, a held note still sustains, the meters still read, and
unmuting lands in the middle of whatever was already sounding.  It ramps over
8ms, because a gain that jumps to zero mid-waveform is a click.

It is the one control with a keyboard shortcut -- a bare `m`, no modifier,
because a modifier is two hands and nothing in this app takes typed text.  It
is also the one player control that is **not** stored: everything else here
should come back the way it was left, but an app that starts silent because of
something someone did last week is an app that appears broken.  Muted shows in
the status bar with its own unmute button, since the mute can be on while
another tab is in front and "no sound" is otherwise a hardware question.

### The player's controls, and the patch's

The two switches are deliberately *not* on the Voice tab and are not stored
per voice:

* **Down a fifth** plays every voice a just fifth below what it otherwise
  would, so what you whistle is the fifth of what you hear rather than the
  root: a tune whistled in D comes out in G.
* **Sustain** makes a note you *hold* outlive the breath that made it -- it
  slides onto the nearest real note, settles under itself, and stays there.
  Indefinitely: the only things that end a tail are the next note and the
  switch, so a drone left alone is still going a minute later.  Notes too
  short to have been meant that way are untouched, so a fast phrase sounds the
  same either way, and switching the sustain off under a sounding tail fades
  it over 0.6s rather than cutting it.

Both correspond to `current-fifth` and `current-sustain` in the command-line
build.  They belong to the player rather than to the patch: which voice you
are playing, what interval you are playing it at, and whether the line
breathes with you are three separate decisions, so these survive a voice
change and apply to all of them.

A voice may opt out of the sustain (`no_sustain`, which `pluck` sets), and the
Play tab says so under the switch when the voice you are on is one of them --
a switch that silently does nothing is how a switch gets a reputation for
being broken.

### While playing

The Voice tab's Listening section reports **While playing**: the loudest the
*detector* heard while a note was actually sounding.  That is not the input
meter above it.  The input meter is a sample peak over everything, including
the room between notes; this is an RMS over the analysis window, taken only
while a note was sounding, and it is the number `level_full` is actually
compared against.  So it is the one to read when setting `level_full` -- which
is why it sits on the same page as that slider -- and it is the same number
the command-line build prints every four seconds.

## Editing voices

Everything in `SynthParams` is on the **Voice** tab, generated from the table
in `VoiceParameters.swift` rather than laid out by hand -- adding a field to
the struct means adding one entry there, not writing another slider.  There
are fifty of them; the grouping is the only thing keeping that readable, so a
new field wants a group as much as it wants a range.

A `ParamSpec`'s range is a taste range rather than the engine's -- the engine
clamps far wider, in `synth_sanitize_params`.  But it has to *contain* every
preset's value for that field, or the slider parks at its own floor and
reports a number the voice does not have.  Several fields read zero as "unset"
and let the engine fill in a default (`resonance_width`, `octave_stack_width`,
`octave_stack_ref_hz`); those ranges start at zero and their formatters say
`auto`, rather than starting at a usable value and lying about the ones that
are off.

A `ParamSpec`'s `id` is its persistence key, so renaming one silently discards
that edit for everyone who made it.  Rename the `label` instead.

`level_full` is the one field of `SynthParams` that is *not* on the tab: it is
an input level rather than a description of a sound, so it is a 0-9 bar on the
Play tab instead.  See **Full blow** above.  Everything else is here.

`out_gain` is on the tab because the tab is otherwise exhaustive, but it is not a taste
control: each preset's value is set so that every voice measures the same
loudness (ITU-R BS.1770), and changing one means re-running the loudness match
rather than just that voice.  The slider says so.  Three voices sit
deliberately under the match -- `reese` by one volume step, the two drawbars
by two -- which is the one place taste overrides the measurement; the reasons
are in the `out_gain` comment at the top of `synth.c`.

## Do not use `LabeledContent`

`LabeledContent` inside a grouped `Form` inside a `TabView` sends SwiftUI's
attribute graph into a cycle on macOS 26, as soon as a tab is switched away
from and back.  The app beachballs, and what actually freezes it is the
*reporting*: AttributeGraph writes several million `=== AttributeGraph: cycle
detected through attribute N ===` lines a second to stderr, and that write is
synchronous on the main thread.  A `sample` of the hung process is almost
entirely `AG::Graph::print_cycle` → `fprintf` → `write`.

Bisected to this one view: with the Play and Audio tabs stubbed out the cycle
count is zero, with either one restored it is millions, and the Voice tab --
the only one that never used `LabeledContent` -- never triggered it.  Swapping
in the plain `HStack` of `StatRow` (in `PlayView.swift`) took it back to zero
with no other change.

So use `StatRow`.  If you reach for `LabeledContent` again, check with a tab
switch before believing it.

## How the UI reaches the audio thread

Nothing locks and nothing allocates on the audio thread.

Starting and stopping is a third thread, not the main one.  `whistle_start` is
synchronous CoreAudio work -- opening units, negotiating formats, and in split
mode a wait of up to 200ms while the input ring primes -- and `restart()`
re-enters all of it on every device change and every settings change.  On the
main thread that meant unplugging an interface mid-tune stalled the window for
as long as the hardware took to answer.  `AudioLifecycle` (Swift) owns a
serial queue that every `whistle_start` and `whistle_stop` goes through, which
also gives the C side the single-lifecycle-thread guarantee its comments
assume.  Termination is the one synchronous call: the device has to be handed
back, with the sample rate and buffer size restored, before the process goes
away.

Results come back on the main queue, and `running` is read from the C side
rather than from which callback arrived, so a start that was overtaken by a
newer one cannot leave a stale answer on screen.

Volume, gate, the fifth and the sustain are single atomic ints, picked up once
per block.  They are separate from the program below precisely because they
are not part of it: a voice change must not disturb them, and they must not
wait on one.

The voice and its parameters travel **together**, through a four-slot ring
with a generation counter: applying them separately would sound one block of
the old voice's parameters, or of the new voice's defaults, every time you
switched.  The audio thread copies the published slot into storage only it
owns, then re-checks the counter, so it can tell whether the writer lapped it
mid-copy and retry.  `synth_sanitize_params` runs on the writing side, so
numbers that would divide by zero or run off the end of a fixed array cannot
reach the audio thread no matter where they came from.

Meters go the other way: peaks are accumulated per block into atomics and the
UI takes and resets them at 24Hz.  The playing level is one of them even
though the engine already accumulates it, because `engine_take_peak_level`
reads and clears a plain float that the audio thread writes every sample --
calling it from the UI thread, as the command-line build does from its own
main loop, would be a race.  It is taken on the audio thread instead and
folded into an atomic like the rest.
