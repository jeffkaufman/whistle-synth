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

**Subtitle** — `Control a synth by whistling`  [28/30]

**Primary category** — Music.  Matches `LSApplicationCategoryType =
public.app-category.music` in `Info.plist`; they should not disagree.

**Secondary category** — leave empty.  The obvious second is Entertainment and
it is not what this is.

**Bundle ID** — `com.jefftk.WhistleSynth`.  Register it under Identifiers on
the developer portal *before* opening this form; the field is a picker over
already-registered App IDs, and Distribute registering it for you happens a
step too late to fill that picker.  See "Before any of this: the build" #3.

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

### Promotional text  [148/170]

Editable later without a review, which makes it the place for anything you
might want to change on a Tuesday.

```
Control a synth by whistling. Twelve voices, continuous pitch, and low enough latency to play like an instrument. Headphones or separate mic needed.
```

### Description  [2895/4000]

```
Whistle Synth lets you control a synthesizer by whistling.

It listens to your mic, quickly works out what note you're on, and plays that back on a synth voice. It follows pitch continuously, so you can slide and bend, and it tracks how loud your whistle is, which is what most of the voices use for their dynamics. It is monophonic: one note at a time, the one you are whistling.

TWELVE VOICES

Bass, Sub Bass, Octaveless, Reese, 808, Pluck, FM, FM Sub, Grind, Square, Drawbar and Drawbar Hi, plus a Passthrough for setting your input level and your gate.

PLAYING IT

Plug in an external mic or headphones, so you don't get feedback. Whistle into the mic, keeping it as close to your mouth as possible. Listen to the synth.

Down a fifth plays every voice a fifth below what you whistle: a tune whistled in D comes out in G.

Sustain lets you play a note and have it continue after you stop, which is good for accompanying yourself on another instrument. Unlike the rest of Whistle Synth, this snaps to the nearest concert pitch note. It's triggered by playing a note over half a second long, and stopped by playing a short note.

An octave shift and a settable range mean a whistle can drive a bass line four octaves down, which is the biggest reason to use the app.

Mute is on the M key, with no modifier, to make it as fast as possible to mute if it's not doing what you want.

THE VOICE EDITOR

Every parameter of the current voice is on its own tab, each with a sentence saying what it actually does: pulse width, spectral tilt, how many partials to synthesize, how bright the voice is when you back off against when you lean in, the rolloff slope, resonance, and the rest. You can learn a fair amount of synthesis by turning the knobs and reading.

THE AUDIO TAB

Choose input and output device, sample rate and buffer size, and then see what you actually got: input latency, output latency, round trip, detection lag, and a dropout count. On an audio interface at 64 frames this runs at a few milliseconds round trip, which is the difference between an instrument and a delay pedal.

The audio device is borrowed, not taken. Whistle Synth starts on your device's default sample rate, and puts back anything it changed when it stops.

YOU NEED HEADPHONES OR A SEPARATE MICROPHONE

On a typical Mac the built-in microphone and the built-in speakers are a few inches apart, so the synth hears its own output and howls. Whistle Synth will not run that route at all. Connect headphones (wired for low latency) or an external microphone (as close as possible to your mouth). A good microphone (SM58 or better) into an audio interface gives best performance.

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

### Notes  [1702/4000]

This field is the whole reason `mac/app-store-review.md` #15 exists.  The app
refuses the built-in-mic-into-built-in-speakers route, so a reviewer opening
it on a bare MacBook sees "Headphones needed" and cannot make it produce a
sound.  An app that cannot be made to do anything is a 2.1 rejection whatever
the reason, and the banner is not a defence if nobody said so in advance.

```
Whistle Synth turns whistling into a synth line: it hears the note you whistle and plays that note back on a synth voice.

PLEASE CONNECT HEADPHONES BEFORE TESTING. Use wired headphones for good latency.

The app deliberately refuses to run the built-in microphone into the built-in speakers. On a MacBook they are a few inches apart, so the synth hears its own output through the mic and howls immediately. Rather than ship an app whose first experience is feedback, it declines that one route and shows a "Headphones needed" banner instead. The banner clears by itself within about a second of headphones being connected, and no other setup is needed.

To test:

1. Connect headphones.
2. Launch Whistle Synth and grant the microphone permission when asked. The app does nothing until it is granted: that is the only thing it uses the microphone for, and there is no other input.
3. Whistle a steady note into the mic. The bar at the bottom of the Play tab moves and the note name beside it shows what it heard; the synth voice follows the same pitch through the headphones.
4. Try the voice tiles at the top of the Play tab to hear different sounds, or Passthrough to hear your microphone unprocessed if you want to confirm the input is working.

A demo video is attached showing all of the above, since the working state depends on hardware that may not be in front of you.

Two other things you may notice:

- The app changes the output device's sample rate only if you pick one on the Audio tab. It starts on "Device default" and puts back whatever it changed when it stops.
- There is no account, no network access, and no data collection. Audio is processed in memory and discarded.

Thank you.
```

### Attachment: the demo video — done

`mac/store/whistle-synth-demo.mp4`.  13.2 MB, 1:08, 2472x1512, H.264 video and
AAC audio.  Attach it in App Review Information, below the Notes.

Not optional in practice.  It is the ordinary remedy for "the reviewer could
not reproduce the working state", and this app's working state depends on
hardware that is not on the reviewer's desk.

**It is an `.mp4` on purpose.**  App Store Connect's accepted attachment types
are `.pdf .doc .docx .rtf .pages .xls .xlsx .numbers .zip .rar .plist .crash
.jpg .png .mp4 .avi` — and `.mov` is not among them, which is what ⌘⇧5 writes.
The capture was already H.264 and AAC, so the conversion is a rewrap, not a
re-encode: the pixels are bit-identical to what came off the screen.

**The audio was quiet and has been raised.**  As recorded it peaked at −9.4 dB
and averaged −32 dB, which a reviewer on laptop speakers in an office would
have struggled with.  It is now +6.4 dB louder, peaking at −3.0 dB, re-encoded
at 192k so the boost does not amplify the artifacts of the original 106k.
Nothing was near clipping, so no limiting was needed.

The original `~/Desktop/whistle-synth-demo-video.mov` is untouched.  Rebuild
from that, not from the `.mp4`, if you change anything — it keeps the audio one
generation of encoding deep instead of two:

```sh
ffmpeg -i ~/Desktop/whistle-synth-demo-video.mov \
  -c:v copy -af "volume=6.4dB" -c:a aac -b:a 192k \
  -movflags +faststart -map_metadata -1 \
  mac/store/whistle-synth-demo.mp4
```

Recheck the level with `ffmpeg -i out.mp4 -af volumedetect -f null -`; the
`volume=` figure is whatever gets `max_volume` to −3 dB, so it changes with
every retake.

**How it was captured**, if it needs doing again.  The awkward part is getting
the synth output and the whistle into one file:

1. **Audio tab:** Input = a real microphone (the Scarlett), Output = MacBook
   Pro Speakers.  That combination is allowed — the refusal is only when
   *both* ends are built-in — and it means the synth is coming out of the
   machine you are recording.
2. **⌘⇧5 ▸ Options:** microphone on (the built-in one, to catch the room) *and*
   system audio.  Both land in one stereo track.
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

**The file is not tracked by git.**  13 MB against a repository whose whole
history packs to 777 KiB; the screenshots are small enough to track and this is
not.  It lives in `mac/store/` beside them anyway, because that is where you
will look for it.

---

## Before any of this: the build — archived and checked

**The five `project.pbxproj` lines are committed**, in `c71b837` ("mac: the
App Store prep that was never committed").  `DEVELOPMENT_TEAM`, two
`ENABLE_APP_SANDBOX` and two `GCC_OPTIMIZATION_LEVEL` are all in `HEAD` with no
working-tree diff, so the thing you upload is a thing the repository can
rebuild.  This is the state that was made and lost twice before — see the
status blocks at the top of `mac/app-store-review.md` — so if you come back to
this cold, confirm it again with `git diff HEAD --
mac/WhistleSynth.xcodeproj/project.pbxproj` before archiving.

**The archive exists, 2026-08-31:**
`~/Library/Developer/Xcode/Archives/2026-08-31/WhistleSynth-2026-08-31.xcarchive`,
built from the tree at `9b8c4ab` with `xcode-select` already pointing at Xcode.
What was checked in it: universal (`x86_64 arm64`), `TeamIdentifier=C58DMTY76R`,
a signature chaining to Apple Root CA with no `adhoc` flag, hardened runtime on,
exactly the two entitlements (`app-sandbox`, `device.audio-input`), version
`1.0` build `1`, and the **new whistle icon** — `Assets.car` carries the whole
ramp from 16 up to a true 1024, which is the copy the App Store reads.

Two things about that archive that look wrong and are not.  It is signed
`Apple Development`, not `Apple Distribution`: Distribute re-signs on export,
so this is what an unexported archive is supposed to look like, and it stays
true after the old certificate is revoked.  And the `AppIcon.icns` beside
`Assets.car` holds only the small sizes — that is actool's legacy fallback
file, not the icon anyone reads.

Then, in order:

1. `xcode-select` must point at Xcode, not the Command Line Tools:
   `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
2. Archive.  Product ▸ Archive in Xcode, or
   `xcodebuild -project mac/WhistleSynth.xcodeproj -scheme WhistleSynth
   -configuration Release -destination 'generic/platform=macOS' archive`.
   A plain `build` is **not** a rehearsal — with the default destination it
   is arm64 only.
3. **The App Store Connect record has to exist before the upload, not after.**
   This is the one ordering trap in the whole sequence, and it is two deep:
   ASC's New App form offers a Bundle ID *picker*, which lists only App IDs
   already registered on the developer portal, and the upload in step 4 fails
   with "No suitable application records were found" if no record is waiting
   for the build.  So: register `com.jefftk.WhistleSynth` under Identifiers on
   the portal, then create the app record in ASC, then upload.  Xcode's
   automatic signing would register the App ID for you during Distribute, but
   that is one step too late to fill in the picker.

   The record only has to *exist* before the upload, not be finished.  New App
   is a six-field form — macOS, `Whistle Synth`, English (U.S.), the bundle ID,
   SKU `whistle-synth-1`, Full Access — so create that, start the upload in
   step 4 immediately, and paste the rest of this document into the browser
   while the build processes.  Processing is minutes to an hour and there is no
   reason to spend it idle.
4. **Distribute App ▸ App Store Connect ▸ Upload.**  This is the step that has
   never been run.  It creates, on its own, the rest of what does not exist
   yet: the Mac App Store provisioning profile, the `Apple Distribution`
   certificate that signs the `.app`, and the **Mac Installer Distribution**
   certificate that signs the `.pkg` that is actually uploaded.  The second
   certificate is the one people forget, and the error when it is missing
   names the `.pkg`, not the certificate.
5. Wait for the build to finish processing in App Store Connect — usually
   minutes, occasionally an hour — then attach it to version 1.0.
6. Submit.

Nothing here bumps a version number, because nothing has been accepted yet.
The moment a build is uploaded, `CFBundleVersion` (`1`) is spent: any later
upload for 1.0 needs it raised, even if the code is identical.

---

## Leftovers, none of them blocking

* ~~**Revoke the old development certificate.**~~  Done 2026-08-31, before the
  Distribute run, which is the point: it read "JEFFREY THOMFORDE KAUFHAN", and
  the distribution certificates Distribute creates are now issued under the
  corrected name from the start.
* **Agreements, Tax, and Banking** — confirm the legal name there reads
  Kaufman too.  #14 corrected the membership record; this section is a
  separate copy of the same name, and a mismatch holds up payouts silently
  rather than failing loudly.  It has to be accepted before a free app can be
  distributed at all, so this is worth checking early even though it is
  filed under "leftovers".  *(2026-08-31: for a **free** app there is nothing
  to do here — no Paid Apps agreement, no banking, no tax forms.  The one
  failure mode a free app still has is an un-accepted **update** to the
  Developer Program License Agreement, which shows as a yellow banner on ASC ▸
  Business and does block submission.  Ten seconds to rule out.)*
* **The icon was redrawn on 2026-08-30** (`9b8c4ab`) and is no longer the
  signal-chain diagram this list used to apologise for.  It is a whistle that
  is also a synth, still `mac/tools/make-icon.swift`'s output, still legible at
  16px.  The screenshots predate it and do not need retaking: no tab in the app
  draws its own icon, so none of the four contains the old one.
* **#16**, the feedback refusal only catching the built-in transport type, is
  a 1.0.1 item.  It wants a machine with a Studio Display to test on.
