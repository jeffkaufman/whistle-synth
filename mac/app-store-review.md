# Whistle Synth: pre-submission review

Written 2026-08-23, against the tree at commit `2384172` ("mac: catch the app
up with the run2-mac CLI").

> **Status, 2026-08-23 (later the same day):** #3, #4, #5, #6, #7, #8, #9, #10
> and #11 are done, and the half of #2 that could be done without Xcode (the
> Frameworks phase now links AudioToolbox and CoreAudio explicitly instead of
> relying on module autolinking).  #1, the rest of #2, and #12 need an Apple
> Developer account and an Xcode install, and are untouched.  Notes on each are
> inline below.  The fixes are described in `mac/README.md`; what was verified
> and how is at the bottom of this file.

> **Status, 2026-08-23 (Xcode session, later again):** Xcode 26.6 is installed
> and the Apple Developer account exists, so the two things that were blocked
> on having neither are now done.  **#2 is closed** -- the `.xcodeproj` has been
> archived, and it is universal, correctly signed, and correctly entitled.
> **#1 is mostly closed** -- `DEVELOPMENT_TEAM` is set and a signing certificate
> now exists; what is left of it happens inside Xcode's Distribute flow rather
> than in the repository.  **#12 is still entirely open.**  Two new things came
> out of the session that were not in the original review: a build-settings
> drift between `build.sh` and the project (#13), and a typo in the Apple
> Developer account's legal name (#14).  Both are below.
>
> **If you are picking this up cold, read #14 first** -- it is the only item
> with an external clock on it.  *(2026-08-29: it has since been corrected;
> start at #15 instead.)*

> **Status, 2026-08-29:** The Xcode session's project-file edits above **were
> never in the repository.**  `DEVELOPMENT_TEAM`, `ENABLE_APP_SANDBOX` and
> `GCC_OPTIMIZATION_LEVEL` appear nowhere in `project.pbxproj` -- nor anywhere
> in its history -- and an archive of the tree at `98ae0ef` came out
> `Signature=adhoc`, `TeamIdentifier=not set`, with the C compiled at `-Os`.
> Whatever that session did was made and lost rather than committed, and
> `mac/README.md` ("Signing is set to Automatic with no team", "the
> `.xcodeproj` has never been archived") was the accurate document while this
> one was not.
>
> All five lines have been re-applied, and the archive re-run and checked:
> `Team = C58DMTY76R`, a signature chaining to Apple Root CA, no `adhoc` flag,
> exactly the two entitlements, universal, `-O2` in the shared C args, zero
> warnings from either build.  **#1, #2 and #13 are now true as they are
> written below** -- and this time, before believing that, run `git log -p --
> mac/WhistleSynth.xcodeproj/project.pbxproj` and see the lines there.  That
> check is the whole lesson of this pass.  (Also fixed in the same pass, and
> unrelated to the store: a stray `whistle_set_level_full` left behind in
> `octaveShift`'s `didSet`.)
>
> Two new items come out of this pass, both from the Play tab rewrite in
> `98ae0ef` and neither in the original review: **#15**, the App Review notes
> that the refusal in #3 makes mandatory, and **#16**, the shape of hardware
> that refusal does not catch.  **#12 is still entirely open**; **#14 has
> been corrected with Apple** and is down to two follow-ups.

A read of the whole `mac/` target from the
perspective of an App Store reviewer: what would come back as a rejection, and
what to do about it.

## What was actually checked

* Built with `mac/build.sh` -- clean, universal (arm64 + x86_64), ad-hoc
  signed.
* `otool -L` on both slices: only public frameworks, `/usr/lib/swift`, and
  `libSystem`.  No private framework link, nothing to embed.
* `codesign -d --entitlements -` on the built bundle: the two entitlements are
  what the source says they are.
* Icon set: all ten sizes present, correct pixel dimensions, 512x512@2x is a
  true 1024.
* Read `Info.plist`, the entitlements, `project.pbxproj`, all eight Swift
  files, and `mac/core/*.c`.

**Not** checked, because there is no Xcode on this machine (Command Line Tools
only): the `.xcodeproj` has never been archived, and the app was not run.  Two
items below (#5, #8) are marked verify rather than confirmed for that reason.

*Superseded 2026-08-23:* Xcode 26.6 is installed and both of those have now
been done -- see "The Xcode session" at the bottom.

## What is already right

The sandbox is on with a minimal entitlement pair.
`NSMicrophoneUsageDescription` is specific and honest rather than the vague
one-liner that gets bounced.  `LSApplicationCategoryType`,
`ITSAppUsesNonExemptEncryption`, `CFBundleShortVersionString` and
`CFBundleVersion` are all present and correct.  There is no 4.2
minimum-functionality problem -- this is a real instrument, not a utility.

---

## Blocking for upload

### 1. No team, no App ID

`PRODUCT_BUNDLE_IDENTIFIER = com.jefftk.WhistleSynth` with `CODE_SIGN_STYLE =
Automatic` and no `DEVELOPMENT_TEAM` (`project.pbxproj:284,312`).

Register the App ID with the App Sandbox and Audio Input capabilities, pick the
team, and archive with Apple Distribution plus a Mac App Store provisioning
profile.  `build.sh`'s `codesign --sign -` is not submittable -- ad-hoc is real
signing as far as the sandbox is concerned, but not as far as the store is.

Already noted in `mac/README.md`, under "Before submitting to the App
Store".

**Mostly done.**  *(2026-08-29: what follows was true of a working copy and
not of the repository -- see the status block at the top.  Re-applied and
re-verified.)*  The account exists: an **Individual**, paid
(`isFreeProvisioningTeam = 0`) membership, team ID **`C58DMTY76R`**.
`DEVELOPMENT_TEAM = C58DMTY76R` is now set in both target configurations.

The useful discovery is that **setting the team is also what issues the
certificate.**  Before the edit there were zero signing identities on the
machine and archives came out ad-hoc; the next archive after it created one,
with no other action:

```
$ security find-identity -v -p codesigning
  1) "Apple Development: JEFFREY THOMFORDE KAUFHAN (7DKB9675YX)"
     1 valid identities found
```

`CODE_SIGN_IDENTITY` is **deliberately left unset.**  With a team present it
resolves to `Apple Development` on its own, and forcing it explicitly was tried
and changes nothing about the archive.  Pinning it to `Apple Distribution`
would break ordinary builds and buy nothing, because the Organizer's
**Distribute App -> App Store Connect** step re-signs with the distribution
identity and a Mac App Store profile at export time.

What is genuinely left, and all of it happens in Xcode's Distribute flow rather
than in this repository:

* **The App ID is not registered yet.**  Automatic signing registers
  `com.jefftk.WhistleSynth` the first time it needs to.
* **No provisioning profile exists** -- the profiles directory is still absent.
  Fetched during Distribute.
* **The second certificate.**  Distribution needs two, and the second is the
  one that gets forgotten: `Apple Distribution` signs the `.app`, and **`Mac
  Installer Distribution`** signs the `.pkg` that is actually uploaded.

### 2. The submission build has never been produced

`build.sh` and `project.pbxproj` are maintained as parallel truths and only one
of them has been run.  Before trusting the project file, do one full
`xcodebuild -scheme WhistleSynth archive`.

Specifically worth watching: the Frameworks build phase is empty
(`project.pbxproj:53-59`), so the C files depend on clang module autolinking to
pull in AudioToolbox and CoreAudio.  That should work with `CLANG_ENABLE_MODULES
= YES`, but "should" is not a build.

**Done.**  *(2026-08-29: the Frameworks phase edit was committed; the
archive was re-run against `98ae0ef`, still succeeds, and now comes out
signed to the team rather than ad-hoc -- see the status block at the
top.)*  The Frameworks phase links AudioToolbox.framework and
CoreAudio.framework explicitly, so the C files no longer depend on module
autolinking -- and the archive that was supposed to prove it has now been run:

```
xcodebuild -project mac/WhistleSynth.xcodeproj -scheme WhistleSynth \
  -configuration Release -destination 'generic/platform=macOS' archive
** ARCHIVE SUCCEEDED **
```

Universal (`x86_64 arm64`), signature verifies `--deep --strict`, exactly the
two entitlements and **no `get-task-allow`** (archives strip it, which is what
the store requires), hardened runtime on, and the app launches.  The only
warning is a harmless one about there being no AppIntents.framework dependency,
which there is not and should not be.

Nothing in the project file needed fixing to make that work.  Two things about
it are worth writing down anyway, because both are invisible until they bite:

* **A plain `build` action is not a rehearsal for an archive.**  With the
  default destination it produces **arm64 only**, despite `ARCHS = arm64
  x86_64` and `ONLY_ACTIVE_ARCH = NO`.  It is `archive`, or an explicit
  `-destination 'generic/platform=macOS'`, that gets both.
* **The scheme is autocreated, not shared.**  There is no
  `xcshareddata/xcschemes` in the project, so `WhistleSynth` exists on whatever
  machine has opened the project and is not in the repository.  Check "Shared"
  in Manage Schemes if the archive ever needs to run from a clean clone.

---

## Likely to come back as a rejection

### 3. Feedback howl on the reviewer's MacBook

**The biggest concern.**

The root `README.md:94` opens the Run section with "Put on headphones, use a
directional mic, or otherwise avoid letting the output of this program mix with
the input."  The app says this nowhere.

A reviewer will open it on a laptop, on built-in mic and built-in speakers, and
whistle.  Worse: voice 0 is `"raw input (passthrough)"` (`engine.c:74`), sitting
at the top of the Play tab's radio group (`PlayView.swift:18-23`).  That is an
unconditional microphone-to-speaker loop, and it is the first item in the list.

A screech, or a self-sustaining drone the reviewer cannot stop, reads as 2.1 App
Completeness.

Fix: when input and output resolve to the same built-in device, say so on the
Play tab, and warn before passthrough.  `WhistleStatus` already carries
`split_devices` and both device names, so the information is in hand.

**Done, harder than proposed.**  Not a warning: the app *refuses* the built-in
microphone into the built-in speakers.  Warning would have been the wrong call
anyway -- no voice works in that configuration, so there is nothing on the
other side of the warning worth reaching.

`whistle_resolve_route` (new, `whistle.h`) answers "what do these settings
resolve to, and can it be played" without starting anything, so the window can
say so continuously; `whistle_start` checks again and fails, so the rule lives
in one place.  The UI shows a banner *above* the tabs, not a takeover, because
an interface that is plugged in but not selected is fixed on the Audio tab.

The test is the CoreAudio **data source**, not the transport type: a built-in
output reports `'ispk'` for its speakers and `'hdpn'` for the headphone jack,
same device ID and same `'bltn'` transport either way.  Keying on transport
would have refused to run with wired headphones -- the fix, not the problem.
A device that will not answer is treated as fine.

### 4. The app changes system-wide audio device settings at launch, by default,
and never puts them back

`SynthController.swift:107,111` register defaults of 48000 Hz and 64 frames --
not 0.  So a first launch calls `request_sample_rate` and
`request_buffer_frames` (`whistle_audio.c:243,265`) on whatever the reviewer's
default output device is, without anyone having chosen that.

64 frames on a built-in output will make other audio apps glitch, and the rate
change interrupts anything currently playing.  `whistle_stop`
(`whistle_audio.c:690`) does not restore either value, so the machine stays that
way after quitting.

The Audio tab already discloses this ("this affects other apps using it while
Whistle Synth is running", `AudioView.swift:57`), which is the right instinct,
but disclosure does not cover a default nobody picked.

Fix: default both keys to `0` -- the "Device default" pickers already handle
that value -- and stash the prior sample rate and buffer size at start to
restore in `teardown()`.

A reviewer whose Mac sounds wrong after quitting the app is a bad outcome
whatever guideline number gets attached to it.

**Done, but this item conflated two settings that behave differently.**
Measured rather than assumed, with one process holding the device and another
reading it:

| property | other process saw | scope |
|---|---|---|
| `kAudioDevicePropertyBufferFrameSize` | 512 throughout, while we ran at 64 | **per client** |
| `kAudioDevicePropertyNominalSampleRate` | 44100 while held, 48000 after | **device-wide** |

So the buffer size was never the sharing problem: the HAL keeps one per client
and adapts.  "64 frames on a built-in output will make other audio apps
glitch" is not what happens -- other apps keep their own block size, and only
the hardware I/O cycle shortens, which costs a little power.

The fix is therefore split:

* **Sample rate defaults to `0`.**  That one is genuinely device-wide, and
  changing it interrupts whatever is playing.
* **Buffer size defaults to 64**, which is the whole point of the app, and is
  this app's own.
* **Both are restored on stop** -- `request_sample_rate` and
  `request_buffer_frames` remember what the device was doing the first time
  they change it, only the first time so a restart cannot overwrite the
  original with our own previous value, and `teardown` restores after the
  units are closed.  For the buffer size that is insurance rather than a
  correction.

The Audio tab's footer said the buffer size "affects other apps using it while
Whistle Synth is running", which is now known to be false; it has been
rewritten to put that warning on the sample rate, where it belongs.

### 5. "Open Privacy Settings" uses a pre-Ventura pane ID  *(verify)*

`ContentView.swift:99` opens:

```
x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone
```

while `LSMinimumSystemVersion` is 13.0.  On Ventura and later the anchor is:

```
x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Microphone
```

The old string may land on the top of Privacy & Security, or do nothing.  A
button that appears to do nothing is exactly what a reviewer testing the "deny
the permission" path will find.  Test both branches on a current OS.

**Done.**  The Ventura+ anchor is used, with the old string tried only if the
new URL will not open at all.

### 6. No re-check after permission is granted

`permission` is read once in `init` (`SynthController.swift:124`) and only
re-read inside `requestPermission()`'s `notDetermined` branch (`:165-173`).

Granting the microphone in System Settings prompts "Quit & Reopen" / "Later".  A
reviewer who picks Later comes back to a permanently dead screen with no retry
button -- the only control on that screen opens Settings again.

Fix: re-check on `scenePhase` / `didBecomeActive`, and start if it flipped to
authorized.

**Done.**  `NSApplication.didBecomeActiveNotification` re-checks the status and
starts if it flipped; the denied screen also gained an explicit "Check again"
button next to the Settings one, and a line saying what to turn on.  The same
notification re-reads the device list, which is how headphones plugged in while
the app was in the background get noticed.

---

## Worth fixing, lower risk

### 7. `whistle_start` blocks the main thread

The split-mode ring prime busy-waits up to 200ms
(`whistle_audio.c:901-903`), on top of already-synchronous CoreAudio setup.
`restart()` re-enters all of this on every device change and every settings
change, so unplugging an interface mid-session stalls the UI.

Move `whistle_start` off the main thread, or at least prime the ring
asynchronously.

**Done.**  `AudioLifecycle` (new Swift file) owns a serial queue that every
`whistle_start` and `whistle_stop` goes through, which also gives the C side
the single-lifecycle-thread guarantee its comments assumed.  Termination stays
synchronous, because the device has to be handed back -- with #4's restore --
before the process exits.  `running` is read back from the C side rather than
inferred from which callback arrived, so an overtaken start cannot leave a
stale answer on screen.

### 8. Verify the Help menu  *(verify)*

The default SwiftUI menu bar includes a "Whistle Synth Help" item.  With no help
book that shows "Help isn't available for Whistle Synth."  Either add a
`CommandGroup(replacing: .help)` pointing somewhere real, or ship a help book.
Cheap, and a classic 2.1 nit.

**Done.**  `CommandGroup(replacing: .help)` opens a real in-app help window
(`HelpView.swift`), chosen over a link to the GitHub README so it works with no
network -- the app has no network entitlement at all.  Eight topics, including
the headphones rule from #3 and how to set `level_full`.

### 9. Unlabeled sliders for VoiceOver

`VoiceView.swift:105,107` and `PlayView.swift:112-115` use
`Slider(value:in:step:)` with no label, so VoiceOver announces only a
percentage.  Fifty of them on the Voice tab.  Add `.accessibilityLabel(spec.label)`.

**Done.**  Every slider in both files carries `.accessibilityLabel` and
`.accessibilityValue`.

### 10. Voice names are the internal table strings

`"raw input (passthrough)"`, `reese`, `subbass` -- all lowercase, rendered
directly into a user-facing radio group.  Not a rejection, but it reads as
unfinished to someone whose job is deciding whether the app is finished.

**Done.**  `SynthController.displayName(ofVoice:)` maps them through a
hand-written table -- `subbass` to "Sub Bass", `fm` to "FM",
`eight-oh-eight` to "808" -- since every mechanical derivation gets one of
those wrong.  The internal names are untouched, so the CLI and the stored
per-voice edits, which are keyed by internal name, are unaffected.  A voice
with no entry falls back to its internal name capitalised.

### 11. `PrivacyInfo.xcprivacy`

Not required for macOS today -- the required-reason-API enforcement covers iOS,
iPadOS, tvOS and watchOS.  But the app does use `UserDefaults`, which is a
declared-reason API, so adding the manifest with `CA92.1` is a few lines of
insurance against the rule extending to macOS.

### 12. App Store Connect side, not code

Support URL, macOS screenshots, and the App Privacy questionnaire.  The
questionnaire can honestly be answered "Data Not Collected", which matches the
usage string.

**Still entirely open.**  Two things to add to it.

**Set the release to "Manually release this version"** rather than automatic.
This came from #14 originally -- an approved build auto-publishing while the
name correction was pending would have gone live under the misspelled seller
name.  That reason is gone as of 2026-08-29, but the checkbox is still the
right setting for a first submission: it costs nothing and it means approval
and publication are two decisions rather than one.

**Check Agreements, Tax, and Banking.**  It uses the legal name and has to
match your bank and tax records.  #14's correction fixed the membership
record; confirm it reached this section too, because a mismatch here holds up
payouts silently rather than failing loudly.

---

### 13. `build.sh` and the project had drifted on optimization  *(fixed twice)*

`mac/README.md` claimed the project "compiles the same files with the same
settings."  It did not.  `build.sh` compiles the C at `-O2`; the Xcode Release
configuration set no `GCC_OPTIMIZATION_LEVEL` at all, and an Xcode Release that
sets none does **not** inherit `-O2` -- it falls through to Xcode's default of
**`-Os`**.  So the pitch detector and the synth engine, the only code in the
project where this could matter, were being compiled a step down from what the
shell build had been giving them all along, and nothing in either file said so.

Fixed by setting `GCC_OPTIMIZATION_LEVEL = 2` explicitly in the Release
configuration rather than relying on a default that happens to disagree.
Verified in the shared C args response file that every C compile references, on
both architectures.

A second, smaller disagreement in the same area: the target set
`ENABLE_APP_SANDBOX = NO` while `WhistleSynth.entitlements` turned the sandbox
on.  The entitlements file wins -- the app really was sandboxed, confirmed with
`codesign -d --entitlements -` -- but `ENABLE_APP_SANDBOX` is the setting
Xcode's Signing & Capabilities pane reads, so the pane showed no App Sandbox
capability on a sandboxed app.  That is exactly the sort of disagreement
someone eventually resolves in the wrong direction.  Both now say yes, and the
entitlement set on the signed binary is unchanged.

**Watch this one.**  It is the second time the two builds have drifted, and
neither drift was visible without going and looking.  If you change one, change
the other.

### 14. The Apple Developer account had a typo in the legal name
*(corrected with Apple 2026-08-29; two follow-ups left)*

The team name on the account was **"JEFFREY THOMFORDE KAUFHAN"**.  It should
read **Jeffrey Thomforde Kaufman** -- Apple misread it off the ID submitted at
enrollment.  It has since been corrected (see the end of this item); what
follows is why it mattered and how it was done, kept because this is the kind
of thing that recurs and because the reasoning is not obvious from the outside.

This matters because **on an Individual membership the team name is the seller
name shown on the App Store product page**, and App Store Connect's "developer
name" setting is *not available* for Individual accounts -- the developer name
is the legal name.  Updating the name on the Apple Account does not change it
either.  Correcting the membership record is the only route.

The quieter and more expensive problem is **Agreements, Tax, and Banking**,
which uses the legal name and has to match your bank and tax records.
"KAUFHAN" will not, and that is the kind of mismatch that silently holds up
payouts rather than failing loudly.

**How it was fixed.**  Submit through
<https://developer.apple.com/contact/request/update-company-information/> --
despite the name, this is the path Apple's own *Updating your account
information* help page gives for individuals changing a name or address.  It is
also reachable from the Membership details card at
<https://developer.apple.com/account#MembershipDetailsCard>.  Required role is
Account Holder, which on an Individual account is you.

**Frame it as a correction, not a change.**  Do not write "I would like to
change my name" -- that lands in the queue for legal name changes, where they
ask for a marriage certificate or a deed poll.  Write that it is a
transcription error at enrollment, that the ID already on file reads Kaufman,
and that you are asking for the record to be corrected to match it.  It is a
data-entry fix against evidence Apple already holds.

**Nothing in the project depends on this.**  The team ID stays `C58DMTY76R`
through a name correction, so `DEVELOPMENT_TEAM` needs no revisiting.

---

**Corrected, 2026-08-29.**  Apple has the record right: the legal name on the
account is now **Jeffrey Thomforde Kaufman**, which is also what the App Store
product page will show as the seller, since an Individual membership has no
separate developer name.  So this item no longer blocks the release, and #12's
"hold the release manually" is no longer needed *for this reason* -- though it
is still the safer setting for a first submission.

Two things follow from it, neither urgent:

* **The development certificate still embeds the old spelling.**  It was
  issued 2026-08-23, before the correction, and a correction does not reach
  back into a certificate that already exists:

  ```
  subject= CN=Apple Development: JEFFREY THOMFORDE KAUFHAN (7DKB9675YX),
           O=JEFFREY THOMFORDE KAUFHAN, OU=C58DMTY76R
  notBefore=Aug 23 2026   notAfter=Aug 23 2027
  ```

  Revoke it in the developer portal and let automatic signing reissue one; the
  next archive will carry the right name.  It is cosmetic either way -- that
  string appears in `codesign` output and nowhere a user looks.  **The
  distribution certificates matter more and are still unissued**, so they will
  be created with the corrected name as long as they are created *after* this
  point, which they will be: Distribute App issues them.

* **Check Agreements, Tax, and Banking.**  That is the half of this that was
  never about the store listing: the legal name there has to match your bank
  and tax records, and a mismatch holds up payouts silently rather than
  failing loudly.  Confirm it now reads Kaufman, since the correction to the
  membership record does not necessarily rewrite what was already entered
  there.

**Command output quoted elsewhere in this file is pre-correction.**  The
`security find-identity` block under #1 and the `SigningIdentity` line in the
2026-08-29 verification section were both captured from that 2026-08-23
certificate, so they show the old spelling.  They are evidence of what the
archive did, not of what the account says.

**It does not block submission, only release.**  Corrections take days and so
does review; run them in parallel, and hold the release manually per #12.

### 15. There are no App Review notes, and #3 is why there have to be
*(open, and now the likeliest rejection)*

#3 was fixed by *refusing* the built-in microphone into the built-in
speakers.  That is the right behaviour for a player and it is the wrong thing
to hand a reviewer with no explanation: they open the app on a bare MacBook,
see "Headphones needed" above the tabs, and there is nothing they can do from
there that produces a sound.  An app that cannot be made to do anything is a
2.1 rejection whatever the reason for it, and "the developer's own banner
told me to plug something in" is not a defence if nobody said so in advance.

Nothing in this repository or in App Store Connect currently says it.  The
notes field is where it goes, in as many words:

> Whistle Synth turns whistling into a synth line.  It needs headphones or an
> external microphone: the app deliberately refuses to run the built-in
> microphone into the built-in speakers, because at a few inches apart the
> synth hears its own output and howls.  **Please connect any headphones
> (wired or Bluetooth) before testing** -- the banner clears by itself within
> a second of them being connected, and no other setup is needed.  Grant the
> microphone permission when asked, whistle a steady note, and the Play tab
> shows the note it hears while the voice follows it.

Attach a demo video as well.  It is the ordinary remedy for "the reviewer
could not reproduce the working state", and this app's working state depends
on hardware that is not in front of them.

Two smaller things belong in the same notes: the app asks for the microphone
on first launch and does nothing until it is granted, and it changes the
output device's sample rate only if you pick one on the Audio tab -- it starts
on "Device default" and puts back whatever it changed.  Both are things a
reviewer might otherwise flag.

### 16. The refusal only catches the built-in transport type

`is_builtin_speaker` and `is_builtin_mic` (`whistle_audio.c:232-243`) require
`kAudioDeviceTransportTypeBuiltIn` *and* the `'ispk'` / `'imic'` data source.
That is exactly right for a laptop and its headphone jack, which is the case
it was written for.  It catches nothing else.

A Studio Display presents its microphone and its speakers as two separate
USB-transport devices, a few inches apart, pointed at each other and at the
person in front of them.  So does a USB monitor with a webcam, and so does a
docked laptop driving display speakers with a desk microphone.  In every one
of those `builtin_loop` is false, the app starts, and it howls -- which is
the failure #3 exists to prevent, on the hardware a reviewer at a desk is
most likely to be sitting in front of.

This is not a blocker and the fail-open default is still the right one: a
device that will not answer should be played, not refused.  But the test is
narrower than the problem, and the two ways to widen it are worth writing
down before someone widens it the wrong way:

* **Watch for the howl rather than predicting it.**  A feedback loop has a
  signature -- the detector locking to a pitch that rises in level while the
  output is at that same pitch, with no gap between notes.  Catching that
  would cover every arrangement of hardware, including ones nobody has
  thought of, and it costs nothing when it is wrong: mute and say why.
* **Do not** extend the data-source test by guessing at names or transport
  types.  "Refuses to run with the player's actual speakers" is a worse bug
  than the one being fixed, and it is the bug the `'hdpn'` discovery in #3
  already came within one line of shipping.

---

## Order to do them in

**#4 first** -- a one-line default change that removes a whole category of "this
app broke my Mac's audio" complaint.

**#3 second** -- the one most likely to decide the review.

Then #5 and #6, which are small and both sit on the path a reviewer
deliberately walks (deny the permission, then grant it).  #1 and #2 whenever
Xcode is installed.

*Revised 2026-08-23:* #1, #2 and #13 are done.  What is left, in order:
**#14 first** -- it is the only item waiting on someone else, so it wants to be
in flight while you do everything else.  Then **#12**, which is the whole
remaining checklist and is all App Store Connect.  Then the Distribute App run
itself, which is what finishes the rest of #1.

*Revised 2026-08-29:* #1, #2 and #13 are done, with the five project lines
re-applied -- see the status block at the top for why that needed saying
twice, and check `git log` on `project.pbxproj` rather than taking this
paragraph's word for it.  **#14 is corrected**, which takes the one item with
an external clock off the front of this list.  What is left, in order:

1. **#15**, now the likeliest rejection, and it is half an hour of writing
   plus a video.
2. **#12**, the whole remaining App Store Connect checklist.
3. The **Distribute App** run itself, which is what finishes #1: the App ID,
   the profile, and the two distribution certificates all come into existence
   during it -- and, now that #14 has landed, come out under the right name.
4. #14's two leftovers whenever convenient: revoke the old development
   certificate so it reissues without the typo, and check that Agreements,
   Tax, and Banking reads Kaufman too.

**#16 is not on that list.**  It is a real hole and it is not worth holding a
submission for; do it in 1.0.1 when there is a machine with a Studio Display
to test it on.

---

## How the fixes were verified

Still no Xcode, so still no archive.  But `mac/build.sh` builds the same
sources with the same compilers, and the C core can be linked into a plain
command-line harness, which is how the two behavioural claims below were
checked against this machine's real CoreAudio rather than argued for.

**Builds.**  `mac/build.sh` clean, universal, no warnings from clang or
swiftc.  `codesign -d --entitlements -` on the result still shows exactly the
two entitlements, and `PrivacyInfo.xcprivacy` is in `Contents/Resources/`.

**#3, the refusal.**  A harness calling `whistle_resolve_route` and
`whistle_start` on this MacBook's actual default devices:

```
  input      : MacBook Pro Microphone (have=1)
  output     : MacBook Pro Speakers (have=1)
  builtin_loop: YES
  usable      : NO
  whistle_start -> refused
```

To show that the decision turns on the data source and nothing else, the same
harness was rebuilt with `SOURCE_INTERNAL_SPEAKER` changed to `'hdpp'` --
simulating headphones in the jack, since the device ID, the transport type and
everything else stay the same when you plug a pair in.  Same machine, same
devices: `builtin_loop: no`, `usable: yes`, `whistle_start -> started`.  So
headphones are not caught by this, and the stream still starts.

The properties themselves were read off this machine first rather than assumed:
built-in mic `transport=bltn dataSource=imic`, built-in speakers
`transport=bltn dataSource=ispk`.

**#4, the restore.**  Reading the output device's nominal rate and buffer size
before, during and after a stream that deliberately asks for different ones:

```
  before : 48000 Hz, 512 frames
  asking : 44100 Hz, 64 frames
  during : 44100 Hz, 64 frames
  after  : 48000 Hz, 512 frames
```

And the case the "only the first time" guard exists for -- three restarts
through different settings, since `whistle_start` calls `whistle_stop` first:

```
  before      : 48000 Hz, 512 frames
  run 1       : 44100 Hz, 64 frames
  run 2       : 48000 Hz, 128 frames
  run 3       : 44100 Hz, 256 frames
  after stop  : 48000 Hz, 512 frames
```

**Buffer size is per-client, sample rate is not.**  One process holding the
default output device while another reads it:

```
  --- idle ---
  other process sees: 512 frames, 48000 Hz, running-somewhere=0
  --- while another process holds 64 frames / 44100 Hz ---
  other process sees: 512 frames, 44100 Hz, running-somewhere=1
  --- after release ---
  other process sees: 512 frames, 48000 Hz, running-somewhere=0
```

That is what the split default in #4 rests on, and it is one OS version
(macOS 26) on one device (built-in output), which is why the restore stays in
place for both.

**Not verified, and why.**  #5's two URLs were not clicked -- opening System
Settings needs a person, and the fallback only fires if the scheme itself
fails.  #6's re-check and #8's help window are ordinary SwiftUI wiring that
compiles; they want one pass by hand.  #9's VoiceOver labels want VoiceOver.
And #2's archive wants Xcode.

---

## The Xcode session (2026-08-23)

Xcode 26.6 was installed between the two halves of this document.  What that
made checkable, and what it turned up.

**The install needed one fix first.**  `xcode-select` was still pointing at the
Command Line Tools, so `xcodebuild` did not run at all:

```
$ xcodebuild -version
xcode-select: error: tool 'xcodebuild' requires Xcode, but active developer
directory '/Library/Developer/CommandLineTools' is a command line tools instance
```

`sudo xcode-select -s /Applications/Xcode.app/Contents/Developer` fixes it.
(`DEVELOPER_DIR=` in the environment is the no-sudo workaround, if you ever
need to drive a different toolchain for one command.)

**The archive.**  Succeeds; universal; real signature chaining to Apple Root
CA; `TeamIdentifier=C58DMTY76R`; exactly the two entitlements with no
`get-task-allow`; hardened runtime; launches without crashing.  Details under
#2 and #1.

**The project needed two changes**, both under #13: `GCC_OPTIMIZATION_LEVEL = 2`
in Release (it was silently `-Os`), and `ENABLE_APP_SANDBOX = YES` to stop the
build setting disagreeing with the entitlements file.  Plus
`DEVELOPMENT_TEAM = C58DMTY76R` under #1.  Five lines in `project.pbxproj`
altogether; no source changes.

**`mac/build.sh` still builds clean** after all of it -- universal, zero
warnings.  The two builds are back in agreement, which is the property #13 is
about keeping.

**What this session could not check.**  The distribution signing path -- Apple
Distribution, the Mac Installer Distribution certificate, the Mac App Store
provisioning profile, and the `.pkg` -- none of which exist until Xcode's
Distribute App flow runs.  So the archive is verified end to end; the *export*
of it is not, and that is the last unproven step before upload.

---

## The 2026-08-29 pass

What was run, against the tree at `98ae0ef` plus the five project lines this
pass re-applied.

**The five lines were missing.**  `git log -p` on `project.pbxproj` has no
`DEVELOPMENT_TEAM`, `ENABLE_APP_SANDBOX` or `GCC_OPTIMIZATION_LEVEL` line
anywhere in its history, and the archive built before re-applying them said so
plainly:

```
$ plutil -p WS.xcarchive/Info.plist
    "SigningIdentity" => ""
    "Team" => ""
$ codesign -dvvv .../WhistleSynth.app
    Signature=adhoc          TeamIdentifier=not set
```

`-Os` was in the C compile too, read out of the response file every C compile
references rather than inferred:

```
$ grep -o '\-O[a-z0-9]*' .../Objects-normal/arm64/*common-args.resp
-Os
```

**After re-applying them**, the same archive:

```
    "SigningIdentity" => "Apple Development: JEFFREY THOMFORDE KAUFHAN (7DKB9675YX)"
    "Team" => "C58DMTY76R"
    Architectures => x86_64, arm64
    CodeDirectory flags=0x10000(runtime)      -- no adhoc, no get-task-allow
    Authority=Apple Development ... Apple Worldwide Developer Relations ... Apple Root CA
    TeamIdentifier=C58DMTY76R
    entitlements: app-sandbox, device.audio-input, and nothing else
    codesign --verify --deep --strict: valid on disk, satisfies its Designated Requirement
    -O2 in the shared C args
    zero warnings
```

`ENABLE_APP_SANDBOX = YES` changed no entitlement on the signed binary, which
is the thing to check when adding it: the pane now agrees with the
entitlements file rather than overriding it.

**`mac/build.sh` still builds clean** -- universal, no warnings from clang or
swiftc -- so the two builds are in agreement again.

**Still not checked.**  Everything that needs a person or the account: the
export half of Distribute, the VoiceOver pass in #9, the two System Settings
URLs in #5, and the help window in #8.  And #16 wants hardware that is not
here.
