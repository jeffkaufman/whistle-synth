# App Store Connect: everything to paste

Written 2026-08-30, picking up from `mac/app-store-review.md` #12 and #15 with
the screenshots taken.

Fields are in the order App Store Connect asks for them.  Character counts
against Apple's limits are in brackets; they were counted, not estimated.

---

## Screenshots — done

`mac/store/screenshots/*.png`, four of them, 2560x1600.

They were taken with ⌘⇧4-Space, which is the right way to get a window with
its shadow and **the wrong way to get an App Store asset**: that capture
writes the shadow into an alpha channel, and Apple's screenshot spec wants a
flattened image with no transparency.  All four had `hasAlpha: yes`.  They
have been flattened onto white (the shadow survives as a real grey, which is
the look the alpha was for), converted from this machine's display profile
to sRGB, set to 72 dpi, and stripped of EXIF and XMP.

The originals on the Desktop are untouched.  If you retake any, run the same
pipeline before uploading:

```sh
magick in.png \
  -profile "/System/Library/ColorSync/Profiles/sRGB Profile.icc" \
  -background white -alpha remove -alpha off \
  -density 72 -units PixelsPerInch \
  -define png:exclude-chunk=eXIf,tEXt,iTXt,zTXt,tIME \
  out.png
```

Then check it with `sips -g hasAlpha -g profile -g pixelWidth out.png`.  The
answers you want are `no`, `sRGB IEC61966-2.1`, and one of 1280, 1440, 2560,
2880.

**Upload order.**  Play first — it is the app.  Then Voice, then Audio, then
Help.  The first screenshot is the one that shows in search results, and Play
is the only one of the four that looks like an instrument.

**Whether to ship the Help shot at all** is a judgement call.  It is a wall of
prose and it will be the worst-looking tile on the page.  It is also the one
that says "headphones needed" in the app's own voice, which is the single
thing a browser most needs to know before buying.  Ship it last, or drop it
and put that sentence in the description instead — it is in the description
below either way.

---

## App Information (does not change per version)

**Name** — `Whistle Synth`  [13/30]

**Subtitle** — `Whistle it, hear a synth`  [24/30]

**Primary category** — Music.  Matches `LSApplicationCategoryType =
public.app-category.music` in `Info.plist`; they should not disagree.

**Secondary category** — leave empty.  The obvious second is Entertainment and
it is not what this is.

**Bundle ID** — `com.jefftk.WhistleSynth`, which Distribute registers for you.

**SKU** — `whistle-synth-1`.  Never shown to anyone, never reusable, so it
only has to be a string you will not want back.

**Content Rights** — No, it does not contain, show, or access third-party
content.

**Age Rating** — 4+.  Every question in the questionnaire is No.  There is no
user content, no web view, no ads, no location, no links out.

---

## Pricing and Availability

**Free**, all countries and regions.  (Stated rather than assumed — if you
mean to charge, this is the one line here that needs changing, and it changes
nothing else in this document.)

---

## Version 1.0

### Promotional text  [168/170]

Editable later without a review, which makes it the place for anything you
might want to change on a Tuesday.

```
Whistle a line and hear a synth play it — twelve voices, continuous pitch, and low enough latency that it plays like an instrument. Headphones or a separate mic needed.
```

### Description  [2755/4000]

```
Whistle Synth turns whistling into a synthesizer.

It listens to your microphone, works out what note you are on, and plays that note back on a synth voice. It follows the pitch continuously, so a slide is a slide and a bend is a bend rather than a jump between keys, and it tracks how hard you are blowing, which is what most of the voices use for their dynamics. It is monophonic: one note at a time, the one you are whistling.

TWELVE VOICES

Bass, Sub Bass, Octaveless, Reese, 808, Pluck, FM, FM Sub, Grind, Square, Drawbar and Drawbar Hi, plus a Passthrough for setting your input level and your gate.

PLAYING IT

Down a fifth plays every voice a fifth below what you whistle: a tune whistled in D comes out in G.

Sustain lets a note you hold outlive the breath that made it — it slides onto the nearest real note, settles under itself, and stays there until the next note. A drone left alone is still going a minute later.

An octave shift and a settable range mean a whistle can drive a bass line four octaves down, which is most of the point.

Mute is on the M key, with no modifier, because a modifier is two hands. It ramps rather than cuts, and the engine keeps running behind it, so unmuting lands in the middle of whatever was already sounding.

THE VOICE EDITOR

Every parameter of the current voice is on its own tab, each with a sentence saying what it actually does: pulse width, spectral tilt, how many partials to synthesize, how bright the voice is when you back off against when you lean in, the rolloff slope, resonance, and the rest. You can learn a fair amount of synthesis by turning the knobs and reading.

THE AUDIO TAB

Choose input and output device, sample rate and buffer size, and then see what you actually got: input latency, output latency, round trip, detection lag, and a dropout count. On an audio interface at 64 frames this runs at a few milliseconds round trip, which is the difference between an instrument and a delay pedal.

The audio device is borrowed, not taken. Whistle Synth starts on your device's default sample rate, and puts back anything it changed when it stops.

YOU NEED HEADPHONES OR A SEPARATE MICROPHONE

On a typical Mac the built-in microphone and the built-in speakers are a few inches apart, so the synth hears its own output and howls. Whistle Synth will not run that route at all. Connect headphones — wired or Bluetooth — or use a microphone that sits up by your mouth, and it starts. A dynamic microphone into an audio interface is what it is happiest with.

PRIVACY

Audio is processed on your Mac. Nothing is recorded, nothing is saved, nothing is sent anywhere. No account, no analytics, no network access.

Whistle Synth is open source: https://github.com/jeffkaufman/whistle-synth
```

### Keywords  [87/100]

Comma-separated, **no spaces after the commas** — a space costs a character
and buys nothing.  "Whistle" and "synth" are deliberately absent: the name
field is indexed on its own, and repeating it here spends characters twice.

```
synthesizer,pitch,bass,monophonic,microphone,808,FM,drone,lead,sustain,portamento,music
```

### Support URL

```
https://github.com/jeffkaufman/whistle-synth
```

Checked reachable, 2026-08-30.  Apple does fetch it, and a 404 here is a
rejection.

### Marketing URL

Leave empty.  Optional, and there is nowhere else to point.

### Copyright

```
2026 Jeff Kaufman
```

No `©` — App Store Connect adds it.

### Version release

**Manually release this version.**  Approval and publication should be two
decisions rather than one on a first submission; it costs nothing and it means
nothing goes live at 3am while you are asleep.

---

## App Privacy

**Data Not Collected**, and answer nothing else.  That is what
`PrivacyInfo.xcprivacy` says, what `NSMicrophoneUsageDescription` says, and
what the app does — the audio never leaves the process.

The one trap in this questionnaire: it asks about data *collected*, and
microphone audio that is processed and discarded is not collected.  Do not
tick "Audio Data" out of caution.  Ticking it asserts the app transmits or
retains audio, which would then be a false statement in the other direction
and would raise questions nobody wants to answer.

---

## App Review Information

**Sign-in required:** No.

**Contact:** your name, phone, and email.  Use an address you actually read;
this is where a rejection arrives.

### Notes  [1695/4000]

This field is the whole reason `mac/app-store-review.md` #15 exists.  The app
refuses the built-in-mic-into-built-in-speakers route, so a reviewer opening
it on a bare MacBook sees "Headphones needed" and cannot make it produce a
sound.  An app that cannot be made to do anything is a 2.1 rejection whatever
the reason, and the banner is not a defence if nobody said so in advance.

```
Whistle Synth turns whistling into a synth line: it hears the note you whistle and plays that note back on a synth voice.

PLEASE CONNECT HEADPHONES BEFORE TESTING. Any headphones will do, wired or Bluetooth.

The app deliberately refuses to run the built-in microphone into the built-in speakers. On a MacBook they are a few inches apart, so the synth hears its own output through the mic and howls immediately. Rather than ship an app whose first experience is feedback, it declines that one route and shows a "Headphones needed" banner instead. The banner clears by itself within about a second of headphones being connected, and no other setup is needed.

To test:

1. Connect headphones.
2. Launch Whistle Synth and grant the microphone permission when asked. The app does nothing until it is granted — that is the only thing it uses the microphone for, and there is no other input.
3. Whistle a steady note. The bar at the bottom of the Play tab moves and the note name beside it shows what it heard; the synth voice follows the same pitch through the headphones.
4. Try the voice tiles at the top of the Play tab to hear different sounds, or Passthrough to hear your microphone unprocessed if you want to confirm the input is working.

A demo video is attached showing all of the above, since the working state depends on hardware that may not be in front of you.

Two other things you may notice:

- The app changes the output device's sample rate only if you pick one on the Audio tab. It starts on "Device default" and puts back whatever it changed when it stops.
- There is no account, no network access, and no data collection. Audio is processed in memory and discarded.

Thank you.
```

### Attachment: the demo video

Not optional in practice.  It is the ordinary remedy for "the reviewer could
not reproduce the working state", and this app's working state depends on
hardware that is not on the reviewer's desk.

The awkward part is capturing the synth output and the whistle in one file.
The route that needs no extra software:

1. **Audio tab:** Input = a real microphone (the Scarlett), Output = MacBook
   Pro Speakers.  That combination is allowed — the refusal is only when
   *both* ends are built-in — and it means the synth is coming out of the
   machine you are recording.
2. **⌘⇧5 ▸ Options:** turn on the microphone (the built-in one, to catch the
   room) *and* system audio, if your macOS offers it.  Recent macOS added
   system-audio capture to the built-in recorder; if this one has no such
   item, see the fallback below.
3. Record 30–60 seconds: the Play tab, a steady whistle, the note readout
   moving with it, a voice change or two, and the range or the fifth if there
   is time.  Do not narrate over it — the point is that the sound follows the
   whistle, and a voice-over sits in the same octave and muddies exactly that.
4. Keep it short and keep it boring.  A reviewer wants to see that it works,
   not that it is good.

**Fallback if ⌘⇧5 will not capture system audio:** point a phone at the screen
and record the room.  The video quality does not matter at all here; the
audible relationship between whistle and synth is the entire content.  A
phone recording of a laptop is a completely normal App Review attachment.

---

## Before any of this: the build

**Five lines in `project.pbxproj` are uncommitted as of 2026-08-30.**
`DEVELOPMENT_TEAM`, two `ENABLE_APP_SANDBOX`, and two
`GCC_OPTIMIZATION_LEVEL` are in the working tree and not in `HEAD` — which is
exactly the state that lost them once already, on 2026-08-23.  See the status
block at the top of `mac/app-store-review.md`.  **Commit them before
archiving**, so that the thing you upload is a thing the repository can
rebuild.

Then, in order:

1. `xcode-select` must point at Xcode, not the Command Line Tools:
   `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
2. Archive.  Product ▸ Archive in Xcode, or
   `xcodebuild -project mac/WhistleSynth.xcodeproj -scheme WhistleSynth
   -configuration Release -destination 'generic/platform=macOS' archive`.
   A plain `build` is **not** a rehearsal — with the default destination it
   is arm64 only.
3. **Distribute App ▸ App Store Connect ▸ Upload.**  This is the step that has
   never been run.  It creates, on its own, the things that do not exist yet:
   the App ID registration, the Mac App Store provisioning profile, the
   `Apple Distribution` certificate that signs the `.app`, and the **Mac
   Installer Distribution** certificate that signs the `.pkg` that is actually
   uploaded.  The second certificate is the one people forget, and the error
   when it is missing names the `.pkg`, not the certificate.
4. Wait for the build to finish processing in App Store Connect — usually
   minutes, occasionally an hour — then attach it to version 1.0.
5. Submit.

---

## Leftovers, none of them blocking

* **Revoke the old development certificate.**  It still reads "JEFFREY
  THOMFORDE KAUFHAN"; revoking makes automatic signing reissue it under the
  corrected name.  Do this *before* the Distribute run so the distribution
  certificates are created under the right name from the start.
* **Agreements, Tax, and Banking** — confirm the legal name there reads
  Kaufman too.  #14 corrected the membership record; this section is a
  separate copy of the same name, and a mismatch holds up payouts silently
  rather than failing loudly.  It has to be accepted before a free app can be
  distributed at all, so this is worth checking early even though it is
  filed under "leftovers".
* **The icon** is `mac/tools/make-icon.swift`'s output — honest and legible at
  16px, but generated rather than designed.  It is good enough to ship and it
  is the most visible thing on the product page.
* **#16**, the feedback refusal only catching the built-in transport type, is
  a 1.0.1 item.  It wants a machine with a Studio Display to test on.
