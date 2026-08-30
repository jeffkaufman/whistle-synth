#include "synth.h"

#include <math.h>
#include <string.h>

// The tone is a pulse wave, built additively.  Partial n of a width-w pulse
// has amplitude sin(n*pi*w)/n, and sweeping w is the classic PWM lead
// movement: at w=0.5 the even partials null out and it goes hollow and
// square, and as w narrows they fill back in and it turns nasal and bright.
//
// Additive rather than a shaped ramp because we can then simply stop before
// Nyquist and never alias, and because it makes the harmonic rolloff -- the
// thing that has to track how hard the player is blowing -- a direct
// multiply instead of a filter to tune.
//
// `out_gain` is not a taste control.  Each one is set so that the presets
// measure the same loudness -- equal LUFS, ITU-R BS.1770, which is the
// standard model for perceived loudness and the reason one bass voice has to
// be numerically much hotter than another to sound as loud.  Changing one
// means re-running the match, not just adjusting that voice.  The match is
// run over recordings/whistling.f32, a recording of the real instrument,
// because it depends on the material: matched on synthetic test tones the
// numbers come out several dB different.
//
// Three voices deliberately sit *below* the match, and the offsets are the
// only numbers here that are taste rather than measurement.  `reese` is one
// volume step down and the two drawbars are two and a half, in the same
// 3.5dB-a-step units the volume knob uses (see `volume_steps` in engine.c),
// so the offsets are 0.667x and 0.363x of what the match asked for.  Half a
// step is geometric like the knob itself -- 1.5^-0.5, half of 3.5dB -- rather
// than halfway between two of its numbers.  Equal LUFS is equal
// *loudness*, and these three are not doing the same job as the rest: the
// drawbars are a sustained pad with a leslie chewing on it, which measures
// like a bass line and sits on the ear like a wall, and `reese` carries more
// going on above the fundamental than the LUFS filter charges it for.  Undo
// the offset and they are correct on a meter and too loud in a room.
//
// Re-running the match therefore does not just replace these three numbers:
// it produces the matched value, which then has its offset applied again.
//
// It is run through a model of the speaker rather than on the signal itself:
// a QSC K10.2, flat to about 55Hz and then a ported cabinet's cliff.  Every
// voice here is a bass voice and several of them put real energy under that
// corner, so matching them full-range matches a system nobody is listening
// on.  All of them land on -21.0 LUFS through that filter, and on a
// full-range system they therefore do *not* measure equal.
//
// The one exception is `pluck`, which says so where it sits: a plucked
// envelope has a high crest factor by design, an integrated meter always
// reads that low, and it is set by peak instead.
//
// The ceiling on the whole table is `subbass`, which reaches 0.82 peak at
// this target.  Nothing can go louder without it clipping.

// The sustain: what a note does after the player stops, when the control is
// on.  Everything here is measured over recordings/holding.f32 and
// recordings/scoop-up.f32, and all of it follows from one fact about the
// input -- a whistle does not stop, it trails off, and by the last hop the
// detector still calls voiced the input is a median 22-30dB under the note,
// having got there in about 40ms.
//
// The voice follows that all the way down, because while there is input this
// is the voice it always was and nothing here is allowed to change that.  So
// the tail cannot inherit where the note ended; it inherits the note, through
// the weighted averages below.
//
//
//
// The window the held pitch is averaged over, weighted by how loud the note
// was at the time.  Level-squared rather than level, which is the same idea
// carried one step further: the scoop into the note and the fade out of it are
// both quiet, and weighting by power rather than amplitude discounts them
// harder.  Measured over notes longer than 300ms, the held pitch lands a
// median of 5.4 cents from the note's level-weighted pitch, against 83 cents
// for the pitch at the last voiced hop.
//
// This is the half that carries the pitch, rather than the droop test above:
// weighting by power makes *when* the note was let go stop mattering, because
// the quiet end of it was contributing almost nothing either way.
#define SYNTH_HOLD_PITCH_S 0.5f
//
// And the sustain the note moves into once it is being held: this far under
// the level it was played at, arrived at over this long, in pitch and in
// level together.
//
// A note that held at exactly the level it was played at was the obvious
// thing to build first and the wrong thing to play -- a bass line and its own
// tail at the same volume is two bass lines.  6dB under is the tail sitting
// beneath the next phrase instead of competing with it.
//
// The 250ms is a ramp rather than a filter, so it takes 250ms whatever the
// interval, and it is shaped to leave and arrive at zero velocity.  That
// matters more here than anywhere else in the file: this is the one move in
// the whole synth that is not the player's, so if it can be heard happening
// it will be heard as a fault.
//
// Coming back out of it is much quicker -- the preset's own attack -- because
// a note struck while the last one is still sustaining has to speak at full
// straight away.  It is also what makes a wrong guess about the note ending
// cheap: the median one lasts 76ms, which gets 22% into the ramp, so 1.3dB
// and a fifth of the pitch correction, both undone in 25ms.
// 0.57 rather than a round -6dB, because what it scales is the note's
// power-weighted mean level rather than the body of the note, and the mean
// sits a little under the body because it averages the whole of it.
// Measured over recordings/holding.f32 against the rendered level across the
// body of the note that made each tail, this lands them a median 6.0dB under.
// The number to keep true is that measurement, not this constant.
// A held note keeps sounding at full after the player stops, and keeps doing
// it: the hold does not expire.  It used to run for two seconds and then
// fade, which made a drone something you topped up rather than something you
// set going -- staying under a phrase meant re-whistling the same note every
// couple of bars, which is the breath the sustain exists to remove.  So the
// only things that end a tail are the next note and the switch, both of them
// the player deciding it is over.
//
// Which leaves this as the fade for the second of those: letting go of a
// drone by switching the sustain off.  It was the `reese-hold` preset's
// release_s before the sustain became a control, and it is a property of the
// tail rather than of any voice, so every voice gets the same one.  0.6s is a
// time constant, so a released tail is 8.7dB down after 0.6s and 60dB down
// after 4.1 -- measured over a real tail let go this way, 9.3dB and 58.9dB,
// the difference being the settle level it is let go from.  A note that never earned a tail is untouched by any of
// this and releases at its own release_s.
#define SYNTH_HOLD_RELEASE_S 0.6f

#define SYNTH_HOLD_SUSTAIN 0.52f
#define SYNTH_HOLD_SETTLE_S 0.25f
//
// Concert pitch, as the frequency of A.  The sustain -- and only the sustain
// -- lands on the nearest equal-tempered semitone.
//
// It is the one place in this program that has an opinion about absolute
// pitch, and the asymmetry is the point.  A bass line wants every cent of
// what was played: the scoops, the slides and the fact that a whistle is
// fretless are the expression.  A note left ringing under the tune has no
// such freedom -- it is either in tune with the mandolin or it is a beat, and
// nothing about it is moving to disguise that.  So the note you play is
// exactly what you whistled, and what it settles into is a real note.
//
// No hysteresis, unlike the pad that used to do this: the average is frozen
// when the note ends, so there is one snap per note and nothing to dither.
// A band that tunes to 442 changes this number.
#define SYNTH_HOLD_SNAP_HZ 440.0f
//
// Three things a holding voice has to decide that the voices which follow the
// whistle never have to, because they are silent whenever the player is.  All
// three were measured over recordings/holding.f32, which is what
// `--record-hold` asks for.
//
// An onset arriving while a tail is sounding, at a level this far under what
// is being held, is the detector re-triggering on the noise floor of a rest
// rather than a note starting.  Over that recording the two are not close: of
// the 194 onsets that landed on a sounding tail, eight came in at 0.8-1.8% of
// the level being held and the next one up was at 8.1%.  4% sits in the
// middle of an empty gap.
//
// It has to be caught because an onset is the one event that resets
// everything at once -- the level, the timbre and the running pitch average
// -- so a spurious one both craters the tail and parks it on the detector's
// reading of room noise.  Measured, they held 1736Hz and 2063Hz against a
// whistle around 1080Hz, at a level 27dB under the sustain the same note gets
// when no spurious onset lands in it.
#define SYNTH_HOLD_ONSET_FLOOR 0.04f
//
// How long the player has to stay on a note before it earns a tail.  Every
// note getting one is what makes a fast phrase snap: nothing else
// distinguishes the gap between two tongued notes from the end of a phrase.
//
// Swept over the same recording, against notes from the sections where a hold
// is wanted and the sections where it is not:
//
//                  holds wanted     holds unwanted
//     0.35s          30/34              9/117
//     0.50           23/34              8/117
//     0.75           15/34              3/117
//
// There is no threshold that separates them cleanly, because a deliberate
// note and a long note in a phrase are the same thing played with different
// intent.  0.5s is the strict end of the useful range: it costs a third of
// the notes that wanted a tail and buys back nearly all of the ones that did
// not.
//
// The steadiness of the pitch, which would be the other half of "held at one
// note", turns out not to be worth testing: over the same notes the
// deliberate ones sit a median 7.4 cents from their own median pitch and the
// continuous ones 12.0, which is not a separation.  The length does the work.
#define SYNTH_HOLD_MIN_NOTE_S 0.5f
//
// Movement in the tail.  A sustained note has to be worth listening to for
// several seconds, and only `reese` gets that for free: its three copies are
// detuned against each other, so its partials wax and wane on their own.
// Every other voice here is one oscillator with nothing to beat against, and
// held still it is an organ note.
//
// So the partials are moved directly, in SYNTH_SHIMMER_LFOS groups on that
// many slow incommensurate LFOs.  Applied to each partial's amplitude before
// the power normalisation, so what moves is the *balance* between partials
// and not the level -- a drone that pumps is worse than one that sits still,
// and the whole point of the normalisation is that the timbre can move
// without the loudness following it.
//
// Scaled by the settle ramp, so it is exactly zero while the player is on the
// note and fades in as the tail arrives.  This belongs to the tail, not to
// the voice: with the sustain off, or on a note too short to earn a tail,
// none of it exists.
//
// The FM voices take it on the index instead, because their whole spectrum is
// one number and there are no partial amplitudes to move.  Tuned separately
// rather than shared, since the two numbers do not mean the same thing: an
// index wobble moves energy across the whole Bessel envelope at once.
// And how long the LFOs take to come up to speed.  They start stopped, at
// phase zero, and accelerate to the rates above over this long.
//
// Fading the *depth* in was the obvious way to make the movement arrive and
// it is not as good, because it fades in movement that is already underway:
// the LFOs are free-running, so whatever phase they happen to be at when the
// player stops is where the partials get pulled towards, and the note leans
// somewhere for no reason.  Starting them from rest instead means the first
// motion is the slowest motion, which is what "coming from nowhere" actually
// requires.
//
// There is a second reason, and it is the one that makes this exact rather
// than merely gentle.  All three start at phase zero, so at that instant they
// are equal, and multiplying every partial by the same number is precisely
// what the power normalisation below divides back out.  The movement is not
// small at the moment the player stops -- it is zero, identically -- and it
// emerges only as the LFOs drift apart.  Nothing has to be faded at all.
//
// A second, not two.  A tail now runs until something ends it, but what it
// has to survive is the first few seconds of being left alone -- the ones a
// player actually waits through before the next phrase -- and spending half
// of that getting up to speed leaves the movement no room to be movement.
// Measured over the tails in recordings/holding.f32 when they were two
// seconds long plus a 0.6s release, band wander across the whole tail against
// `reese`'s natural 4.44dB:
//
//                 0.0s   1.0s   1.5s   2.0s     (acceleration time)
//    bass         3.94   1.57   0.91   0.51
//    eight-oh-eight 3.38 2.75   2.46   1.75
//    square       2.11   1.66   1.35   0.79
//
// At 1.0s the movement is still emerging rather than switching on -- it has
// moved the spectrum 0.1dB at a quarter second, against 1.3dB when the depth
// was faded on the settle ramp -- and there is still a tail left to move.
#define SYNTH_TAIL_SHIMMER_S 0.5f
// The most the audibility compensation may ask for, as an amplitude ratio.
// 4x is 12dB.
#define SYNTH_AUDIBILITY_MAX 4.0f

// How many FM sidebands either side of the carrier to count.  The index stays
// under 5 and J_n(x) is negligible past about x + 5.
#define SYNTH_FM_TERMS 12

#define SYNTH_TAIL_SHIMMER 0.35f
#define SYNTH_TAIL_SHIMMER_FM 0.30f
static const float synth_shimmer_hz[SYNTH_SHIMMER_LFOS] = { 0.31f, 0.53f, 0.79f };

static const struct SynthParams presets[] = {
  {
    // Four octaves down: a comfortable whistle lands around 50-160Hz, which
    // is where an electric bass actually plays.  (Five octaves, which an
    // earlier version of this program used, is below the low E.)
    .name = "bass",
    .tilt = 1.0f,
    .octave = 0.0625f,
    // No movement to speak of.  A wobbling bass fights the piano's left hand
    // and turns to mud in a PA.
    .pwm_center = 0.30f, .pwm_slow_hz = 0.11f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    // Keep the fundamental dominant when played softly and open up when dug
    // into, like a filter tracking the pluck.
    .cutoff_soft = 1.5f, .cutoff_loud = 7.0f, .rolloff_exp = 2.0f,
    .drive_soft = 1.0f, .drive_loud = 3.0f,
    // Strictly one voice.  Detuned bass beats against itself and smears the
    // low end, and it is the note starting exactly on time that makes a bass
    // line feel tight.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.040f, .articulation_s = 0.008f, .glide_s = 0.003f,
    .out_gain = 0.286f,
  },
  {
    // Five octaves down, which is where the old `ebass` voice lived and what
    // this is here to bring back.  A comfortable whistle lands around
    // 25-80Hz: below the bottom of an electric bass, felt as much as heard.
    .name = "subbass",
    // No 1/n rolloff at all, which sounds like the wrong direction for a
    // dark voice but isn't: the cutoff below does the darkening, and it has
    // to be set low enough that a pulse's own rolloff on top would drop the
    // second partial under the first and lose the character.  The two work
    // against each other and this is where they balance.
    .tilt = 0.20f,
    // Matches the old voice closely: partial 3 lands at 3.12 against its 3.11,
    // partial 4 at 4.24 against 4.3, partial 5 at 5.40 against 5.7.
    .stretch = 0.014f,
    .octave = 0.03125f,
    // A narrow pulse puts the second partial above the first, which is what
    // the old voice did (0.24 against 0.20) and is why it read as pitched
    // rather than as rumble: the octave up carries the note, the fundamental
    // carries the weight.
    .pwm_center = 0.21f, .pwm_slow_hz = 0.09f, .pwm_slow_depth = 0.045f,
    // Both the stretch and this wobble are deliberately restrained.  Pushed
    // any harder they stop reading as a growl and start reading as
    // distortion, which is what a sub-bass least wants: down here the ear
    // takes any harshness in the partials as the whole character, because
    // the fundamental is more felt than heard.
    .growl_hz = 5.0f, .growl_depth = 0.035f, .growl_onset_s = 0.25f,
    .cutoff_soft = 2.6f, .cutoff_loud = 4.5f, .rolloff_exp = 4.0f,
    .drive_soft = 0.5f, .drive_loud = 1.1f,
    // One oscillator.  At 30Hz two detuned copies beat over about ten
    // seconds, so the fundamental would slowly vanish and come back.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 14,
    .min_partial_hz = 22.0f,
    .level_full = 0.22f,
    .attack_s = 0.005f, .release_s = 0.050f, .articulation_s = 0.012f, .glide_s = 0.004f,
    .out_gain = 0.422f,
  },
  {
    // Octaveless: a Shepard tone you can play.  The partials are octaves
    // rather than harmonics, and their loudness comes from a bell fixed in
    // Hz, so whistling an octave higher slides the whole stack one slot along
    // a curve that hasn't moved and lands on exactly the spectrum it started
    // from.  Play a rising scale and it rises without ever arriving: what
    // fades in at the bottom is what fades out at the top.
    //
    // The nominal fundamental is eight octaves down, which puts it at 2-12Hz.
    // That is not a pitch, it is the spacing of the stack; what you hear is
    // the bell, centred on 82Hz -- low E -- with nine octaves above the
    // fundamental reaching 3.1kHz at the top of the whistle range.
    //
    // The sizing is the whole design.  The whistle spans 2.5 octaves and the
    // stack spans 8, so the bell has about 5.5 octaves to sit in without
    // either tail running off an end, which is what would break the wrap.  A
    // sigma of 0.9 octaves fits with room to spare: the outermost component
    // is 60dB down at both extremes of the range.
    .name = "octaveless",
    // Deeper than the rest, and it takes it: nine partials an octave apart
    // move far more audibly than thirty-two in a harmonic series, and this is
    // the one voice with no register of its own to hold the interest.  At the
    // table's 0.35 it wandered 2.1dB across a tail against `reese`'s 4.6; at
    // 0.9 it is 5.5.  Kept under 1 so no partial's amplitude crosses zero.
    .shimmer_depth = 0.9f,
    .octave = 0.00390625f,   // 2^-8
    .octave_stack_hz = 82.0f, .octave_stack_width = 0.9f,
    .unison = 1, .harmonics = 9,
    // Nearly off, and that is a constraint rather than a taste.  Difference
    // tones between octave-spaced partials are themselves octave-spaced and
    // survive the wrap; the odd-order products at 3f and 5f are not, and they
    // are what would put an audible seam in the illusion.  This is as much
    // grit as the trick will take.
    .drive_soft = 0.8f, .drive_loud = 1.3f,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    // A longer glide than the other basses want, because the illusion needs
    // continuity: heard as a smooth sweep the stack has no octave at all,
    // and heard as discrete jumps the ear starts tracking one component and
    // finds the seam.
    .attack_s = 0.006f, .release_s = 0.090f, .articulation_s = 0.010f,
    .glide_s = 0.020f,
    .out_gain = 0.363f,
  },
  {
    // The Reese: two or three saws detuned far enough that the beating is the
    // instrument.  Four octaves down, so a comfortable whistle is 50-160Hz --
    // at the bottom of that the outer copies beat about twice a second, which
    // is the slow churn the sound is named for, and at the top about six.
    //
    // Everything `bass` says about not detuning a bass is true and this
    // ignores all of it on purpose: the smear *is* the patch.
    .name = "reese",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.28f, .pwm_slow_hz = 0.07f, .pwm_slow_depth = 0.03f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 3.0f, .cutoff_loud = 12.0f, .rolloff_exp = 2.0f,
    // Enough peak to hear the corner move, not enough to whistle.
    .resonance = 5.0f, .resonance_width = 0.35f,
    .drive_soft = 1.2f, .drive_loud = 2.6f,
    .unison = 3, .detune_cents = 9.0f, .harmonics = 32,
    // The bottom two partials come from one copy.  Measured before that, the
    // fundamental swung 31dB as the three copies drifted in and out of phase
    // -- in stereo that is the sound of a Reese and in mono it is the bass
    // falling out of the tune every couple of seconds.  The churn is still
    // there, it just lives above the octave now.
    .mono_partials = 2,
    .level_full = 0.22f,
    .attack_s = 0.008f, .release_s = 0.060f, .articulation_s = 0.008f,
    .glide_s = 0.006f,
    // 0.276 matched, one volume step down: see the offsets in the header
    // comment.  Everything this voice has going on above the fundamental is
    // loudness the meter does not charge it for.
    .out_gain = 0.184f,
  },
  {
    // The 808: nearly a sine, with the pitch dropping half an octave over the
    // first 35ms.  That drop is the whole sound -- it is what a drum machine
    // put there to fake the thump of a skin, and without it this is just a
    // sub.
    //
    // The release is 70ms, not the 300 an 808 actually rings for.  A record's
    // 808 is one note every beat or two with space around it; a bass line
    // under a dance tune is not, and at 300ms the notes run into each other
    // and the line stops being playable.  The pitch drop is what makes this
    // an 808, not the tail.
    //
    // Four octaves down rather than five, which is where the register wants
    // to be for this and not just where the other sub voices sit: an 808 is
    // its fundamental and almost nothing else, so it has to stay above
    // `min_partial_hz`.  Five octaves puts most of the whistle range under
    // 25Hz, and the voice would spend its time playing its second partial.
    .name = "eight-oh-eight",
    .tilt = 1.2f,
    .octave = 0.0625f,
    .pwm_center = 0.40f, .pwm_slow_hz = 0.05f, .pwm_slow_depth = 0.01f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.2f, .cutoff_loud = 6.0f, .rolloff_exp = 2.0f,
    .drop_octaves = 0.6f, .drop_s = 0.035f,
    // The dirt an 808 gets from being turned up.  Not a garnish: an 808 on a
    // record has been through a desk and a PA and is audibly saturated, and
    // that saturation is most of why it survives being played on a phone.
    .drive_soft = 1.6f, .drive_loud = 4.0f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 12,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.070f, .articulation_s = 0.012f,
    .glide_s = 0.004f,
        // Deliberately 3.2dB off the equal-LUFS match the rest of the table
    // keeps, which is the one thing the note at the top of this file says not
    // to do.  The reason: measured full-range this voice is level with the
    // others, and measured through a model of a K10.2 -- flat to about 55Hz,
    // then a ported box's 24dB/octave cliff -- it is 3.2dB under the lead.
    // So is every other bass voice, within a dB.  But this one is nearly pure
    // fundamental, and a near-sine at 40-200Hz is the case where a K-weighted
    // measurement most over-states what you actually hear.  Set by ear on the
    // speaker it gets played through, which beats the model.
    //
    // If the bass voices as a family turn out to sit low on that box, the fix
    // is to re-run the whole match against the same filter, not to keep
    // nudging one preset.
.out_gain = 0.254f,
  },
  {
    // A plucked bass: the first voice here whose note has a shape of its own.
    // Everything else sounds for exactly as long as you whistle, which is an
    // organ; this one speaks hard and falls back to a quarter of its peak in
    // a third of a second, so a run of notes has space between them and the
    // pulse comes from the attacks rather than from the gaps.
    .name = "pluck",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.30f, .pwm_slow_hz = 0.09f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 1.6f, .cutoff_loud = 7.0f, .rolloff_exp = 2.0f,
    // The filter falls with the note, which is what a plucked string does and
    // what makes the attack read as an attack rather than as a volume bump.
    .resonance = 2.5f, .resonance_width = 0.40f,
    .cutoff_env_octaves = 2.0f, .cutoff_env_s = 0.12f,
    // At 110bpm an eighth note is 270ms, so the decay has to do most of its
    // work inside that or the notes just run together at a lower level and
    // nothing has been gained.  150ms to a tenth of the peak puts a note
    // 11dB down by the time the next one lands.
    .decay_s = 0.15f, .sustain_level = 0.12f,
    // The one voice the sustain control does not apply to; see no_sustain.
    .no_sustain = true,
    .drive_soft = 1.0f, .drive_loud = 2.8f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.003f, .release_s = 0.060f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
        // Set by peak rather than by the K10.2 match the rest of the table uses,
    // and so it measures about 3.5dB under them.  A plucked envelope has a
    // high crest factor on purpose -- that is what an attack *is* -- so an
    // integrated measurement always reads low against a voice that sits at
    // one level, and matching it there put 0.18% of samples over 0.75 with
    // the peak at 0.985.  It will not sound quiet: the attacks arrive at the
    // same height as everything else, and on a dance floor the attack is the
    // part being listened to.
.out_gain = 0.567f,
  },
  {
    // Two-operator FM, and the only voice here that isn't a pulse wave.  The
    // modulator sits at the fundamental and the index runs 0.8 to 3.0 with
    // how hard you blow.
    //
    // Worth having because of *how* it opens up rather than how it sounds
    // standing still: a filter uncovers partials that were already there in
    // order, while the index moves energy around by Bessel functions, so
    // partials rise and fall out of order and some of them invert on the way.
    // It is the sound of a DX bass and there is no filter setting that
    // reaches it.
    .name = "fm",
    .octave = 0.0625f,
    .fm_ratio = 1.0f, .fm_index_soft = 0.8f, .fm_index_loud = 3.0f,
    .drive_soft = 1.0f, .drive_loud = 1.8f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 1,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.004f, .release_s = 0.050f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
    .out_gain = 0.307f,
  },
  {
    // The same FM, five octaves down instead of four: a comfortable whistle
    // lands at 25-80Hz, which is `subbass`'s register.  It exists because FM
    // is the one generator here whose brightness does not come from
    // uncovering partials, and that matters most exactly where the
    // fundamental cannot be reproduced -- on a K10.2 everything under 55Hz is
    // gone, so what you actually hear down here is the sidebands, and the
    // index is a direct control over how many of them there are and how far
    // up they reach.
    .name = "fm-sub",
    .octave = 0.03125f,
    // A wider index range than `fm`, and pushed higher at both ends.  Four
    // octaves down the fundamental does most of the work and the index is
    // colour; five octaves down the fundamental is inaudible on most of what
    // this gets played through, so the index is what makes the note speak at
    // all.  At index 4 on a 34Hz note the sidebands reach about 170Hz, which
    // is where a small box starts being able to help.
    .fm_ratio = 1.0f, .fm_index_soft = 1.2f, .fm_index_loud = 4.0f,
    // Gentler than `fm`.  Down here the ear takes any harshness in the
    // partials as the whole character, the same reason `subbass` runs its
    // drive at 0.5-1.1 -- and FM is already supplying the harmonics that a
    // drive would otherwise have to invent.
    .drive_soft = 0.9f, .drive_loud = 1.6f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 1,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    // 22 rather than 25, matching `subbass`: the high-pass sits a little
    // below the lowest thing worth keeping, and at 25 it would be thinning
    // the bottom of this voice's own range rather than clearing out what the
    // drive invented underneath it.
    .min_partial_hz = 22.0f,
    .level_full = 0.22f,
    .attack_s = 0.005f, .release_s = 0.055f, .articulation_s = 0.010f,
    .glide_s = 0.004f,
    .out_gain = 0.394f,
  },
  {
    // Valve grind.  The saturator is pushed off centre before it clips, which
    // is the difference between a transistor and a valve: an odd function can
    // only make odd harmonics however hard you drive it, and the bias is what
    // puts the even ones in.  Those land an octave above the fundamental --
    // in the gap between this and a mandolin's low G, which is otherwise the
    // emptiest part of the arrangement.
    .name = "grind",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.32f, .pwm_slow_hz = 0.08f, .pwm_slow_depth = 0.02f,
    .growl_hz = 5.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.5f, .cutoff_loud = 9.0f, .rolloff_exp = 2.0f,
    .drive_soft = 2.0f, .drive_loud = 5.0f, .drive_bias = 0.45f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.005f, .release_s = 0.050f, .articulation_s = 0.008f,
    .glide_s = 0.004f,
    .out_gain = 0.246f,
  },
  {
    // A hollow square and nothing else: width exactly 0.5, which nulls every
    // even partial, no detune, no sweep, and as little drive as the voice
    // will take.  There is no trick in it.  It is here because a plain square
    // bass is a real sound with a long history and this table had no
    // deliberately plain voice in it to check the others against.
    .name = "square",
    .tilt = 1.0f,
    .octave = 0.0625f,
    .pwm_center = 0.50f, .pwm_slow_hz = 0.0f, .pwm_slow_depth = 0.0f,
    .growl_hz = 0.0f, .growl_depth = 0.0f, .growl_onset_s = 1.0f,
    .cutoff_soft = 2.0f, .cutoff_loud = 8.0f, .rolloff_exp = 2.0f,
    .drive_soft = 0.6f, .drive_loud = 1.0f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 32,
    .min_partial_hz = 25.0f,
    .level_full = 0.22f,
    .attack_s = 0.003f, .release_s = 0.040f, .articulation_s = 0.008f,
    .glide_s = 0.003f,
    .out_gain = 0.493f,
  },
  {
    // A drawbar organ through a leslie on fast, and the first voice here that
    // is not a bass.  Nine sine partials at the footages a Hammond's drawbars
    // sound at, two rotors swinging their amplitude and their pitch, and a
    // valve amp between them.
    //
    // Two octaves down, which lands a 550-3150Hz whistle at 137-787Hz: C#3 to
    // G5, the register a right hand plays in.  That is where the drawbars
    // have room -- the 16' sits an octave under the note and the 1' three
    // octaves over it, so the voice occupies five octaves from wherever the
    // note is, and one octave down would put its top above the whistle that
    // asked for it.
    //
    // Nothing here is a filter, and that is the point.  Every other voice in
    // this table gets brighter when the player leans in; a tonewheel organ
    // cannot, because there is no filter anywhere in it and the only thing on
    // the instrument that changes the timbre is a drawbar somebody has to
    // stop and pull.
    //
    // It does not get louder either -- see `contact_level`.  So the breath
    // has to go somewhere, and where it goes is the leslie: whistle harder
    // and the rotors spin up.  That is not a substitute for the missing
    // dynamics, it is the expression the instrument actually has; an organist
    // ends a phrase by kicking the speed switch, not by playing louder.
    .name = "drawbar",
    .octave = 0.25f,
    // 86 8643 222, which is a full registration with the fifth pulled back:
    // 16', 8' and 4' full for the weight, and a taper above them.
    //
    // The 1' was at 4 to begin with, on the theory that two octaves down puts
    // it at 1.1-6.3kHz and that is where a mono PA needs a lead to live.  On
    // headphones that was right and on the K10.2 it was wrong -- see `top_hz`
    // -- so it is back on the taper where the other upper drawbars are.  The
    // theory was about a speaker and should have been settled on one.
    //
    // The 5 1/3' is the one drawbar set by what this instrument is rather
    // than by what a Hammond does.  It sounds a fifth *above* the note, and
    // an organist playing chords hears that as the hollow, slightly out-of-
    // tune weight the drawbar is famous for; a monophonic line played at 8
    // instead grows a second melody in parallel fifths, which fights whatever
    // the arrangement is doing.  At 6 it is 6dB under the note and reads as
    // colour on one line rather than as two.
    .drawbars = { 8, 6, 8, 6, 4, 3, 2, 2, 2 },
    // Sixteen, because the 1' drawbar is partial 16 of the half-note series
    // and there is nothing above it.  Ten of the sixteen are silent, which
    // costs a multiply each and is what buys the footages coming out exactly
    // where a drawbar puts them.
    .harmonics = 16,
    // One tone generator.  A Hammond's ninety-one tonewheels all turn off one
    // shaft, so its partials cannot drift against each other at all -- what
    // moves in this sound is the leslie, and detuning would be a different
    // instrument.
    .unison = 1, .detune_cents = 0.0f,
    // 330rpm to 510rpm on the horn, 280 to 430 on the drum, on how hard the
    // player is blowing.  A 122's tremolo is 400 and 340, so this range sits
    // around a real cabinet on fast and reaches past what the motor does,
    // which is the point of it being a control instead of a switch.
    //
    // The floor started at 3.9Hz, a genuinely moderate spin, and came up
    // because of where the player actually sits when the voice is coming out
    // of a PA rather than headphones: they whistle more quietly, and quieter
    // whistling was landing the rotors at the bottom of the range and leaving
    // them there.  Simulated by attenuating recordings/whistling.f32, the
    // horn's median over the notes played:
    //
    //     as recorded    -6dB    -9dB
    //        5.0Hz       4.4     4.3     (floor at 3.9)
    //        6.3         5.8     5.7     (floor at 5.5)
    //
    // What that costs is the bottom of the range: this is now fast to very
    // fast rather than moderate to very fast.  A leslie at 4Hz is a real
    // thing but it is not what this voice sounds like at its best, and a
    // control whose bottom third is where the player lives is not a control.
    //
    // The floor is therefore a calibration to a player rather than a property
    // of a cabinet.  If the input level itself is what has moved, `level_full`
    // is the number that says so -- `run2-mac` prints the level while playing
    // for exactly this reason -- and it would fix the contact and the drive
    // along with the rotors.
    //
    // At every speed the two rates are deliberately not a ratio of small
    // integers, so the rotors drift through each other with a period of about
    // a second.  That beat is most of why a leslie does not sound like a
    // tremolo, and it survives the speed moving because both ends are set
    // that way.
    //
    // Measured off the render, against held tones at a fifth of full level
    // through to half again over it -- the horn rate recovered from the pitch
    // swing itself, so this is what the rotor actually did and not what it
    // was asked for:
    //
    //     breath   0.29  0.48  0.71  0.97  1.45   of level_full
    //     horn     6.39  7.00  7.69  8.49  8.59   Hz
    //     drum     5.32  5.80  6.37  7.01  7.10
    //
    // And over recordings/whistling.f32, which is a real performance rather
    // than a ladder: 5.5 to 8.3Hz, median 6.2, p95 8.1.  Attenuated 6dB, to
    // stand in for a player who has backed off because the PA is doing the
    // work, 5.5 to 7.5 with a median of 5.9 -- quieter playing now moves the
    // rotors less rather than parking them.
    .leslie_horn_soft_hz = 5.5f, .leslie_horn_loud_hz = 8.6f,
    .leslie_drum_soft_hz = 4.6f, .leslie_drum_loud_hz = 7.1f,
    // The rotors' mass.  Long enough that the speed follows the phrase rather
    // than the shape of each note -- the level swings 20dB inside one note
    // and a rotor that chased it would be a wobble on every one -- and short
    // enough to be a gesture: about two seconds to cross the range, which is
    // roughly what a real cabinet takes to come up to tremolo.
    .leslie_spin_s = 0.9f,
    // The crossover a real cabinet uses, and every partial is *shared* across
    // it rather than assigned to one rotor -- see the leslie block in
    // update_controls.  What that comes to, measured over held notes at every
    // rotor speed: the note itself swings 6.8-8.0 cents and the partials
    // above 1kHz swing 20-26 of the horn's 27.  The melody is steady and the
    // colour around it is turning, which is the right way round; a leslie
    // that swung the fundamental with it would be a vibrato.
    //
    // It also means the effect deepens as the line climbs, because more of
    // the voice is in the horn up there.  That happens in the room too.
    .leslie_crossover_hz = 800.0f,
    // 8.4dB peak to peak on the horn, 3.9 on the drum: a directional mouth
    // going round is not the same thing as sound spilling off a rotating
    // baffle, and the horn is the one that beams.  What reaches the listener
    // is the two of them crossing, which measures 5.7-8.9dB peak to peak on a
    // held note depending on where in the range it sits.
    .leslie_horn_am = 0.45f, .leslie_drum_am = 0.22f,
    // And the doppler that goes with it.  27 cents is what 13cm of radius at
    // 400rpm actually comes to; the drum's mouth does not move -- the baffle
    // in front of it does -- so it gets a fraction of that rather than the
    // same number scaled by its rate.
    .leslie_horn_cents = 27.0f, .leslie_drum_cents = 9.0f,
    // The valve amp.  A drawbar organ is nine sines and would be an entirely
    // clean sound without one; every record anybody thinks of when they think
    // of this instrument is that sound pushed into a preamp.  The bias is
    // what puts even harmonics in -- see drive_bias -- which here means the
    // saturator fills the gaps between drawbars rather than only reinforcing
    // the footages that are already there.
    //
    // Set by how much it fills them.  The products land on the same
    // half-partial grid the drawbars sit on, so they can be measured against
    // it.  The loudest of them sits at 1.75 times the note; over held tones
    // at a fifth of full level and at full, in dB under the note:
    //
    //     0.7 / 2.0 / 0.25   -20.6   -14.5
    //     0.6 / 1.5 / 0.20   -22.0   -16.7   <- this
    //     0.4 / 0.9 / 0.15   -26.1   -21.9
    //
    // A step louder turns a monophonic line into a fuzz voice; a step quieter
    // is clean enough that the amp stops being part of the sound.  The 5dB
    // between the two ends matters more here than on any other voice in the
    // table, because it is one of only two things the player's breath still
    // reaches -- the volume is not one of them.
    .drive_soft = 0.6f, .drive_loud = 1.5f, .drive_bias = 0.20f,
    // Unused by a drawbar voice, and set to what `fm` and `octaveless` set
    // them to for the same reason: they are divisors in a branch this voice
    // does not take.
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    // The cabinet.  A leslie is a horn driver and a wooden box; nine sine
    // partials running to 6.3kHz with a saturator on top of them is a
    // full-range signal, and no organ has ever sounded like one.
    //
    // It went in because of the speaker, which is the only place it could
    // have come from.  On headphones this voice was right; through the
    // K10.2 -- where the horn takes over around 2kHz and is at its most
    // forward -- it was harsh.  Measured over recordings/whistling.f32, in
    // octave bands relative to each voice's own total:
    //
    //              125    250    500     1k     2k     4k     8k
    //   bass       -7.0  -16.0  -31.2  -42.8  -58.1  -66.9  -72.9
    //   square     -8.8  -14.6  -24.1  -35.1  -48.3  -64.0  -70.5
    //   drawbar    -5.0   -4.4   -6.7  -11.9  -14.1  -21.6  -46.4  (before)
    //   drawbar    -4.8   -4.3   -6.6  -12.2  -18.7  -32.0  -64.2  (now)
    //
    // A treble voice belongs 30dB above the basses up there and that is not
    // the problem; 8kHz energy from an organ is.  3.5kHz takes 4.6dB off the
    // 2k octave, 10 off the 4k, and effectively all of the 8k, which is the
    // band the saturator was filling and nothing in the instrument ever did.
    .top_hz = 3500.0f,
    .level_full = 0.22f,
    // The key contact, at a tenth of full level -- 0.022 in absolute terms.
    // Above it the organ is at exactly one volume and the breath is spending
    // itself on the rotors instead.  What is below it is the scoop into a
    // note, the trail out of it, and the dips between tongued ones, which is
    // exactly the material that should still be moving.
    //
    // Measured over held tones across a five-to-one range of breath, the
    // rendered level moves 2.0dB with the contact and 10.6dB with it off.
    // The 2dB that is left is not the envelope -- it is the amp being driven
    // harder, which is a real organ getting dirtier rather than louder.
    //
    // It started at 0.20 and came down when the voice met a PA, which is the
    // second thing the speaker corrected about this preset and the one that
    // mattered more.  A player whistles more quietly into a room that is
    // already loud: measured off the meter `run2-mac` prints, quiet playing
    // there peaked at 0.0596, which is a note body of 0.028-0.042 -- the
    // whole of it *under* a contact sitting at 0.044.  So every quiet note
    // was on the square-law skirt, ducked and moving with each wobble of the
    // breath, which is the exact opposite of what this parameter is for.
    // Note-to-note rendered level over the recording attenuated to that
    // level, p10 to p90:
    //
    //     contact 0.20 (0.044)    9.0 dB
    //     contact 0.10 (0.022)    4.0 dB
    //     ...and 2.7dB over the recording at the level it was made at
    //
    // What it costs is the protection the knee gives against the detector
    // finding a note in room noise: a hop at 0.01 came out 26dB down and now
    // comes out 14dB down.  That is the trade, and quiet playing being an
    // organ is worth more than a quiet blip being 12dB quieter.
    //
    // This is a fraction of `level_full`, so the two move together: if that
    // is ever recalibrated the absolute contact moves with it, and it is the
    // absolute number -- comfortably under the player's quietest note body --
    // that has to stay true.
    .contact_level = 0.10f,
    // The fastest gate in the table, and it is reaching for the key click: a
    // Hammond's click is the contacts making at whatever phase the tonewheels
    // happen to be at, and a 2ms gate on nine free-running sines is that
    // mechanism exactly -- a broadband transient, because the partials arrive
    // in whatever phase relationship they were already in, and different on
    // every note for the same reason.
    //
    // How much of it survives being played by a whistle is another question,
    // and the measurement is worth having before believing the paragraph
    // above.  Over the onsets in prototypes/in-scale.f32 the rendered note
    // takes 8-13ms to go from a tenth of its level to nine tenths, against
    // `bass`'s 11-19: the gate is no longer what is holding it up, the
    // player's own attack is.  A real click would be inside 2ms.  What this
    // buys is the quickest onset the input allows, which is the most of one
    // this instrument can have.
    .attack_s = 0.002f, .release_s = 0.015f,
    .articulation_s = 0.012f, .glide_s = 0.005f,
    // Much less tail movement than the rest of the table, because the leslie
    // is already moving everything: the shimmer exists so that a sustained
    // note is not an organ note held still, and this voice's problem is the
    // opposite one.
    .shimmer_depth = 0.12f,
    // The table's usual match: equal LUFS through the K10.2 model, measured
    // over recordings/whistling.f32, which lands this at -23.1 against the
    // rest of the table's -22.4 to -23.1.  It reaches 0.27 peak doing it,
    // against the 0.82 `subbass` needs -- a voice whose lowest partial is
    // 69Hz does not spend headroom on excursion nobody hears.
    //
    // The match is worth 0.6dB less here than it is anywhere else, and it is
    // worth knowing why.  Every other voice's loudness follows the playing,
    // so matching the integral matches the whole curve; this one is flat, so
    // the integral is the only thing there is to match.  On a ladder of notes
    // all played at one level it therefore measures about 3dB under the rest
    // of the table -- the same voice on material with no dynamics in it,
    // where the others are all at their loudest and this one is where it
    // always is.  Over a real performance they land together, which is the
    // thing that has to be true when the player changes voice mid-set.
    //
    // Across pitch it is among the flattest things in the table: 1.0dB from
    // the bottom of the whistle to the top, against `square`'s 3.1.  That is
    // not the audibility compensation working hard, it is a voice with
    // nothing under the cabinet's corner never asking it for anything.  The
    // 1.0 is `top_hz`, which takes more off a high note than a low one.
    //
    // 0.242 matched, two and a half volume steps down: see the offsets in the
    // header comment.  A drawbar registration through a leslie is a sustained
    // pad, and a pad matched to the same LUFS as a plucked bass line is a pad
    // that never gets out of the way.
    .out_gain = 0.088f,
  },
  {
    // The same organ an octave up: a 550-3150Hz whistle lands at 275-1575Hz
    // against `drawbar`'s 137-787.  One octave down rather than two, which is
    // an upper manual against a lower one -- where a solo is played rather
    // than where a left hand comps.
    //
    // Everything not listed here is `drawbar`'s, deliberately, and the two
    // numbers that are not listed are the whole point of the voice: the
    // crossover and the cabinet do not move.  They are properties of the box
    // the sound comes out of, and a box does not transpose when the organist
    // plays higher up the manual.  So this is not `drawbar` shifted -- it is
    // the same instrument played an octave up *through the same leslie*, and
    // what falls out of that is the difference between the two voices:
    //
    //   - The melody climbs into the horn.  The split is half and half at
    //     800Hz, so a note at the bottom of this range is the drum's and a
    //     note at the top is the horn's.  Measured over the ladder, the
    //     fundamental swings 6.9 cents at 330Hz and 23.3 at 1320, against
    //     `drawbar`'s 6.8 and 6.2 over its own range -- that voice never
    //     leaves the drum and this one crosses.  High organ notes through a
    //     leslie really do warble, and this is the register where it happens.
    //   - The upper drawbars run off the top of the cabinet.  The 1' sits at
    //     eight times the note, which up here is 2.2-12.6kHz against a 3.5kHz
    //     corner, so it is 22dB down at the top of the range and audible only
    //     at the bottom of it.  The registration is unchanged and the voice
    //     is darker for its register anyway, which is what happens when you
    //     play a real one high: the box stops helping.  In octave bands it is
    //     `drawbar` shifted up one band below 1kHz and progressively less
    //     than that above -- 1.4dB short at 2k, 3.8 at 4k, 6.9 at 8k.
    //
    // In absolute terms it still puts more into 2-4kHz than `drawbar` does
    // (-14.0 and -23.1 against -19.4 and -32.4), because an octave up is an
    // octave up.  That is the band a PA horn is most forward in, so if this
    // one is harsh where `drawbar` is not, the registration is where to look
    // and not the cabinet -- the cabinet is the thing that must not move.
    .name = "drawbar-hi",
    .octave = 0.5f,
    .drawbars = { 8, 6, 8, 6, 4, 3, 2, 2, 2 },
    .harmonics = 16,
    .unison = 1, .detune_cents = 0.0f,
    .leslie_horn_soft_hz = 5.5f, .leslie_horn_loud_hz = 8.6f,
    .leslie_drum_soft_hz = 4.6f, .leslie_drum_loud_hz = 7.1f,
    .leslie_spin_s = 0.9f,
    .leslie_crossover_hz = 800.0f,
    .leslie_horn_am = 0.45f, .leslie_drum_am = 0.22f,
    .leslie_horn_cents = 27.0f, .leslie_drum_cents = 9.0f,
    .drive_soft = 0.6f, .drive_loud = 1.5f, .drive_bias = 0.20f,
    .cutoff_soft = 1.0f, .cutoff_loud = 1.0f, .rolloff_exp = 2.0f,
    .top_hz = 3500.0f,
    .level_full = 0.22f,
    .contact_level = 0.10f,
    .attack_s = 0.002f, .release_s = 0.015f,
    .articulation_s = 0.012f, .glide_s = 0.005f,
    .shimmer_depth = 0.12f,
    // 0.245 matched, two and a half volume steps down, for the reason
    // `drawbar` is -- and it has to be the same two and a half, because these
    // two are each other's manuals and a player changing between them
    // mid-phrase must not hear a step.
    .out_gain = 0.089f,
  },
};

#define N_PRESETS ((int)(sizeof(presets)/sizeof(presets[0])))

int synth_preset_count(void) {
  return N_PRESETS;
}

const char* synth_preset_name(int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    return "?";
  }
  return presets[preset].name;
}

// Where partial n actually sits, as a multiple of the note being played.  Two
// voices are not a harmonic series: the octave stack's partial n is the nth
// octave, and a drawbar voice counts in half-partials, because its 16'
// drawbar sounds an octave *below* the note and its 5 1/3' a fifth above it.
// The note itself is then partial 2, and the nine drawbars sit at 1, 3, 2, 4,
// 6, 8, 10, 12 and 16.
static float synth_partial_ratio(const struct Synth* s, int n) {
  const struct SynthParams* p = s->params;
  if (s->drawbar) {
    return 0.5f * n;
  }
  if (p->octave_stack_hz > 0) {
    return exp2f((float)(n - 1));
  }
  return n * (1 + p->stretch * (n - 1));
}

// Whether the partials are far enough off the harmonic series that the
// Chebyshev recurrence can't reach them and each one needs its own phase.
// Half-partials are exactly that: the recurrence walks 1, 2, 3... of whatever
// the oscillator is at, and there is no oscillator here whose multiples are
// 0.5, 1, 1.5.  A drawbar voice pays for its own phases anyway, since the
// leslie moves the horn's partials and the drum's by different amounts.
static bool synth_needs_partial_phase(const struct Synth* s) {
  return s->drawbar || s->params->stretch > 0 ||
         s->params->octave_stack_hz > 0;
}

// Which partial of the half-note series each drawbar sounds at, in the order
// they sit on the instrument: 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3',
// 1'.  The second one out of order is not a mistake -- the 5 1/3' drawbar is
// to the left of the 8' on a Hammond, and generations of players have the
// registrations memorised in that order.
static const int synth_drawbar_partial[SYNTH_DRAWBARS] = {
  1, 3, 2, 4, 6, 8, 10, 12, 16
};

// White noise in -1..1.  Xorshift rather than an LCG, and not for the usual
// reasons: the low bits of a power-of-two LCG cycle with period 2, 4, 8...,
// so taking the whole word puts a periodic component *in* the noise, and
// through a band that emphasizes nothing in particular it is audible as a
// buzz rather than as air.  Xorshift's bits are all equally good.
static float synth_noise(struct Synth* s) {
  uint32_t x = s->noise_state ? s->noise_state : 0x9e3779b9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->noise_state = x;
  return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

// One-pole coefficient reaching ~63% of the way in `seconds`.
static float coeff(float seconds, float sample_rate) {
  if (seconds <= 0) {
    return 1;
  }
  return 1 - expf(-1.0f / (seconds * sample_rate));
}

// The pitch actually being played: the note, plus whatever the per-note drop
// and the vibrato are adding to it right now.  In log2 Hz, like log_freq.
static float synth_pitch_log(const struct Synth* s) {
  const struct SynthParams* p = s->params;
  float v = s->log_freq + s->drop;
  if (p->vibrato_cents > 0) {
    float onset = p->vibrato_onset_s > 0
        ? fminf(1, s->note_age / p->vibrato_onset_s) : 1;
    v += onset * (p->vibrato_cents / 1200.0f) *
         sinf(2 * (float)M_PI * s->vibrato_pos);
  }
  return v;
}

// What counts as full blow: the player's number if they have set one, and
// the voice's own if they have not.  Everything that measures the input
// against a level goes through here.
static float synth_level_full(const struct Synth* s) {
  return s->player_level_full > 0 ? s->player_level_full
                                  : s->params->level_full;
}

// Level mapped to the 0..1 the envelope runs on.  A slight compression, no
// more, or the voice stops responding to how hard it is being pushed.
//
// Unless the voice has a key contact, in which case it is not a curve at all
// but a switch with a knee under it: full above the contact however hard the
// player blows, and falling as the square below so that what separates two
// tongued notes still separates them.  See `contact_level`.
static float synth_loudness_of(const struct Synth* s, float level) {
  const struct SynthParams* p = s->params;
  float full = synth_level_full(s);
  if (p->contact_level > 0) {
    float u = fmaxf(0, level) / (p->contact_level * full);
    return fminf(1.0f, u * u);
  }
  return fminf(1.0f, powf(fmaxf(0, level / full), 0.8f));
}

// How much a partial at this frequency contributes to what a listener hears,
// as a power weight: the cabinet's response times the ear's.
//
// This is what makes a voice hold its loudness across the range.  Weighting
// every partial equally holds the *electrical* power constant, which is not
// the same thing at all once the note is low enough that the cabinet has
// stopped reproducing its fundamental: measured over a ladder of notes at
// identical input level, `square` was 12.1dB louder at the top of the whistle
// range than at the bottom and `eight-oh-eight` 13.9dB.  Weighting by
// audibility instead means a note whose fundamental has fallen off the bottom
// is normalised on the harmonics that are actually carrying it, which is also
// what a listener is hearing it by.
//
// Analytic rather than the digital filters BS.1770 specifies, so that it does
// not depend on the sample rate: a 24dB/octave cabinet corner, the RLB
// high-pass, and the 4dB shelf.  Checked against a real BS.1770 meter over
// the whole whistle range; see the loudness section of prototypes/README.md.
static float synth_audibility(float hz) {
  if (!(hz > 1)) {
    return 0;
  }
  // The cabinet: flat at and above 55Hz, a ported box's cliff below it.
  float u = hz / 55.0f;
  float u4 = u * u * u * u;
  float u8 = u4 * u4;
  float cab = u8 / (1 + u8);
  // The ear, as BS.1770 weights it: a high-pass and a shelf, with the corners
  // and the order fitted to the standard's own digital filters rather than
  // guessed.  Over 20-5000Hz this tracks them to 0.66dB worst case and 0.26dB
  // rms, which matters -- the first version of this was eyeballed and its
  // 2.2dB error at 41Hz came straight back out as loudness that still moved
  // with the pitch.
  float hp = powf(hz / 53.2f, 2.90f);
  hp = hp / (1 + hp);
  float x = hz / 2957.0f;
  float x2 = x * x;
  float shelf = (1 + 4.0f * x2) / (1 + x2);
  // Scaled so a partial at 1kHz weighs exactly 1, which is only so that the
  // compensation reads as a number near 1 rather than an arbitrary one.
  return cab * hp * shelf * (1.0f / 1.3071f);
}

// J_n(x) squared for n = 0..n_max, by the power series with a running term.
// The FM voices keep their index under 5, so this converges in a handful of
// terms and needs no library beyond multiplication.
static void synth_bessel_sq(float x, float* out, int n_max) {
  float h = 0.5f * x;
  for (int n = 0; n <= n_max; n++) {
    float t = 1;
    for (int k = 1; k <= n; k++) {
      t *= h / (float)k;
    }
    float sum = t;
    for (int k = 1; k < 24; k++) {
      t *= -(h * h) / ((float)k * (float)(n + k));
      sum += t;
      if (fabsf(t) < 1e-9f) {
        break;
      }
    }
    out[n] = sum * sum;
  }
}

static float atan_norm(float v) {
  return atanf(v) / (float)(M_PI / 2);
}

// The nearest equal-tempered semitone to a log2 Hz pitch, also in log2 Hz.
static float synth_snap_log(float log_hz) {
  float ref = log2f(SYNTH_HOLD_SNAP_HZ);
  return ref + floorf((log_hz - ref) * 12 + 0.5f) / 12.0f;
}

// How far below the whistle this voice is playing: the preset's own octave,
// times whatever the fifth control is asking for.  Everything that turns a
// played pitch into a frequency goes through here.
static float synth_octave(const struct Synth* s) {
  return s->params->octave * s->octave_mul;
}

// What sits between the pitch this voice is playing and the note a listener
// hears, in log2 Hz -- split into the part that is musical and the part that
// is an artifact of how the voice is built.  The snap has to see through both,
// and it uses them differently.
//
// Musical: the octave, and the fifth when it is on.  An octave is a power of
// two and lands on the same grid either way, but 2/3 is 2.0 cents off the
// tempered fifth, so under the fifth this is real.
static float synth_musical_offset(const struct Synth* s) {
  return log2f(synth_octave(s));
}

// Artifact: the detune, when `mono_partials` takes the bottom partials from
// one copy rather than from all of them.  That copy is the flat one by
// construction, so `reese` sounds 9 cents under what it is playing.  At 73Hz
// that is a beat every 2.6 seconds against a fretted instrument, two of them
// inside one sustain -- and nothing else in the synth cares, because nothing
// else claims to be in tune with anybody.
static float synth_artifact_offset(const struct Synth* s) {
  return s->params->mono_partials > 0 ? log2f(s->detune[0]) : 0.0f;
}

void synth_preset_defaults(int preset, struct SynthParams* out) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  *out = presets[preset];
}

void synth_sanitize_params(struct SynthParams* p) {
  // level_full is a divisor, and octave multiplies every partial's frequency.
  if (!(p->level_full > 1e-4f)) {
    p->level_full = 1e-4f;
  }
  if (!(p->octave > 1e-4f)) {
    p->octave = 1e-4f;
  }
  if (p->unison < 1) {
    p->unison = 1;
  }
  if (p->unison > SYNTH_UNISON) {
    p->unison = SYNTH_UNISON;
  }
  if (p->harmonics < 1) {
    p->harmonics = 1;
  }
  if (p->harmonics > SYNTH_MAX_HARMONICS) {
    p->harmonics = SYNTH_MAX_HARMONICS;
  }
  // powf(n, tilt) and powf(n/cutoff, rolloff_exp) both want a positive base
  // and a finite exponent; cutoff_* are divisors.
  if (!(p->cutoff_soft > 1e-3f)) {
    p->cutoff_soft = 1e-3f;
  }
  if (!(p->cutoff_loud > 1e-3f)) {
    p->cutoff_loud = 1e-3f;
  }
  if (!(p->rolloff_exp > 0)) {
    p->rolloff_exp = 1;
  }
  if (p->growl_onset_s <= 0) {
    p->growl_onset_s = 1e-3f;
  }
  if (p->min_partial_hz < 0) {
    p->min_partial_hz = 0;
  }
  if (!(p->stereo_width >= 0)) {
    p->stereo_width = 0;
  }
  if (p->stereo_width > 1) {
    p->stereo_width = 1;
  }
  // Both widths are divisors, and only mean anything when the thing they
  // shape is switched on.
  if (p->octave_stack_hz > 0 && !(p->octave_stack_width > 1e-3f)) {
    p->octave_stack_width = 1;
  }
  if (p->resonance > 0 && !(p->resonance_width > 1e-3f)) {
    p->resonance_width = 0.25f;
  }
  if (!(p->breath >= 0)) {
    p->breath = 0;
  }
  if (p->mono_partials < 0) {
    p->mono_partials = 0;
  }
  if (p->mono_partials > SYNTH_MAX_HARMONICS) {
    p->mono_partials = SYNTH_MAX_HARMONICS;
  }
  // A hold is a countdown, so a negative one has to read as "no hold" rather
  // than as a countdown that never expires.
  if (!(p->sustain_level >= 0)) {
    p->sustain_level = 0;
  }
  if (p->sustain_level > 1) {
    p->sustain_level = 1;
  }
  // Both are divisors or multipliers that only mean anything when the thing
  // they belong to is switched on.
  if ((p->fm_index_soft > 0 || p->fm_index_loud > 0) && !(p->fm_ratio > 0)) {
    p->fm_ratio = 1;
  }
  if (p->octave_stack_track > 0 && !(p->octave_stack_ref_hz > 1)) {
    p->octave_stack_ref_hz = 1000;
  }
  // A drawbar is a nine-position slider and nothing else; anything outside
  // 0-8 is an edit that got away rather than a louder organ.
  for (int d = 0; d < SYNTH_DRAWBARS; d++) {
    if (!(p->drawbars[d] >= 0)) {
      p->drawbars[d] = 0;
    }
    if (p->drawbars[d] > 8) {
      p->drawbars[d] = 8;
    }
  }
  // A rotor swinging deeper than 1 would take a partial through zero and out
  // the other side, which is a partial inverting rather than a speaker
  // pointing away.  The crossover is a comparison and the drum is the side a
  // zero would put everything on, so it has to be a real frequency.
  if (p->leslie_horn_soft_hz > 0) {
    if (!(p->leslie_crossover_hz > 0)) {
      p->leslie_crossover_hz = 800;
    }
    if (!(p->leslie_horn_am >= 0)) {
      p->leslie_horn_am = 0;
    }
    if (p->leslie_horn_am > 1) {
      p->leslie_horn_am = 1;
    }
    if (!(p->leslie_drum_am >= 0)) {
      p->leslie_drum_am = 0;
    }
    if (p->leslie_drum_am > 1) {
      p->leslie_drum_am = 1;
    }
    if (!(p->leslie_horn_cents >= 0)) {
      p->leslie_horn_cents = 0;
    }
    if (!(p->leslie_drum_cents >= 0)) {
      p->leslie_drum_cents = 0;
    }
    // Rates are frequencies and the interpolation between them runs on a
    // 0-to-1 speed, so a negative one would wind a rotor backwards.
    if (!(p->leslie_horn_loud_hz >= 0)) {
      p->leslie_horn_loud_hz = p->leslie_horn_soft_hz;
    }
    if (!(p->leslie_drum_soft_hz >= 0)) {
      p->leslie_drum_soft_hz = 0;
    }
    if (!(p->leslie_drum_loud_hz >= 0)) {
      p->leslie_drum_loud_hz = p->leslie_drum_soft_hz;
    }
    // A rotor with no mass at all is a rate that steps with the level, so
    // this is clamped to something rather than allowed to be nothing.
    if (!(p->leslie_spin_s > 0.01f)) {
      p->leslie_spin_s = 0.01f;
    }
  }
  // Above the contact the voice is at full, so a contact at zero level would
  // put it at full on silence.
  if (p->contact_level < 0) {
    p->contact_level = 0;
  }
  if (p->top_hz < 0) {
    p->top_hz = 0;
  }
}

void synth_set_params(struct Synth* s, const struct SynthParams* params) {
  s->params = params;

  int unison = params->unison;
  if (unison < 1) {
    unison = 1;
  }
  if (unison > SYNTH_UNISON) {
    unison = SYNTH_UNISON;
  }
  s->unison = unison;

  // Spread symmetrically about the centre voice, in tuning and in position:
  // the copy that is flat is the one on the left.  Symmetric means the pans
  // sum to zero, which is what keeps the side signal from leaking a copy of
  // the mid into the image.
  for (int u = 0; u < unison; u++) {
    float spread = unison > 1
      ? (u / (float)(unison - 1)) * 2 - 1   // -1..1
      : 0;
    s->detune[u] = exp2f(spread * params->detune_cents / 1200.0f);
    s->pan[u] = spread;
    // The first copy carries the low partials alone, at the level the rest
    // get from `unison` copies summing in power.
    s->low_gain[u] = u == 0 ? sqrtf((float)unison) : 0.0f;
  }

  // The drawbars, resolved into the one thing the partial loop wants: an
  // amplitude for every partial of the half-note series, zero at the ten it
  // has no drawbar for.  A step is 3dB -- 2^(1/2) in amplitude -- with 8 at
  // unity and 0 silent rather than 24dB down, because a drawbar pushed all
  // the way in is a disconnected contact and not a quiet one.
  s->drawbar = false;
  for (int i = 0; i < SYNTH_MAX_HARMONICS; i++) {
    s->drawbar_amp[i] = 0;
  }
  for (int d = 0; d < SYNTH_DRAWBARS; d++) {
    float v = params->drawbars[d];
    if (!(v > 0)) {
      continue;
    }
    s->drawbar = true;
    s->drawbar_amp[synth_drawbar_partial[d] - 1] =
      exp2f(0.5f * (fminf(8.0f, v) - 8.0f));
  }
}

void synth_set_preset(struct Synth* s, int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  synth_set_params(s, &presets[preset]);
}

// The two pitch controls, multiplied into the one number everything reads.
static void synth_update_octave_mul(struct Synth* s) {
  // 2/3 exactly, not exp2f(-7.02/12).  See synth.h.
  s->octave_mul = (s->fifth ? 2.0f / 3.0f : 1.0f) *
                  exp2f((float)s->octave_shift);
}

void synth_set_fifth(struct Synth* s, bool on) {
  s->fifth = on;
  synth_update_octave_mul(s);
}

void synth_set_level_full(struct Synth* s, float level) {
  // The same floor synth_sanitize_params puts on the preset's own: this is a
  // divisor on the audio thread.
  s->player_level_full = level > 1e-4f ? level : 0;
}

void synth_set_octave_shift(struct Synth* s, int octaves) {
  if (octaves > SYNTH_OCTAVE_SHIFT) {
    octaves = SYNTH_OCTAVE_SHIFT;
  }
  if (octaves < -SYNTH_OCTAVE_SHIFT) {
    octaves = -SYNTH_OCTAVE_SHIFT;
  }
  s->octave_shift = octaves;
  synth_update_octave_mul(s);
}

void synth_set_sustain(struct Synth* s, bool on) {
  s->sustain = on;
}

void synth_init(struct Synth* s, float sample_rate, int preset) {
  memset(s, 0, sizeof(*s));
  s->sample_rate = sample_rate;
  synth_update_octave_mul(s);   // both controls off: exactly 1
  s->log_freq = log2f(440.0f);
  s->control_countdown = 0;
  // All together: detune pulls them apart within a second, and starting them
  // deliberately spread would mean starting partly cancelled.
  for (int u = 0; u < SYNTH_UNISON; u++) {
    s->phase[u] = 0;
  }
  // Not zero: a plucked voice whose envelope hasn't been armed yet should be
  // at full, not silent, or the very first note is missing its attack.
  s->pluck = 1;
  s->audibility_comp = 1;
  synth_set_preset(s, preset);
}

// Recomputes timbre and drive.  Called at SYNTH_CONTROL_HOP, not per sample.
// Whether a note has been held long enough to have earned a tail.  Read in
// two places, so it lives here rather than being recomputed.
static bool synth_earned(const struct Synth* s) {
  return s->sustain && !s->params->no_sustain &&
         s->note_playing >= SYNTH_HOLD_MIN_NOTE_S;
}

static void update_controls(struct Synth* s, bool playing) {
  const struct SynthParams* p = s->params;

  float reach = fmaxf(0, s->level / synth_level_full(s));

  // Loudness stays close to what was played -- a slight compression, no more,
  // or the lead stops responding to how hard it's being pushed.  It is frozen
  // once the note is released, because from there the gate does the fading
  // and having both fall would halve the release time.
  //
  // Sustaining is the exception, because the level does not fall once
  // the player stops -- it eases onto the note's own average and stays there,
  // so there is nothing here to double up with the gate.
  if (playing || synth_earned(s)) {
    s->loudness = synth_loudness_of(s, s->level);
  }

  // Brightness and drive get a soft knee instead: they should keep responding
  // when the player leans past what we called full, and a mis-set input level
  // should cost expression rather than usability.  Unlike loudness this keeps
  // following the level as it falls away, so the tail of a note darkens
  // rather than buzzing at full brightness the whole way down.
  float dynamics = 1 - expf(-2.2f * reach);
  s->dynamics = dynamics;

  // What the leslie's rotors are being asked for, frozen the moment the
  // player stops so that a gap between phrases does not read as a request to
  // slow down.  See `leslie_target`.
  //
  // The level straight, not the soft-kneed `dynamics` the drive and the
  // cutoff run on.  That knee exists so that a mis-set input level costs
  // expression rather than usability, and it is the wrong shape here: it is
  // already at 0.47 by the quietest breath this voice will sound at, so the
  // whole bottom half of the rotors' range would be unreachable and the
  // control would run from fast to very fast.  Measured over
  // recordings/whistling.f32, straight level puts the median note at 5.5Hz
  // and the loud end at 8.6, which is the moderate-to-very-fast this is for.
  if (playing) {
    s->leslie_target = fminf(1.0f, reach);
  }

  // The depth is simply the settle, which is zero whenever a note is being
  // played or no tail was earned -- that is what keeps this out of the voice.
  // How the movement *arrives* is not this: it is the LFOs accelerating from
  // rest, which they do over SYNTH_TAIL_SHIMMER_S.
  float fade = s->settle * s->settle * (3 - 2 * s->settle);
  float shimmer = fade * (p->shimmer_depth > 0 ? p->shimmer_depth
                                               : SYNTH_TAIL_SHIMMER);
  for (int g = 0; g < SYNTH_SHIMMER_LFOS; g++) {
    s->shimmer_gain[g] = sinf(2 * (float)M_PI * s->shimmer_pos[g]);
  }

  // How long they have been on this note.  Note length rather than level,
  // because these want to do different jobs: brightness has to arrive on
  // every note of a run, and the growl must not.
  float sustain = fminf(1, s->note_age / p->growl_onset_s);

  float width = p->pwm_center +
    p->pwm_slow_depth * sinf(2 * (float)M_PI * s->pwm_pos) +
    p->growl_depth * sustain * sinf(2 * (float)M_PI * s->growl_pos);
  width = fmaxf(0.06f, fminf(0.5f, width));

  float cutoff = p->cutoff_soft + (p->cutoff_loud - p->cutoff_soft) * dynamics;
  // Both cutoff modulations are in octaves and multiply, so a wobble stays
  // the same wobble wherever the dynamics and the note envelope have put the
  // filter.  cutoff_env is what the note has left of its opening sweep.
  if (p->wobble_octaves > 0) {
    cutoff *= exp2f(p->wobble_octaves * sinf(2 * (float)M_PI * s->wobble_pos));
  }
  if (p->cutoff_env_octaves > 0) {
    cutoff *= exp2f(s->cutoff_env);
  }
  s->drive = p->drive_soft + (p->drive_loud - p->drive_soft) * dynamics;
  s->fm_index =
    p->fm_index_soft + (p->fm_index_loud - p->fm_index_soft) * dynamics;
  // The FM voices have no partial amplitudes to move, so their tail moves on
  // the index; see SYNTH_TAIL_SHIMMER_FM.
  s->fm_index *= 1 + fade * SYNTH_TAIL_SHIMMER_FM * s->shimmer_gain[0];

  // Stop before Nyquist rather than aliasing back down.  The highest unison
  // voice is the one that runs out of room first.
  float f0 = exp2f(synth_pitch_log(s)) * synth_octave(s) *
             s->detune[s->unison - 1];
  int active = p->harmonics;
  if (active > SYNTH_MAX_HARMONICS) {
    active = SYNTH_MAX_HARMONICS;
  }
  while (active > 1 &&
         f0 * synth_partial_ratio(s, active) > s->sample_rate * 0.45f) {
    active--;
  }
  s->harmonics_active = active;

  float base = exp2f(synth_pitch_log(s)) * synth_octave(s);

  // Drop partials too low for anything to reproduce.
  int lowest = 1;
  if (p->min_partial_hz > 0) {
    while (lowest <= active &&
           base * synth_partial_ratio(s, lowest) < p->min_partial_hz) {
      lowest++;
    }
  }

  float power = 0;
  float heard = 0;
  for (int i = 0; i < lowest - 1 && i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_target[i] = 0;
  }
  for (int i = lowest - 1; i < active; i++) {
    int n = i + 1;
    float amp;
    if (s->drawbar) {
      // Nine sines at fixed footages and nothing else: no width, no tilt, no
      // cutoff and no dynamics.  A tonewheel organ has no filter in it, and
      // the only thing a player can do to the timbre while playing is pull a
      // drawbar -- so what a whistle's dynamics reach here is the level and
      // the valve amp downstream, which is exactly what an expression pedal
      // reaches on the real instrument.
      amp = s->drawbar_amp[i];
    } else if (p->octave_stack_hz > 0) {
      // A bell fixed in Hz, not in partial number.  That is the whole trick:
      // the components move under a weighting that doesn't, so an octave of
      // played pitch slides the stack one slot and lands back on itself.
      float hz = base * synth_partial_ratio(s, n);
      float bell = p->octave_stack_hz;
      if (p->octave_stack_track > 0) {
        // Moves `track` octaves for every octave of played pitch.  At 0 this
        // is the fixed bell that makes the voice octaveless; anything above
        // trades some of that for melodic contour.
        bell *= exp2f(p->octave_stack_track *
                      (synth_pitch_log(s) - log2f(p->octave_stack_ref_hz)));
      }
      float x = log2f(hz / bell) / p->octave_stack_width;
      amp = expf(-0.5f * x * x);
    } else {
      float rolloff = 1 / (1 + powf(n / cutoff, p->rolloff_exp));
      if (p->resonance > 0) {
        // A bump at the cutoff, on top of the rolloff rather than replacing
        // it, so the peak rides the same sweep the corner does.
        float d = log2f(n / cutoff) / p->resonance_width;
        rolloff *= 1 + p->resonance * expf(-0.5f * d * d);
      }
      // Negative is meaningful and wanted: past the first null the pulse
      // series flips sign, and that is part of the PWM sound.
      amp = sinf(n * (float)M_PI * width) / powf((float)n, p->tilt) * rolloff;
    }
    // The tail's movement, before the normalisation so the level does not
    // follow it.  See SYNTH_TAIL_SHIMMER.
    if (shimmer > 0) {
      amp *= 1 + shimmer * s->shimmer_gain[i % SYNTH_SHIMMER_LFOS];
    }
    s->harmonic_target[i] = amp;
    power += amp * amp;
    float hz = base * synth_partial_ratio(s, n);
    // What the listener gets of this partial, which is not only the room and
    // the ear: a voice that models the box it comes out of has already
    // thrown some of it away.  `top_hz` is applied after the saturator and so
    // cannot be part of the partial amplitudes, but leaving it out of the
    // weighting here means the compensation holds the *unfiltered* level
    // steady and the filtered one tilts -- which is audible as a voice
    // getting quieter as it climbs, because the whole spectrum is moving up
    // against a corner that is not moving.  Two one-poles, in power.
    float weight = synth_audibility(hz);
    if (p->top_hz > 0) {
      float t = hz / p->top_hz;
      float lp = 1 / (1 + t * t);
      weight *= lp * lp;
    }
    heard += amp * amp * weight;
  }
  for (int i = active; i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_target[i] = 0;
  }

  // Hold the level steady as the timbre moves, so the only thing changing how
  // loud this is is how hard the player is blowing.  Unison voices are
  // mutually detuned and so sum in power, not amplitude.
  //
  // Plain power, not audibility-weighted, and deliberately: this is what
  // feeds the saturator, and a drive that sees a different level at every
  // pitch is a different voice at every pitch.  Making the *heard* level
  // steady is a separate job, done after the saturator -- see
  // audibility_comp below.
  float norm = sqrtf(power * s->unison * 0.5f);
  if (norm < 1e-6f) {
    norm = 1e-6f;
  }
  for (int i = lowest - 1; i < active; i++) {
    s->harmonic_target[i] *= 0.7f / norm;
  }

  // The leslie's amplitude half, and the one place in this file where a
  // modulation deliberately goes *after* the normalisation rather than
  // before it.  The tail shimmer is applied before, so that the balance
  // between partials moves and the level does not; a leslie is the opposite
  // instruction -- the level moving is the whole effect, and a speaker
  // pointing away from you really is quieter.  Normalising it away would
  // leave a rotating timbre and no rotation.
  //
  // Split at the box's crossover, so the horn's partials and the drum's swing
  // at their own rates and drift through each other, which is what stops a
  // leslie sounding like one tremolo.  It is also why this is worth doing per
  // partial rather than on the output.
  if (p->leslie_horn_soft_hz > 0) {
    float horn = 1 + p->leslie_horn_am * sinf(2 * (float)M_PI * s->leslie_horn_pos);
    float drum = 1 + p->leslie_drum_am * sinf(2 * (float)M_PI * s->leslie_drum_pos);
    for (int i = 0; i < active; i++) {
      // How much of this partial comes out of the horn: a 12dB-an-octave
      // split at the crossover, half and half in power where it sits, rather
      // than one rotor or the other.
      //
      // A hard assignment is what a crossover is not, and it would be
      // audible: a partial parked near 800Hz would jump between a rotor 1.45
      // times its level and one 0.78 times it every time the player's pitch
      // wandered a cent across the line, which is a flutter on that partial
      // and nothing a leslie does.  Sharing it is also what happens in the
      // box -- near the crossover the note comes out of both speakers.
      //
      // Steep rather than gentle, because the two ends of it are doing
      // different jobs.  On a 6dB slope the horn still has a third of the
      // note itself at the bottom of the range, which swings the fundamental
      // 27 cents and reads as vibrato on the melody; at 12dB the note is the
      // drum's, the upper drawbars are the horn's, and only the partials
      // actually near 800Hz are shared.
      float x = base * synth_partial_ratio(s, i + 1) / p->leslie_crossover_hz;
      float x2 = x * x;
      float w = x2 * x2 / (1 + x2 * x2);
      s->leslie_mix[i] = w;
      s->harmonic_target[i] *= drum + w * (horn - drum);
    }
  }

  // And how far under that the listener actually hears it.  A note low enough
  // that the cabinet has given up on its fundamental is carried by harmonics
  // that are worth less than the electrical power says, and this is the gain
  // that puts it back.  Applied after the saturator, because applying it
  // before just drives the saturator harder and it hands most of it back:
  // tried that way first, and `square` moved 12.1dB to 11.7dB.
  //
  // Capped, because the bottom of the range is a place where a few dB of
  // "heard" costs a great deal of excursion, and an uncapped inverse of a
  // 24dB/octave cliff runs away.
  if (p->fm_index_loud > 0 || p->fm_index_soft > 0) {
    // The FM voices are not the sum above -- they have one partial in it and
    // a whole spectrum in the oscillator -- so their audibility is worked out
    // from where the sidebands actually are.  A two-operator FM signal has
    // constant envelope, so its total power is 1 whatever the index does, and
    // all the index moves is where that power sits: J_n(index)^2 of it at the
    // carrier plus n modulators.  That is the entire reason this voice needs
    // its own case, and also why the answer is exact rather than fitted.
    float sq[SYNTH_FM_TERMS];
    synth_bessel_sq(s->fm_index, sq, SYNTH_FM_TERMS - 1);
    float fmod = base * p->fm_ratio;
    heard = sq[0] * synth_audibility(base);
    for (int n = 1; n < SYNTH_FM_TERMS; n++) {
      heard += sq[n] * (synth_audibility(base + n * fmod) +
                        synth_audibility(fabsf(base - n * fmod)));
    }
    power = 1;
  }

  if (heard > 1e-12f) {
    s->audibility_comp = fminf(SYNTH_AUDIBILITY_MAX, sqrtf(power / heard));
  } else {
    s->audibility_comp = 1;
  }
}

float synth_process(struct Synth* s, const struct PitchHint* hint) {
  const struct SynthParams* p = s->params;

  // Whether the player is still on this note.  This is the detector's call and
  // nothing else's, for every preset including this one: while there is input
  // the voice follows it, and the tail is only ever about what happens when
  // the input stops.
  //
  // An earlier version of this voice second-guessed `voiced` here, on the
  // grounds that a whistle trails off and the detector hangs on through the
  // trail-off.  That much is true -- and it is what the level average below
  // is for -- but it is a
  // question about what to *hold*, not about whether the player has stopped,
  // and answering it here meant the voice could decide a note was over while
  // it was still being whistled.  It did, routinely: on a note with a hard
  // attack, whose body sits 6dB under its own first 50ms, it gave up 167ms in
  // and the bass dropped out from under a note the player was still on.  What
  // to hold is settled by the weighted average below and by the fall time
  // above, both of which discount a trail-off without ever refusing to follow
  // one.
  //
  // An onset is the one event that resets everything at once -- the level,
  // the timbre and the running pitch average -- so a holding voice, which is
  // sounding when they arrive rather than silent, has to be sure it is a
  // note.  One that comes in at nothing against what is already being held is
  // the detector re-triggering on the noise floor of a rest: see
  // SYNTH_HOLD_ONSET_FLOOR.  Ignoring it leaves the tail exactly as it was.
  // Whether a tail is sounding right now, which is the state everything below
  // keys off.  Not simply "the control is on": a note too short to have been
  // meant as a held one never earns a tail, and with nothing ringing there is
  // nothing here to protect -- so the voice behaves exactly as it does with
  // the control off.  That is what makes this a switch rather than a mode.
  bool holding = synth_earned(s) && s->gate > 0.01f;

  bool onset = hint->onset;
  if (holding && onset && hint->level < SYNTH_HOLD_ONSET_FLOOR * s->level) {
    onset = false;
  }

  // Not on the onset sample, where `level` still belongs to the previous note.
  bool playing = hint->voiced;

  // How long the player has stayed on this note.  A tail is earned, not
  // given: this is the one question a tail raises that a voice which simply
  // follows the whistle never has to ask.  Held once, it stays true for the
  // rest of the note, so a note that qualified and is now trailing off still
  // gets one.
  if (onset) {
    s->note_playing = 0;
  }
  if (playing) {
    s->note_playing += 1.0f / s->sample_rate;
  }
  bool earned = synth_earned(s);
  bool settling = earned && !playing;

  // How far into the sustain we are, and the pitch it set out from.  Advanced
  // before anything reads it, and `settle_from_log` therefore holds the last
  // pitch the note was actually played at.
  //
  // Linear into the sustain so the slide takes 250ms whatever the interval,
  // and a one-pole at the preset's own attack coming back out of it, so a
  // note struck over a sustaining one speaks at full immediately.  Coming
  // back out is also what a note that never earned a tail does, and what the
  // hold-off does while it waits: in both the pitch and the level simply stay
  // where the droop test froze them, and nothing slides anywhere.
  // Whenever there is a tail or a control asking for one.  This is also where
  // `settle` comes back to zero: a short note that never earns a tail still
  // has to unwind whatever the last one left behind, and so does switching
  // the control off part-way through a tail.
  if (s->sustain || s->settle > 0) {
    if (playing) {
      // Where the slide onto the sustain sets out from.  Not the pitch the
      // note ended on: the level is allowed to follow a trail-off all the way
      // down, and the pitch follows it too, so the last thing played is a
      // median 62-83 cents from what the note was -- drifting, usually
      // upward, as the whistle gives out.  Sliding from there means the tail
      // arrives by moving, and the move is audible because the level is
      // swelling back up underneath it at the same time.  A note held as
      // level as the player can hold it went up and then came back down.
      //
      // So it sets out from the note's own average, which is where the slide
      // was always trying to end up anyway; what is left for it to travel is
      // the snap, fifty cents at the outside.  That does put a step in the
      // pitch at the moment the player stops -- but that moment is the bottom
      // of the fade, 20dB or more under the note, which is the quietest the
      // voice ever is while it is still sounding.  It is the one place a step
      // costs nothing.
      s->settle_from_log = s->pitch_den > 1e-12f
          ? s->pitch_num / s->pitch_den : s->log_freq;
      s->settle_from_level = s->level;
    }
    if (settling) {
      // The first sample of a tail: start the movement from rest and from
      // phase zero, so it emerges rather than cutting in wherever the LFOs
      // had got to.  `settle` is exactly zero until here, which is what makes
      // this a usable edge.
      if (s->settle == 0) {
        s->shimmer_rate = 0;
        for (int g = 0; g < SYNTH_SHIMMER_LFOS; g++) {
          s->shimmer_pos[g] = 0;
        }
      }
      s->settle = fminf(1.0f, s->settle +
                        1.0f / (SYNTH_HOLD_SETTLE_S * s->sample_rate));
      s->shimmer_rate = fminf(1.0f, s->shimmer_rate +
                              1.0f / (SYNTH_TAIL_SHIMMER_S * s->sample_rate));
    } else if (!s->tail_ending) {
      // Not while a tail is being let go: unwinding takes p->attack_s, a few
      // milliseconds against a 0.6s fade, so a tail that unwound as it went
      // would swell 5.7dB back up to the note's level and then disappear.  It
      // unwinds after that, once the gate is down and nothing is audible --
      // or immediately, when a new note arrives and takes the tail with it.
      s->settle -= coeff(p->attack_s, s->sample_rate) * s->settle;
      // Exactly zero, not asymptotically close to it, so that "no tail" is a
      // state the rest of this can test for rather than a small number.
      if (s->settle < 1e-6f) {
        s->settle = 0;
      }
    }
  }
  // Eased at both ends, so the move starts and stops at zero velocity rather
  // than cornering into and out of the sustain.
  float settle = s->settle * s->settle * (3 - 2 * s->settle);

  if (onset) {
    // Land on the note rather than gliding onto it, and restart the growl
    // timer so the wobble belongs to this note.  Take the level as read too,
    // so the attack has this note's dynamics and not the last one's.
    s->log_freq = log2f(fmaxf(1, hint->freq));
    s->note_age = 0;
    s->level = hint->level;
    s->control_countdown = 0;   // this note's dynamics, not the last one's
    // The running pitch average starts from this note and knows nothing of
    // the last one.
    s->pitch_num = hint->level * hint->level * s->log_freq;
    s->pitch_den = hint->level * hint->level;
    s->level_num = hint->level * hint->level * hint->level;
    // Both per-note sweeps are armed here and decay from here on.  They are
    // set rather than added to, so a fast run gets the same swoop on every
    // note instead of stacking them up.
    s->drop = p->drop_octaves;
    s->cutoff_env = p->cutoff_env_octaves;
    s->pluck = 1;
  } else if (playing) {
    // Otherwise track continuously.  Glide is in the log domain so a bend
    // takes the same time everywhere, and it is fast enough to feel instant
    // while still smoothing the detector's hop-to-hop jitter.  When the hint
    // is not trustworthy its freq simply hasn't moved, so this holds.
    float target = log2f(fmaxf(1, hint->freq));
    s->log_freq += coeff(p->glide_s, s->sample_rate) * (target - s->log_freq);
  } else if (earned) {
    // A note being held slides onto the pitch it was *played* at rather than
    // the one it ended on.  The last few tens of milliseconds of a whistle are
    // where the pitch is worst -- the player is letting go and the detector is
    // working from a signal that has nearly gone -- and a hold parks there for
    // six seconds, so it is the one voice that cannot use it.
    //
    // And then onto the nearest real note, because that average is still
    // whatever the player's intonation was: see SYNTH_HOLD_SNAP_HZ.
    //
    // *Which* semitone it lands on is a question about what the player meant,
    // so it is asked of the musical offset alone: a whistle 42 cents flat of
    // G is a G, and it should not come out an F# because this voice happens
    // to detune.  Landing on it exactly then has to account for both, since
    // the artifact is in what the listener hears whether it was meant or not.
    float musical = synth_musical_offset(s);
    float target = s->pitch_den > 1e-12f
        ? synth_snap_log(s->pitch_num / s->pitch_den + musical) - musical -
          synth_artifact_offset(s)
        : s->settle_from_log;
    s->log_freq = s->settle_from_log + settle * (target - s->settle_from_log);
  }

  // The level drives the timbre, and tracks the input quickly while a note is
  // sounding.  Once the note is released it is *held*, not decayed: letting
  // it fall sweeps the cutoff across every partial in a few tens of
  // milliseconds, and sweeping thirty-odd harmonic amplitudes that fast
  // splatters -- it puts a scatter of non-harmonic energy around 1-3kHz into
  // the tail, which is audible precisely because the fundamental is on its
  // way out and no longer covers it.  Freezing the timbre and letting the
  // gate do the fading costs a little realism (real instruments do get
  // duller as they decay) and removes the artifact.
  //
  if (playing) {
    s->level += coeff(p->articulation_s, s->sample_rate)
                * (hint->level - s->level);
  } else if (earned && s->pitch_den > 1e-12f) {
    // What the tail inherits.  While there is input the level follows it
    // exactly as `reese` does, trail-off and all, which is what makes this
    // the same instrument to play; but that means by the time the player
    // stops it is at the bottom of a fade a median 22-30dB under the note,
    // and the loudness and the brightness that hang off it are down there
    // with it.  A tail that inherited that would be the quiet dark drone
    // this voice exists to avoid -- and freezing the level early to dodge it
    // is what used to make the voice stop following notes it was still being
    // given.
    //
    // So it inherits the note rather than the fade: the same weighted
    // average that carries the pitch, asked for level instead.  A trail-off
    // is quiet and the weight is power, so it counts for almost nothing.
    //
    // On `settle`, the same ramp as the pitch slide and the drop, and for a
    // reason worth stating: those three are one gesture and have to move
    // together.  On a one-pole of its own this started climbing the moment
    // the player stopped while the drop waited out the hold-off, so a level
    // note went up into its own tail and then came back down -- audible on
    // every note, and exactly the kind of move that reads as a fault because
    // it is the one move here that is not the player's.
    float avg = s->level_num / s->pitch_den;
    s->level = s->settle_from_level +
               settle * (avg - s->settle_from_level);
  }

  // Where the held pitch comes from: a running average weighted by how loud
  // the note was, so the quiet ends of it -- the scoop in, the fade out --
  // count for almost nothing against the part that was actually played.  The
  // weight is the level squared, which is the note's power.
  if (s->sustain && playing) {
    float k = coeff(SYNTH_HOLD_PITCH_S, s->sample_rate);
    float w = hint->level * hint->level;
    s->pitch_num += k * (w * s->log_freq - s->pitch_num);
    s->pitch_den += k * (w - s->pitch_den);
    s->level_num += k * (w * hint->level - s->level_num);
  }

  // Note the pre-decrement placement: `countdown-- <= 0` after setting
  // SYNTH_CONTROL_HOP fires every HOP+1 samples, not every HOP.
  if (--s->control_countdown <= 0) {
    update_controls(s, playing);
    s->control_countdown = SYNTH_CONTROL_HOP;
  }

  if (onset && !holding) {
    // Start at this note's dynamics rather than ramping up into them.
    //
    // Not when a tail is already sounding, which is the case only a holding
    // voice has: every other preset is at gate 0 when a note starts, so
    // snapping the envelope is inaudible, while here it is a step straight
    // into the listener's ears from whatever the tail was at.  Measured over
    // recordings/holding.f32 before this, the rendered envelope stepped down
    // a median of 8.7dB at an onset and as much as 47dB.  Leaving it alone
    // lets the one-pole below cover the distance at articulation_s instead.
    s->loudness_env = s->loudness;
  }

  // The note starting and stopping.  This is the only thing that starts or
  // stops sound -- there is no hard gate anywhere, so an uncertain detector
  // costs a fade rather than a click.
  //
  // A note that earned a tail freezes the gate where it is once the player
  // stops, and leaves it there.  Armed while the note is still being played,
  // which is what makes a wrong guess about the end of a note free: if the
  // level comes back up this is simply set again from a `playing` sample, and
  // nothing about the gate moved in between.  Measured, the longest wrong
  // guess inside a real note is 148ms, and now it costs nothing at all rather
  // than a fraction of a hold.
  if (playing) {
    s->tail = earned;
    s->tail_ending = false;
  } else if (s->tail && !earned) {
    // The switch went off -- or the player changed to a voice that opts out
    // -- under a sounding tail.  That is the gesture for ending a drone, so
    // it ends like a note rather than being cut: the gate fades at the tail's
    // own release, and `settle` is held where it is below so the level does
    // not jump back up to the note's on the way out.
    s->tail = false;
    s->tail_ending = true;
  }
  if (s->tail_ending && s->gate < 0.001f) {
    s->tail_ending = false;
  }
  if (playing || !s->tail) {
    float gate_target = playing ? 1.0f : 0.0f;
    // Only a tail gets the tail's slow fade; anything shorter releases at the
    // voice's own release_s, exactly as it does with the sustain switched
    // off.
    float release = earned || s->tail_ending ? SYNTH_HOLD_RELEASE_S
                                             : p->release_s;
    float gate_coeff = coeff(
        gate_target > s->gate ? p->attack_s : release, s->sample_rate);
    s->gate += gate_coeff * (gate_target - s->gate);
  }

  // How hard they are blowing, quick in both directions so the dips between
  // tongued notes survive.
  s->loudness_env +=
    coeff(p->articulation_s, s->sample_rate) * (s->loudness - s->loudness_env);

  // Three envelopes multiplied: the note starting and stopping, how hard the
  // player is blowing, and -- for the plucked voices -- where in the note's
  // own decay we are.
  s->amp = s->gate * s->loudness_env;
  if (p->decay_s > 0) {
    s->amp *= s->pluck;
  }
  // The same move, in level: the tail settles under the note that made it
  // rather than holding at it.  On the same ramp as the pitch slide, so the
  // two are one gesture.
  //
  // And it settles under the note's *own* level rather than under whatever
  // the trail-off had reached when the detector gave up -- the same weighted
  // average that carries the pitch, asked the same way.  That is what lets
  // the level follow a trail-off all the way down, which is what makes this
  // sound like `reese` while a note is being played: the tail does not
  // inherit the fade, it inherits the note.  With nothing to average it
  // reduces to the plain drop.
  //
  // Not conditional on the switch: with no tail `settle` is zero and this
  // multiplies by exactly one, and with a tail being let go the switch is
  // already off while the level it put there still has to be honoured -- ask
  // the switch and the drone jumps 5.7dB louder at the moment you end it.
  s->amp *= 1 + settle * (SYNTH_HOLD_SUSTAIN - 1);

  if (hint->voiced) {
    s->note_age += 1.0f / s->sample_rate;
  } else {
    s->note_age = 0;
  }

  s->pwm_pos += p->pwm_slow_hz / s->sample_rate;
  if (s->pwm_pos >= 1) {
    s->pwm_pos -= 1;
  }
  for (int g = 0; g < SYNTH_SHIMMER_LFOS; g++) {
    s->shimmer_pos[g] +=
      synth_shimmer_hz[g] * s->shimmer_rate / s->sample_rate;
    if (s->shimmer_pos[g] >= 1) {
      s->shimmer_pos[g] -= 1;
    }
  }
  s->growl_pos += p->growl_hz / s->sample_rate;
  if (s->growl_pos >= 1) {
    s->growl_pos -= 1;
  }
  // Free-running, like the growl: a wobble is a property of the patch, not of
  // where in the note you are, and restarting it per note is the one thing
  // that stops it sounding like a wobble at all.
  s->wobble_pos += p->wobble_hz / s->sample_rate;
  if (s->wobble_pos >= 1) {
    s->wobble_pos -= 1;
  }
  s->vibrato_pos += p->vibrato_hz / s->sample_rate;
  if (s->vibrato_pos >= 1) {
    s->vibrato_pos -= 1;
  }
  // Free-running for the same reason the growl is, and more so: a leslie does
  // not stop between notes, and starting the horn from the same angle on
  // every note would be the one thing that gives away that it is not actually
  // turning.  It does not stop between *phrases* either, which is why the
  // speed chases the dynamics whether or not anything is sounding: the rotors
  // coast down through a rest and are still coasting when the next note
  // arrives, exactly as they would in the room.
  if (p->leslie_horn_soft_hz > 0) {
    s->leslie_speed += coeff(p->leslie_spin_s, s->sample_rate) *
                       (s->leslie_target - s->leslie_speed);
    float horn_hz = p->leslie_horn_soft_hz +
      (p->leslie_horn_loud_hz - p->leslie_horn_soft_hz) * s->leslie_speed;
    float drum_hz = p->leslie_drum_soft_hz +
      (p->leslie_drum_loud_hz - p->leslie_drum_soft_hz) * s->leslie_speed;
    s->leslie_horn_pos += horn_hz / s->sample_rate;
    if (s->leslie_horn_pos >= 1) {
      s->leslie_horn_pos -= 1;
    }
    s->leslie_drum_pos += drum_hz / s->sample_rate;
    if (s->leslie_drum_pos >= 1) {
      s->leslie_drum_pos -= 1;
    }
  }

  s->drop -= coeff(p->drop_s, s->sample_rate) * s->drop;
  s->cutoff_env -= coeff(p->cutoff_env_s, s->sample_rate) * s->cutoff_env;
  if (p->decay_s > 0) {
    s->pluck += coeff(p->decay_s, s->sample_rate) * (p->sustain_level - s->pluck);
  }

  // Everything that comes out of update_controls steps at the control rate,
  // and anything that reaches the output unsmoothed puts a tone there: a
  // gain that jumps 1455 times a second buzzes at 1455Hz, fixed, regardless
  // of what is being played.  The harmonic amplitudes were already smoothed;
  // the drive was not, and it multiplies the entire signal.
  float smooth = coeff(0.004f, s->sample_rate);
  for (int i = 0; i < SYNTH_MAX_HARMONICS; i++) {
    s->harmonic_amp[i] += smooth * (s->harmonic_target[i] - s->harmonic_amp[i]);
  }
  s->drive_smoothed += smooth * (s->drive - s->drive_smoothed);
  s->fm_index_smoothed += smooth * (s->fm_index - s->fm_index_smoothed);

  if (s->amp < 1e-5f && !hint->voiced) {
    s->side = 0;
    return 0;
  }

  float f0 = exp2f(synth_pitch_log(s)) * synth_octave(s);
  // The rotors' doppler, per sample rather than at the control rate: this is
  // a frequency, and a frequency that steps 1455 times a second has a 1455Hz
  // buzz on it.  Unlike a gain there is nothing downstream smoothing it.
  float doppler_horn = 1, doppler_drum = 1;
  if (p->leslie_horn_soft_hz > 0) {
    doppler_horn = exp2f(p->leslie_horn_cents / 1200.0f *
                         cosf(2 * (float)M_PI * s->leslie_horn_pos));
    doppler_drum = exp2f(p->leslie_drum_cents / 1200.0f *
                         cosf(2 * (float)M_PI * s->leslie_drum_pos));
  }
  float out = 0;
  // The same sum again, but weighted by where each copy sits.  Accumulated
  // here rather than reconstructed later because this is the only place the
  // copies exist separately -- one line down they are one signal.
  float spread = 0;
  for (int u = 0; u < s->unison; u++) {
    float base = f0 * s->detune[u];
    float voice = 0;

    if (p->fm_index_loud > 0 || p->fm_index_soft > 0) {
      // Two-operator FM instead of the additive sum.  Nothing above the
      // fundamental is chosen here -- the index decides how much energy sits
      // where, and the Bessel envelope it produces moves in a way no
      // arrangement of `cutoff` and `tilt` reaches.  Scaled to 0.7 so it
      // arrives at the drive at the same level the additive path does.
      s->fm_phase[u] += base * p->fm_ratio / s->sample_rate;
      if (s->fm_phase[u] >= 1) {
        s->fm_phase[u] -= (int)s->fm_phase[u];
      }
      s->phase[u] += base / s->sample_rate;
      if (s->phase[u] >= 1) {
        s->phase[u] -= (int)s->phase[u];
      }
      float mod = sinf(2 * (float)M_PI * s->fm_phase[u]);
      voice = 0.7f * sinf(2 * (float)M_PI * s->phase[u] +
                          s->fm_index_smoothed * mod);
    } else if (synth_needs_partial_phase(s)) {
      // Partials aren't multiples of anything, so each one carries its own
      // phase and costs a sinf.
      for (int i = 0; i < s->harmonics_active; i++) {
        float* ph = &s->partial_phase[u][i];
        float hz = base * synth_partial_ratio(s, i + 1);
        // The leslie's other half: the doppler shift of a speaker swinging
        // towards you and away again.  On the cosine of the rotor angle where
        // the amplitude is on the sine, because a horn is moving towards you
        // fastest a quarter turn before it points at you -- see the leslie
        // block in synth.h.  Each partial follows the rotor on its side of
        // the crossover, so the horn's pitch swing and the drum's pull
        // against each other instead of the whole voice wobbling as one,
        // which is all a vibrato would be.
        if (p->leslie_horn_soft_hz > 0) {
          // Interpolated between the two rotors on the same crossover split
          // the amplitude uses, so a partial shared between them is pulled
          // about by both.  Linear in the multiplier rather than in cents,
          // which for swings this small is the same thing to well under a
          // cent and costs a multiply instead of an exp2f per partial.
          float w = s->leslie_mix[i];
          hz *= doppler_drum + w * (doppler_horn - doppler_drum);
        }
        *ph += hz / s->sample_rate;
        if (*ph >= 1) {
          *ph -= (int)*ph;
        }
        float a = s->harmonic_amp[i];
        if (i < p->mono_partials) {
          a *= s->low_gain[u];
        }
        voice += a * sinf(2 * (float)M_PI * *ph);
      }
    } else {
      s->phase[u] += base / s->sample_rate;
      if (s->phase[u] >= 1) {
        s->phase[u] -= (int)s->phase[u];
      }

      float theta = 2 * (float)M_PI * s->phase[u];
      float cos1 = cosf(theta);
      // sin(n*theta) by the Chebyshev recurrence, so each partial costs two
      // multiplies instead of a sinf.
      float sin_prev = 0;              // sin(0)
      float sin_cur = sinf(theta);     // sin(theta)
      for (int i = 0; i < s->harmonics_active; i++) {
        float a = s->harmonic_amp[i];
        if (i < p->mono_partials) {
          a *= s->low_gain[u];
        }
        voice += a * sin_cur;
        float next = 2 * cos1 * sin_cur - sin_prev;
        sin_prev = sin_cur;
        sin_cur = next;
      }
    }

    out += voice;
    spread += voice * s->pan[u];
  }

  // Drive before the envelope, so how dirty it sounds is set by how hard the
  // player is blowing and not by where they are in the note's decay.  This is
  // also most of what makes the voice cut: it fills in the partials above the
  // ones we synthesize.
  float undriven = out;
  if (p->drive_bias != 0) {
    // Off centre going in, and the saturator's value at that offset taken
    // back out so nothing is left at DC.  An odd function driven hard makes
    // only odd harmonics; this is what puts the even ones in.
    float b = p->drive_bias;
    out = atan_norm((out + b) * s->drive_smoothed) -
          atan_norm(b * s->drive_smoothed);
  } else {
    out = atan_norm(out * s->drive_smoothed);
  }

  // The gain the saturation just applied.  The side signal is scaled by this
  // rather than being saturated itself: running a difference of two copies
  // through its own atan normalizes it against a sum of three, which leaves
  // the sides louder than the middle and the image inside-out.  Taking the
  // gain instead keeps the spread in the same proportion to the note that it
  // had before the drive.
  float drive_gain = fabsf(undriven) > 1e-6f
      ? out / undriven : atan_norm(s->drive_smoothed);

  // Breath, added after the drive rather than through it: on a real flute the
  // noise is made at the embouchure and shares the tube, not the reed, so
  // saturating it along with the tone reads as a fuzzbox rather than as air.
  // The band tracks the note because the tube's resonances do.
  //
  // The level is set by the unweighted tone-to-noise ratio and not by LUFS,
  // which is misleading here: a K-weighted measurement of a 100Hz near-sine
  // against a noise band two octaves above it called the air "0.9LU over the
  // tone" when the actual ratio was 6.6dB, which is not any flute.  A real
  // flute's low register runs 15-25dB and the breathy big ones about 10-15.
  if (p->breath > 0) {
    float n = synth_noise(s);
    // A resonant bandpass, not a pair of gentle slopes.  Air in a tube is a
    // *band* -- the noise excites the bore and comes back with the bore's
    // shape on it -- and white noise with a tilt on it is not that.  A
    // Chamberlin state variable filter is two adds and two multiplies and
    // gives a real resonant band, which the cascaded one-poles could not:
    // any arrangement of one-poles is a slope, and a slope up to a couple of
    // kHz is what a snare sounds like.
    //
    // Centred low, at two and a half times the fundamental, because that is
    // where a large flute's air sits.  Capped in absolute Hz so the top of
    // the range doesn't drag the band up into the hiss again.
    float fc = fminf(900.0f, f0 * 2.5f);
    float f = 2 * sinf((float)M_PI * fc / s->sample_rate);
    const float q = 1.0f / 1.2f;
    s->breath_lp += f * s->breath_bp;
    float high = n - s->breath_lp - q * s->breath_bp;
    s->breath_bp += f * high;

    // Modulated by the tone, but only slightly.  The jet at the embouchure
    // makes both the note and the noise, so some correlation is right and it
    // is what puts the noise around the harmonics rather than under them --
    // but at any depth worth noticing it stops being a flute and becomes a
    // snare, which is a band of noise switched on and off at a low pitch.
    // 15% is enough to bind the air to the note and not enough to rattle.
    out += s->breath_bp * p->breath * (1.0f - 0.4f * s->dynamics) *
           (0.9f + 0.15f * out);
  }

  // High-pass after the drive, since the drive is what puts energy back below
  // the partials we were careful not to synthesize.
  if (p->min_partial_hz > 0) {
    // Below the lowest partial we allow, not at it: the job here is to
    // remove what the drive invented underneath, not to thin out the
    // fundamental we deliberately kept.
    float rc = 1.0f / (2 * (float)M_PI * p->min_partial_hz * 0.7f);
    float a = rc / (rc + 1.0f / s->sample_rate);
    for (int stage = 0; stage < 2; stage++) {
      float y = a * (s->hp_y[stage] + out - s->hp_x[stage]);
      s->hp_x[stage] = out;
      s->hp_y[stage] = y;
      out = y;
    }
  }

  // And the top, for a voice that is meant to be coming out of something.  A
  // leslie is a horn driver and a wooden box, not a full-range monitor, and
  // nine sines running to 6kHz with a saturator on top of them is neither.
  if (p->top_hz > 0) {
    float a = 1 - expf(-2 * (float)M_PI * p->top_hz / s->sample_rate);
    for (int stage = 0; stage < 2; stage++) {
      s->lp_y[stage] += a * (out - s->lp_y[stage]);
      out = s->lp_y[stage];
    }
  }

  // Same envelope and gain as the mid, so width changes where a note sits
  // and not how loud it is.  It skips the high-pass: only single-oscillator
  // voices ask for one, and those have no spread to begin with.
  //
  // Left is mid+side and right is mid-side, so the two channels sum back to
  // exactly twice the mono output -- a stereo image that folds down without
  // anything cancelling, which for detuned copies is otherwise exactly what
  // goes wrong.
  s->side = spread * drive_gain * p->stereo_width * s->amp * p->out_gain *
            s->audibility_comp;

  return out * s->amp * p->out_gain * s->audibility_comp;
}
