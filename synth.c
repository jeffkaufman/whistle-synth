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
// The tail used to hold for 2s and then fade over a 0.6s time constant --
// SYNTH_HOLD_S and SYNTH_HOLD_RELEASE_S, inherited from the `reese-hold`
// preset's release_hold_s and release_s.  Both are gone: the tail now holds
// until a note takes it, and nothing times it out.
//
// The two numbers were always a compromise with no good setting.  Long enough
// to carry a phrase is long enough that the fade lands somewhere musical only
// by luck, and short enough to be predictable is short enough that holding a
// drone means re-whistling it every couple of bars.  Removing the clock
// removes the choice: the drone stays until it is replaced, which is what a
// drone is.
//
// What makes that playable rather than a note stuck on is that stopping it is
// already in the instrument.  A note under SYNTH_HOLD_MIN_NOTE_S earns no
// tail, so one short note takes the drone -- monophonic, so it always does --
// and then stops the way any short note stops.  Play a long note to move the
// drone, a short one to end it.

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
// A second, not two.  This was argued from a tail that was two seconds at full
// level and then a 0.6s release -- about three seconds audible, so spending
// half of it getting up to speed left the movement no room to be movement.
// The tail no longer ends, so that budget is gone and a slower acceleration
// would now be affordable; 0.5s stays because the measurement below is what
// chose it, and a drone that takes longer than that to start moving reads as
// a note stuck on rather than as a note ringing.  Measured
// over the tails in recordings/holding.f32, band wander across the whole tail
// against `reese`'s natural 4.44dB:
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

// How close to the note the tone-hole lattice's corner may come, as a
// multiple of the played pitch.  The lattice is fixed in Hz and the note is
// not, so a high enough note walks up to its own cutoff -- and a real flute
// played there does not speak, which is a true thing about the instrument and
// an unacceptable thing for a voice to do to a pitch the player asked for.
// The note itself is safe whatever this is, because the stage is divided
// back out by its own gain at the played pitch and so the loop's gain at the
// fundamental is `breath` wherever the corner sits.  This only stops the
// corner from going under the note it is supposed to be shaping.
#define SYNTH_LATTICE_MIN_RATIO 1.2f

// What the waveguide's breath falls back to between notes, as a fraction of
// the threshold of oscillation.
//
// Not zero, because a player does not stop the air dead -- and emphatically
// not 1, which is the threshold itself: at a loop gain of exactly 1 the tube
// neither grows nor decays, so a released note hung on at whatever amplitude
// it had until the silence check cut it off, a 133dB step on every note.
//
// Half decays the tube by half its amplitude a round trip, which is a few
// milliseconds, and it puts the speaking point at 0.26 of the note envelope
// rather than 0.42.  That last part is what the sustain control needs: a tail
// settles to SYNTH_HOLD_SUSTAIN of the note that made it, which is 0.52, and
// against a speaking point of 0.42 the drone came out barely above the
// threshold -- measured over recordings/holding.f32 it sounded through 15% of
// the take against `flute-low`'s 69%, with an 11.6 second hole in it.
#define SYNTH_BORE_REST 0.5f

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
    .out_gain = 0.276f,
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
    // A concert flute, and the only voice here that is not a bass.  Every
    // number is measured against real flute recordings rather than set by
    // ear: a held C5 (recordings/flute2.f32) and a descending octave of held
    // notes.  The octave take has since been overwritten, so the numbers
    // taken off it survive only in prototypes/flute-harmonics.tsv.
    //
    // One octave down rather than four.  A whistle covers 588-3150Hz, which
    // lands at 294-1575Hz: D4 to G6, almost exactly a concert flute's range.
    .name = "flute",
    .octave = 0.5f,
    // The partials come from the recordings rather than from the pulse
    // formula, which is what `register_hi_hz` above zero selects; pwm_center,
    // tilt, cutoff_* and rolloff_exp are all unused here.  The formula could
    // not make this shape: partial 7 sits *above* partial 6 in the low
    // register, and `sin(n*pi*w)/n^tilt/(1+(n/c)^k)` is monotone by
    // construction.  Fitted over nine notes it left the audible partials
    // 5.1dB rms out; the tables are exact where they were measured and the
    // 4.0dB that remains is note-to-note variation -- a real flute's
    // harmonics genuinely differ by several dB depending on the fingering,
    // and nothing indexed by pitch can follow that.
    //
    // Partials past the ninth were never measurable above the air and are
    // continued at the slope of the ones that were.  They sit 60dB down.
    .partial_lo = { 0.0f, -11.3f, -19.6f, -35.0f, -37.3f, -50.6f, -47.4f, -56.9f, -59.3f, -63.0f, -66.1f, -69.2f },
    .partial_hi = { 0.0f, -24.1f, -31.9f, -37.9f, -41.8f, -46.6f, -57.1f, -57.9f, -62.3f, -64.9f, -67.5f, -70.1f },
    // The register break, and the reason there are two tables.  Below it the
    // second partial sits 11dB under the fundamental and above it 24dB under:
    // the flute gets dramatically purer as it climbs, and treating it as one
    // fixed spectrum makes the top of the range far too rich.  580-700Hz is
    // where the fit put the change, which is D5-F5 -- where a flute actually
    // changes register.
    .register_lo_hz = 580.0f, .register_hi_hz = 700.0f,
    // Backwards, and measured.  Over the one real crescendo in the
    // recordings -- 15dB on a held C5 -- every partial above the fundamental
    // falls about 6dB relative to it, so the flute gets louder and *rounder*
    // rather than louder and brighter.  That is what a controlled crescendo
    // on a flute is: the player opens the embouchure and moves more air
    // rather than blowing faster, which is also what keeps the pitch from
    // rising.  `trombone` ran its filter 1.1 to 14 in the other direction,
    // because going from nothing to blazing is what brass does.
    .purity_loud = 6.0f,
    // Almost none, and lower than it looks like it should be.  A flute does
    // not saturate, and the saturator here is not a garnish but a
    // contaminant: at 0.45 it regenerated a third harmonic in phase with the
    // one the table places and lifted it 4.4dB, which is most of the
    // difference between a flute and a clarinet.  What this costs is
    // headroom -- see out_gain.
    .drive_soft = 0.15f, .drive_loud = 0.22f,
    // One air column.  A flute has nothing to beat against, and detuning it
    // would make it two flutes.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 12,
    // The air, and the single biggest thing wrong with the first version of
    // this voice.  Set to the reference note's measured tone-to-air ratio,
    // 30.0dB, which the render matches to 0.3dB.  The band it runs through
    // matters more than the level: see the breath block in synth_process.
    .breath = 0.0175f,
    // No vibrato and no wobble.  The recordings are straight tone -- the
    // pitch holds to 2-3 cents rms and the *level* of a held note to 0.20dB
    // rms, which is steadier than anything else here manages.  The first
    // version of this voice put a 2.4Hz cutoff wobble on it to keep a held
    // note alive and measured 0.96dB of level movement for it, five times the
    // real thing.  A flute held still really is that still.
    .level_full = 0.22f,
    // Slow to speak and quick to stop, which is the flute's envelope and the
    // opposite of everything else in this table -- the basses all start in
    // 3-8ms.  Measured over the onsets, the note takes 60-100ms to arrive and
    // 30-70ms to fall 20dB when the player stops.
    //
    // The attack is taken from the quick end of what was measured rather than
    // the middle.  The recordings are all long tones started gently, which is
    // the slowest attack a flute has; at 110bpm an eighth note is 270ms, and
    // an attack fitted to the gentlest long tone would spend a third of every
    // note arriving.  This is the one number here set by what the voice has
    // to play rather than by what was measured.
    .attack_s = 0.030f, .release_s = 0.028f,
    // Slower than the basses' 8ms, because a column of air cannot change
    // level as fast as a plucked string can.
    .articulation_s = 0.020f,
    .glide_s = 0.006f,
    // Over 1, which no other preset needs, and it is the near-linear drive
    // above that costs it: an `atan` barely into its curve passes a fraction
    // of what a driven one does and the gain has to come back somewhere.
    .out_gain = 1.403f,
  },
  {
    // The same flute an octave down, which is an alto or bass flute: a
    // whistle lands at 147-787Hz against `flute`'s 294-1575.  The bottom of
    // that is below a concert flute entirely -- 147Hz is D3, where a bass
    // flute lives -- so this is a different instrument in the family rather
    // than the same one transposed, and two things have to move with it.
    //
    // Everything not listed here is `flute`'s, deliberately.  There is no
    // recording of a bass flute to fit against, so the numbers that were
    // measured stay exactly as measured and only the two that are structurally
    // wrong at this size are changed.
    .name = "flute-low",
    .octave = 0.25f,
    .partial_lo = { 0.0f, -11.3f, -19.6f, -35.0f, -37.3f, -50.6f, -47.4f, -56.9f, -59.3f, -63.0f, -66.1f, -69.2f },
    .partial_hi = { 0.0f, -24.1f, -31.9f, -37.9f, -41.8f, -46.6f, -57.1f, -57.9f, -62.3f, -64.9f, -67.5f, -70.1f },
    // Halved, and this is the first of the two.  The register break is a
    // property of where a note sits in the *instrument's* range, not of its
    // absolute frequency, and a flute an octave down breaks an octave lower.
    // Left at `flute`'s 580-700Hz the whole of this voice's range bar the top
    // fifth would read as low register, so whistling a phrase here would come
    // out with a different timbral shape from the same phrase on `flute` --
    // which is not what "the same instrument, lower" means.  Halved, the
    // contour of a phrase is identical and only the pitch moves.
    .register_lo_hz = 290.0f, .register_hi_hz = 350.0f,
    .purity_loud = 6.0f,
    .drive_soft = 0.15f, .drive_loud = 0.22f,
    .unison = 1, .detune_cents = 0.0f, .harmonics = 12,
    .breath = 0.0175f,
    // And this is the second.  The air is fixed in Hz because it is made at
    // the embouchure and in the player's mouth -- but a bass flute's
    // embouchure hole is roughly twice the size, so its air sits lower.  Left
    // at 1600Hz the band would sit 8-15 partials above this voice's
    // fundamental instead of the 3-7 it measures on a concert flute, and
    // air that far above the note stops reading as breath and starts reading
    // as hiss beside it.
    //
    // 1000Hz rather than 800 is a compromise and the one number here that is
    // neither measured nor derived: the instrument scales by two but the
    // player's mouth does not scale at all, so the truth is somewhere between
    // unchanged and halved.  If this voice sounds wrong, start here.
    .breath_hz = 1000.0f,
    .level_full = 0.22f,
    // Unchanged from `flute`, and worth saying why, because the physics says
    // a bigger air column takes longer to speak.  It surely does -- but by
    // how much is a guess without a recording, and a wrong guess costs fast
    // playing, so the measured numbers stay until there is something to
    // measure against.
    .attack_s = 0.030f, .release_s = 0.028f,
    .articulation_s = 0.020f,
    .glide_s = 0.006f,
    .out_gain = 1.403f,
  },
  {
    // The same instrument as `flute-low`, built the other way round.
    //
    // `flute-low` is a measured spectrum played back: two tables of partial
    // amplitudes, a register blend between them, an attack in milliseconds.
    // This has no spectrum in it at all.  It is a tube one wavelength long
    // with a jet blowing across the end of it, and what comes out is whatever
    // that does -- see synth_bore.  Nothing here says how loud the second
    // partial should be, how quickly the note should arrive, or how the
    // timbre should move when the player leans in; all of it falls out of one
    // number, which is how hard the tube is being blown.
    //
    // What that buys and what it costs is in "The flute by physics" in the
    // README.  Briefly: it gets the behaviour and misses the spectrum.
    //
    // The note arrives when the loop has built rather than when an envelope
    // says so, which lands it at 25-30ms against the 20-25 measured on the
    // real flute and `flute-low`'s 36; and it stops when the tube stops,
    // 39ms to -6dB against the real 34-39 and `flute-low`'s 26.  Both are
    // slower for a low note than a high one, because the loop is a period
    // long and a low note's period is longer -- which is a thing the real
    // instrument does and no number here asks for.
    //
    // It also has a register now, which is what `jet_lattice_hz` is for and
    // is the one structural thing the first version of this voice got
    // backwards.  It used to be the same instrument at every pitch -- every
    // audible partial within 0.8dB of itself over the whole range -- and a
    // real flute is emphatically not that.  Measured across thirteen held
    // notes spanning 400 to 1084Hz, the frequency at which the real
    // instrument's harmonic series has fallen 40dB under the fundamental
    // does not move with the note: it sits at 2569Hz, fitted against pitch
    // with a log-log slope of +0.057 where fixed-in-Hz predicts 0 and a
    // fixed harmonic number predicts 1.  This voice measured +1.00, exactly
    // the wrong end.  With the lattice it measures +0.66, and the second
    // partial runs from -19.9dB at the bottom of the range to -25.2 at the
    // top, where before it sat within a dB of -17.9 at every pitch.
    //
    // The spectrum is 6.8dB rms off the measured table, against 9.0 before
    // and against an additive voice that is exact by construction.  What is
    // left is still mostly one thing: a real flute's fourth partial sits
    // 15dB under its third, and no smooth nonlinearity in a smooth loop
    // makes a notch -- here the gap is 8.3dB at the bottom of the range.  It
    // is the same wall the pulse formula hit before the tables replaced it.
    .name = "flute-jet",
    .octave = 0.25f,
    // Above 1 this is a waveguide, and it is the loop's small-signal gain at
    // full breath: 1.0 is exactly the threshold of oscillation.
    //
    // 2.4 before the lattice, and the two move together: the lattice takes
    // the top off the spectrum, so the jet has to be driven harder for what
    // is left under the corner to come out at the right level.  Swept against
    // the corner, 3.2 is the bottom of the flat part -- 6.8dB rms at both
    // 3.2 and 4.2, against 10.3 at 2.4 -- and the lower of the two is worth
    // having because the higher one costs headroom and starts to buzz.  It
    // speaks faster than 2.4 did at every pitch, which is the direction this
    // voice has always needed to move.
    .jet_drive = 3.2f,
    // How far the jet sits off the labium, which is what puts the even
    // harmonics in.  0.7 is what the second partial wants: it lands at
    // -18.9dB, against the -17.3 the measured table blends to at the loud end
    // of a crescendo and -11.3 at mid dynamics.
    .jet_bias = 0.7f,
    // The bore's losses and its low cutoff, as multiples of the note.  0.8 is
    // the value that plays in tune -- the whole range and the whole of the
    // dynamics land between +4.3 and +6.5 cents -- and 0.5 is as high as the
    // cutoff can go before it starts eating the fundamental it is protecting.
    .jet_damp = 0.8f, .jet_hp = 0.5f,
    // Turbulence at the embouchure.  It is what the note grows out of, so
    // more of it speaks sooner -- but only just, since the loop builds
    // geometrically and this only moves where it starts from: a hundredfold,
    // 0.002 to 0.2, took 70ms down to 42.  The breath does that job, and this
    // only has to be enough to get the loop going.
    .jet_noise = 0.03f,
    // The tone-hole lattice, and the only thing in this loop that stands
    // still while the note moves -- see jet_lattice_hz in synth.h for the
    // measurement behind it.
    //
    // 2569Hz is the real concert flute's, measured, and it is used here
    // unchanged rather than halved for an instrument an octave down.  That
    // looks wrong and is not: `flute-low` is `flute`'s measured spectrum
    // played an octave lower with the measured numbers deliberately left as
    // measured, so the spectrum this voice is being asked to match is the
    // concert flute's.  The corner that reproduces it is therefore the
    // concert flute's too.  Swept independently, the best fit landed at
    // 2600Hz -- within 1% of the measured 2569 -- which is the check that
    // this is the lattice and not a curve fit that happens to help.
    .jet_lattice_hz = 2569.0f,
    // Near-linear, and for the same reason `flute` is: the saturator makes
    // harmonics, and every harmonic in this voice is supposed to have come
    // out of the jet.  It does not move with the dynamics here, because the
    // breath already does everything moving with the dynamics should do.
    .drive_soft = 0.15f, .drive_loud = 0.15f,
    // One air column, and no partial table -- the tube has no use for either.
    .unison = 1, .detune_cents = 0.0f, .harmonics = 1,
    // The air, unchanged from `flute-low`: the between-harmonic noise in
    // recordings/flute5.f32 is the same plateau from 1.5 to 4kHz the earlier
    // recordings gave, so the band that was fitted to them still holds.  It
    // is added outside the loop because it is made at the embouchure and
    // radiates from there rather than going round the tube.
    .breath = 0.0175f,
    .breath_hz = 1000.0f,
    .level_full = 0.22f,
    // Neither of these is what it is everywhere else in this table.  There
    // they are how long the note takes to arrive and to go; here they are
    // only how fast the breath does, and the note arrives when the loop has
    // built on top of it and goes when the breath falls back under the
    // threshold.  Measured through the engine, that lands the attack at
    // 29.5ms at the bottom of the range and 25.2 at the top, and the release
    // at 39ms to -6dB and 73-87 to -20 -- against 20-25 and 34-39 and 64-72
    // on the five tongued notes in recordings/flute5.f32.
    //
    // So they are set short and the tube is left to do the shaping.  8ms is
    // as fast as the breath can arrive without the loop having to build from
    // a step, and 25ms is what puts the release where the recording has it:
    // the audible note-off is the breath crossing the speaking threshold,
    // which happens well before the envelope has finished falling.
    .attack_s = 0.008f, .release_s = 0.025f,
    .articulation_s = 0.020f,
    .glide_s = 0.006f,
    // Re-matched after the lattice and the drive both moved: 2.057 before.
    // Measured as LUFS over prototypes/in-ladder.f32 at full volume, this
    // and `flute-low` agree to 0.00dB, with this one peaking at 0.373
    // against 0.217 -- a near-sine carries more of its level in the peak.
    .out_gain = 1.556f,
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

// Where partial n actually sits, as a multiple of the fundamental.  The
// octave stack isn't a harmonic series: its partial n is the nth octave.
static float synth_partial_ratio(const struct SynthParams* p, int n) {
  if (p->octave_stack_hz > 0) {
    return exp2f((float)(n - 1));
  }
  return n * (1 + p->stretch * (n - 1));
}

// Whether the partials are far enough off the harmonic series that the
// Chebyshev recurrence can't reach them and each one needs its own phase.
static bool synth_needs_partial_phase(const struct SynthParams* p) {
  return p->stretch > 0 || p->octave_stack_hz > 0;
}

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

// Level mapped to the 0..1 the envelope runs on.  A slight compression, no
// more, or the voice stops responding to how hard it is being pushed.
static float synth_loudness_of(const struct Synth* s, float level) {
  return fminf(1.0f, powf(fmaxf(0, level / s->params->level_full), 0.8f));
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

// The waveguide's loop has to be exactly one period long at the note being
// played, and every filter in it carries a delay of its own.  These four give
// each stage's phase delay and its gain at the played pitch: the delays come
// out of the delay line, and the gains are divided back out so the round trip
// is lossless at the fundamental however dark the filter is above it.
//
// Derived rather than fitted, and it is worth saying what that buys.  With
// them the loop's linear resonance lands on the requested pitch to better
// than a hundredth of a cent at every frequency checked; without the
// high-pass term alone the voice plays 45 cents sharp, because (1 - z^-1)
// leads by a quarter turn and a quarter turn is a fixed fraction of a period
// wherever the note is.
static float synth_pole_delay(float a, float w) {
  return atan2f(a * sinf(w), 1 - a * cosf(w)) / w;
}

static float synth_pole_gain(float a, float w) {
  float re = 1 - a * cosf(w), im = a * sinf(w);
  return (1 - a) / sqrtf(re * re + im * im);
}

static float synth_hp_delay(float r, float w) {
  return (atan2f(r * sinf(w), 1 - r * cosf(w)) + 0.5f * w -
          0.5f * (float)M_PI) / w;
}

static float synth_hp_gain(float r, float w) {
  float re = 1 - r * cosf(w), im = r * sinf(w);
  return 2 * sinf(0.5f * w) / sqrtf(re * re + im * im);
}

// Reads the tube `d` samples back, interpolating, because the delay that puts
// a note in tune is almost never a whole number of samples.
static float synth_bore_read(const struct Synth* s, float d) {
  int i = (int)d;
  float f = d - (float)i;
  int a = (s->bore_write - i + SYNTH_BORE_MAX) % SYNTH_BORE_MAX;
  int b = (a - 1 + SYNTH_BORE_MAX) % SYNTH_BORE_MAX;
  return s->bore[a] * (1 - f) + s->bore[b] * f;
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
  // The register blend divides by the gap between these two and takes their
  // logs, so both have to be positive and distinct.
  if (p->register_hi_hz > 0) {
    // And the tables only go as far as they were filled in.  The rest of the
    // array is zero, which in a table of dB *under* the fundamental means
    // full strength rather than silence -- so a preset asking for more
    // partials than it has measurements for would sound every one of them at
    // the top of its lungs.  `harmonics` is a control the Mac app puts on a
    // slider, so this is reachable by dragging, not just by editing the
    // table.  An entry counts as filled unless it is exactly zero in both.
    int filled = 1;
    while (filled < SYNTH_MAX_HARMONICS &&
           !(p->partial_lo[filled] == 0.0f && p->partial_hi[filled] == 0.0f)) {
      filled++;
    }
    if (p->harmonics > filled) {
      p->harmonics = filled;
    }
  }
  if (p->register_hi_hz > 0) {
    if (!(p->register_lo_hz > 1.0f)) {
      p->register_lo_hz = 1.0f;
    }
    if (!(p->register_hi_hz > p->register_lo_hz * 1.001f)) {
      p->register_hi_hz = p->register_lo_hz * 1.001f;
    }
  }
  if (!(p->purity_loud > -60 && p->purity_loud < 60)) {
    p->purity_loud = 0;
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
  // The waveguide's two corners are multiples of the note and end up inside
  // an expf, and its bias ends up in a tanh whose slope is a divisor.  A
  // drive at or under 1 is the threshold of oscillation or below it, which is
  // a voice that cannot make a sound, so it reads as "not a waveguide".
  if (!(p->jet_drive > 1.0f)) {
    p->jet_drive = 0;
  }
  if (p->jet_drive > 0) {
    if (!(p->jet_damp > 0.01f)) {
      p->jet_damp = 0.8f;
    }
    if (!(p->jet_hp > 0.01f)) {
      p->jet_hp = 0.5f;
    }
    // Past about 1.8 the slope of tanh has fallen so far that the breath
    // needed to reach the threshold saturates the jet outright.
    if (!(p->jet_bias >= 0 && p->jet_bias < 1.8f)) {
      p->jet_bias = 0.7f;
    }
    if (!(p->jet_noise >= 0)) {
      p->jet_noise = 0;
    }
    if (!(p->jet_lattice_hz >= 0)) {
      p->jet_lattice_hz = 0;
    }
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

  // The jet's operating point.  Both depend only on the preset, so they are
  // worked out here rather than every sample: `bore_t0` is subtracted from
  // the jet's output so it adds no offset of its own, and its slope there is
  // what the breath has to overcome for the tube to speak.
  s->bore_t0 = tanhf(params->jet_bias);
  float slope = 1 - s->bore_t0 * s->bore_t0;
  s->bore_slope_inv = slope > 1e-3f ? 1.0f / slope : 1e3f;
  for (int k = 0; k < SYNTH_LATTICE_POLES; k++) {
    s->bore_lat[k] = 0;
  }
  s->bore_lat_a = 0;
  s->bore_lat_norm = 1;
  // The tube is a different length for a different voice, so a length carried
  // over from the last one would be read once before the control rate caught
  // up.  Zero means "take the next one outright" -- see synth_process.
  s->bore_len = 0;
}

void synth_set_preset(struct Synth* s, int preset) {
  if (preset < 0 || preset >= N_PRESETS) {
    preset = 0;
  }
  synth_set_params(s, &presets[preset]);
}

void synth_set_fifth(struct Synth* s, bool on) {
  // 2/3 exactly, not exp2f(-7.02/12).  See synth.h.
  s->octave_mul = on ? 2.0f / 3.0f : 1.0f;
}

void synth_set_sustain(struct Synth* s, bool on) {
  s->sustain = on;
}

void synth_init(struct Synth* s, float sample_rate, int preset) {
  memset(s, 0, sizeof(*s));
  s->sample_rate = sample_rate;
  s->octave_mul = 1;
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

  float reach = fmaxf(0, s->level / p->level_full);

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
         f0 * synth_partial_ratio(p, active) > s->sample_rate * 0.45f) {
    active--;
  }
  s->harmonics_active = active;

  float base = exp2f(synth_pitch_log(s)) * synth_octave(s);

  if (p->jet_drive > 0) {
    // Size the tube so the wave comes back round in exactly one period, and
    // work out the coefficients of the four filters in the loop.  There is no
    // spectrum to compute here and no partials to fill in: this voice has
    // none, and everything below would be work thrown away.
    float w = 2 * (float)M_PI * base / s->sample_rate;
    // Nothing sensible plays this high or this low, and both ends of the
    // range would divide by something near zero.
    w = fmaxf(1e-4f, fminf(1.0f, w));
    float a = expf(-2 * (float)M_PI * p->jet_damp * base / s->sample_rate);
    float r = expf(-2 * (float)M_PI * p->jet_hp * base / s->sample_rate);
    s->bore_lp_a = a;
    s->bore_hp_r = r;
    s->bore_lp_norm = 1.0f / synth_pole_gain(a, w);
    s->bore_hp_norm = 1.0f / synth_hp_gain(r, w);
    // The lattice, which is the one corner here that stands still while the
    // note moves.  Clamped to stay above the note: a flute played above its
    // own lattice cutoff does not speak, which is true of the instrument but
    // is not a thing this voice may do to a pitch the player asked for.
    float lat = 0, lat_delay = 0;
    if (p->jet_lattice_hz > 0) {
      float corner = fmaxf(p->jet_lattice_hz, SYNTH_LATTICE_MIN_RATIO * base);
      lat = expf(-2 * (float)M_PI * corner / s->sample_rate);
      s->bore_lat_a = lat;
      s->bore_lat_norm = 1.0f / synth_pole_gain(lat, w);
      lat_delay = SYNTH_LATTICE_POLES * synth_pole_delay(lat, w);
    } else {
      s->bore_lat_a = 0;
      s->bore_lat_norm = 1.0f;
    }
    float len = s->sample_rate / base -
                2 * synth_pole_delay(a, w) - 2 * synth_hp_delay(r, w) -
                lat_delay;
    s->bore_len_target =
        fmaxf(4.0f, fminf((float)(SYNTH_BORE_MAX - 2), len));

    // What the listener hears of it.  The fundamental's own weighting rather
    // than a sum over partials, which every other voice needs: this one is
    // near enough a pure tone -- the second partial measures 15-19dB down and
    // the rest further -- and carrying the harmonics moves the answer by
    // under a tenth of a dB anywhere in this voice's range.
    float heard = synth_audibility(base);
    s->audibility_comp = heard > 1e-12f
        ? fminf(SYNTH_AUDIBILITY_MAX, 1.0f / sqrtf(heard)) : 1;
    // The saturator is a contaminant here rather than a garnish, so it is
    // near-linear and does not move with the dynamics: see the preset.
    s->drive = p->drive_soft + (p->drive_loud - p->drive_soft) * dynamics;
    return;
  }

  // Drop partials too low for anything to reproduce.
  int lowest = 1;
  if (p->min_partial_hz > 0) {
    while (lowest <= active &&
           base * synth_partial_ratio(p, lowest) < p->min_partial_hz) {
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
    if (p->octave_stack_hz > 0) {
      // A bell fixed in Hz, not in partial number.  That is the whole trick:
      // the components move under a weighting that doesn't, so an octave of
      // played pitch slides the stack one slot and lands back on itself.
      float hz = base * synth_partial_ratio(p, n);
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
    } else if (p->register_hi_hz > 0) {
      // Straight out of a recording.  Two tables, blended by where the note
      // sits between the two registers, because a flute's harmonics are not a
      // fixed set of ratios -- see partial_lo in synth.h.
      float t = (log2f(base) - log2f(p->register_lo_hz)) /
                (log2f(p->register_hi_hz) - log2f(p->register_lo_hz));
      t = fmaxf(0.0f, fminf(1.0f, t));
      float d = p->partial_lo[i] + (p->partial_hi[i] - p->partial_lo[i]) * t;
      // The tables are the spectrum at mid dynamics, so this is signed: the
      // partials above the fundamental thin out as the player pushes and fill
      // back in as they back off.
      if (n >= 2) {
        d -= p->purity_loud * (dynamics - 0.5f);
      }
      amp = powf(10.0f, d * 0.05f);
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
    heard += amp * amp * synth_audibility(base * synth_partial_ratio(p, n));
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

// One sample of the jet-drive waveguide: a tube with a jet blowing across the
// end of it.  `breath` is how hard it is being blown, as the loop's
// small-signal gain -- 1.0 is exactly the threshold of oscillation.
//
// The loop is one delay line and four filter stages, and the wave goes round
// it once per period:
//
//   the tube        a delay line, one period long
//   the losses      two one-poles at jet_damp times the note
//   the cutoff      two high-pass stages at jet_hp times the note
//   the open end    the sign flip, which is what makes it a flute and not a
//                   clarinet: two inversions a round trip, so every harmonic
//                   is supported rather than only the odd ones
//   the jet         a saturating function of the acoustic field at the
//                   embouchure, which is the only thing here that is not
//                   linear and so the only thing that makes harmonics at all
//
// Every filter stage is divided by its own gain at the played pitch, so the
// round trip is lossless at the fundamental however dark the filter is above
// it.  That is what leaves the loop's gain equal to `breath` exactly, which
// is what makes `breath` mean the same thing at every pitch: measured, the
// voice speaks at the same point on the dial and settles at the same spectrum
// to within 0.2dB a partial from 98Hz to 784Hz.
//
// The jet is the whole return path rather than an addition to a reflection,
// and that is a choice with a cost.  A tube that reflects on its own has a Q,
// and its note goes on ringing after the breath stops; this one does not, so
// the release is the breath leaving rather than the tube emptying.  It buys
// the thing that matters more, which is that there is exactly one loop and so
// exactly one mode: with a reflection path beside the jet -- the arrangement
// in Cook's flute, which this started as -- the two loops are different
// lengths and which of them wins depends on how hard you blow.  Measured that
// way, one note came out at 200Hz, 312Hz and 613Hz at three breath pressures
// with nothing else changed, which is not an instrument you can play.
static float synth_bore(struct Synth* s, const struct SynthParams* p,
                        float breath) {
  float wave = synth_bore_read(s, s->bore_len);

  // Half the trip's losses on the way down the tube, and half on the way
  // back, so the tap in the middle is the wave that reached the open end
  // rather than the one just injected at the embouchure.
  s->bore_lp[0] += (1 - s->bore_lp_a) * (wave - s->bore_lp[0]);
  wave = s->bore_lp[0] * s->bore_lp_norm;

  // The tone-hole lattice, which sits at the far end of the tube because that
  // is where the open holes are.  Two poles fixed in Hz: below the corner the
  // holes reflect and the note stands in the bore; above it they stop
  // reflecting, the wave runs off down the lattice, and nothing comes back.
  //
  // It is the only stage here that does not move with the note, and so the
  // only thing that gives this voice a register -- see jet_lattice_hz.
  if (s->bore_lat_a > 0) {
    for (int k = 0; k < SYNTH_LATTICE_POLES; k++) {
      s->bore_lat[k] += (1 - s->bore_lat_a) * (wave - s->bore_lat[k]);
      wave = s->bore_lat[k] * s->bore_lat_norm;
    }
  }

  // What the listener hears, tapped here rather than before the lattice
  // because the sound leaves the instrument through those same open holes:
  // whatever the lattice will not carry back up the tube it also will not
  // radiate.  Tapped before it, the jet's harmonics reached the output having
  // passed only one of the bore's two loss poles and none of the lattice at
  // all, which is why a lattice put anywhere else in the loop changed the
  // spectrum by two or three dB and left the comb standing.
  float at_end = wave;

  s->bore_lp[1] += (1 - s->bore_lp_a) * (wave - s->bore_lp[1]);
  wave = s->bore_lp[1] * s->bore_lp_norm;

  // The open end inverts, and the tube has nothing to say below its own
  // fundamental.  The high-pass is not a nicety.  tanh is concave on the side
  // the jet sits, so an oscillation across it does not average to its value
  // at rest however carefully `bore_t0` is subtracted -- it leaves an offset
  // that grows with the amplitude, the loop passes it, and left alone it
  // walks the jet off into saturation and the note stops.  One stage is not
  // enough either: with one, a mode a tenth of the way below the note sits
  // over the threshold and takes the voice over, which measured as the top of
  // the range playing a steady 50Hz instead of what was asked for.
  wave = -wave;
  for (int k = 0; k < 2; k++) {
    float y = wave - s->bore_hp_x[k] + s->bore_hp_r * s->bore_hp_y[k];
    s->bore_hp_x[k] = wave;
    s->bore_hp_y[k] = y;
    wave = y * s->bore_hp_norm;
  }

  // Turbulence where the air crosses the embouchure.  A waveguide is silent
  // until something disturbs it, so this is what the note grows out of.
  float eta = wave + p->jet_noise * synth_noise(s);

  // The jet, and the amplitude limit.  tanh's slope falls away as the loop
  // amplitude climbs, and the note settles where that slope has fallen far
  // enough to bring the loop gain back to exactly 1 -- so how big the
  // oscillation gets, and therefore how rich it is, is set by how far over
  // the threshold the breath is.  `bore_t0` is subtracted so the jet adds no
  // offset of its own for the high-pass to have to remove.
  float v = breath * s->bore_slope_inv;
  s->bore[s->bore_write] = -v * (tanhf(eta + p->jet_bias) - s->bore_t0);
  s->bore_write = (s->bore_write + 1) % SYNTH_BORE_MAX;

  // Scaled on the way out.  Not a level -- out_gain sets the level -- but a
  // ceiling on how hard the saturator downstream is driven: at full breath
  // the loop settles around 8, and a fifth of that puts the argument of the
  // atan at 0.25, where it makes a third harmonic 51dB under the fundamental.
  // Against a third partial the tube itself puts at -20dB that is nothing,
  // which is the point.  Every harmonic in this voice is supposed to have
  // come out of the jet.
  return at_end * 0.2f;
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
    } else {
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
  // A note that earned a tail freezes the gate once the player stops, and
  // keeps it frozen.  The latch is re-decided on every sample the player is
  // playing, so a guess that the note was ending costs nothing when it turns
  // out to be wrong -- the level comes back up and the note simply carries on.
  //
  // It is cleared while playing by a note that has not earned a tail, which is
  // what lets one short note end a drone: the short note takes the gate over,
  // and when it stops there is no longer anything holding it open.
  if (playing) {
    s->holding_tail = earned;
  } else if (!s->sustain || p->no_sustain) {
    // The control switched off, or the voice changed to one that opts out,
    // under a sounding tail.  Nothing else would ever clear the latch -- the
    // test above only runs while the player is playing -- so without this the
    // drone would be stuck on with no way to reach it.
    s->holding_tail = false;
  }
  if (playing || !s->holding_tail) {
    float gate_target = playing ? 1.0f : 0.0f;
    float gate_coeff = coeff(
        gate_target > s->gate ? p->attack_s : p->release_s, s->sample_rate);
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
  if (s->sustain) {
    s->amp *= 1 + settle * (SYNTH_HOLD_SUSTAIN - 1);
  }

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
  // The tube's length is a pitch, so it moves the way a pitch does rather
  // than stepping once a control hop.  Set outright the first time, since
  // sliding up from zero would sweep the note in from the top of the range.
  if (s->bore_len <= 0) {
    s->bore_len = s->bore_len_target;
  } else {
    s->bore_len += smooth * (s->bore_len_target - s->bore_len);
  }

  if (s->amp < 1e-5f && !hint->voiced) {
    s->side = 0;
    return 0;
  }

  // Where the note envelope gets applied.  Every voice but the waveguide is
  // an oscillator that sounds whether or not anyone asked, so its envelope
  // goes on at the bottom of this function; the waveguide's went in at the
  // top, as breath, so the multiply at the bottom is 1 for it.
  //
  // That leaves the air below with nothing to fade it.  Measured before this
  // split, `flute-jet` hissed through every rest at -48dB and then had the
  // hiss cut off dead by the silence check above -- a 133dB step, eight times
  // in ninety seconds of whistling.  So the air takes the envelope here
  // instead, which is where it belonged anyway: on a flute the same breath
  // makes the noise and the note.
  float env = p->jet_drive > 0 ? 1.0f : s->amp;
  float air_env = p->jet_drive > 0 ? s->amp : 1.0f;

  float f0 = exp2f(synth_pitch_log(s)) * synth_octave(s);
  float out = 0;
  // The same sum again, but weighted by where each copy sits.  Accumulated
  // here rather than reconstructed later because this is the only place the
  // copies exist separately -- one line down they are one signal.
  float spread = 0;
  if (p->jet_drive > 0) {
    // The envelope is the breath, and it goes in rather than being applied to
    // what comes out.  Everything the envelope carries goes in here: the note
    // starting and stopping, the articulation between tongued notes, and the
    // settle onto a sustained tail.
    //
    // A note at full envelope is blown at `jet_drive`, and one at nothing
    // falls back to SYNTH_BORE_REST -- air still moving, but not enough to
    // sound.  The threshold of oscillation is at 1, which this crosses at
    // 0.26 of the envelope, and that crossing is the note starting and
    // stopping.  It is a real threshold rather than a fade: under it the tube
    // makes no sound at all however much air is going past it, which is what
    // a flute does and is why this voice has no `attack_s` worth the name.
    // Compressed on the way in, and this curve is the one number in the
    // voice with no physics behind it -- it is a measured compromise.
    //
    // It matters more than a curve usually does, because a waveguide near its
    // threshold has almost no output at all: the whole musical range has to
    // fit between speaking and the top of the dynamics, and there is not much
    // room in there.  Straighten the curve and the voice gets its dynamics
    // back but the quiet end falls under the threshold and stops dead;
    // bend it further and the voice is reliable but barely responds to how
    // hard it is played.  Measured against `flute-low` on the same material
    // -- a 15dB crescendo on a held note, and the sustain tails in
    // recordings/holding.f32, whose whistle averages 0.033 against this
    // voice's level_full of 0.22:
    //
    //     exponent   crescendo   tail under the note   of the take sounding
    //       0.50        4.5dB          -2.0dB                  68%
    //       0.65        5.9dB          -4.3dB                  54%
    //       0.80        7.4dB         -32.2dB                  32%
    //       1.00        9.8dB          -8.1dB                  21%
    //     flute-low    11.2dB          -3.8dB                  69%
    //
    // 0.65 tracks `flute-low`'s tail most closely and keeps more of the
    // dynamics than 0.5 does.  What it costs is the quietest playing on an
    // under-level input, where this voice stops and `flute-low` only gets
    // quieter -- so it is more sensitive to the mic level than the rest of
    // the table, and that is a real difference rather than a tuning fault.
    // The direction of the curve is at least what the physics expects: what
    // the loop gain follows is the speed of the jet, and jet speed goes as
    // the square root of the pressure behind it.
    out = synth_bore(s, p, SYNTH_BORE_REST +
                     (p->jet_drive - SYNTH_BORE_REST) * powf(s->amp, 0.65f));
  } else {
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
    } else if (synth_needs_partial_phase(p)) {
      // Partials aren't multiples of anything, so each one carries its own
      // phase and costs a sinf.
      for (int i = 0; i < s->harmonics_active; i++) {
        float* ph = &s->partial_phase[u][i];
        *ph += base * synth_partial_ratio(p, i + 1) / s->sample_rate;
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
  // flute's low register runs 15-25dB and the breathy big ones about 10-15;
  // the concert flute in recordings/flute.f32 measures 25-30dB across its
  // middle register, which is what `flute` is set to.
  if (p->breath > 0) {
    float n = synth_noise(s);
    // Fixed in Hz, and measured that way.  The air was tracking the note at
    // three times the fundamental; across four notes an octave apart in
    // a descending octave of held notes and flute2.f32, the between-harmonic
    // noise does
    // not move with the pitch at all.  It is a plateau from about 1.5 to
    // 4kHz falling 18-20dB an octave above that, at every pitch played.  That
    // makes sense of where it comes from: the noise is made at the embouchure
    // and in the player's mouth, neither of which changes size with the note.
    //
    // The shape is a highpass for the low side and two two-pole lowpasses for
    // the top.  The top edge is the whole point.  A single state variable
    // bandpass falls at 6dB an octave, which left the synthesized air 30dB
    // hot at 12kHz against the real thing -- a flat hiss shelf across the
    // entire top of the spectrum, and much the most audible thing wrong with
    // the first version of this voice.  Fitted against the measured curve
    // this lands within 1.4dB rms from 2 to 12.7kHz.
    //
    // Nothing here resonates, because the measured air has no peak in it: it
    // is a plateau, and the earlier note here about one-poles only being able
    // to make slopes is answered by cascading a highpass with two lowpasses,
    // which makes a flat-topped band with steep edges.
    const float fc = p->breath_hz > 0 ? p->breath_hz : 1600.0f;
    const float q = 1.0f / 0.9f;
    float f = 2 * sinf((float)M_PI * fc / s->sample_rate);
    s->breath_lp += f * s->breath_bp;
    float high = n - s->breath_lp - q * s->breath_bp;
    s->breath_bp += f * high;

    // The highpass output, not the bandpass: the low side has to be steep
    // too.  Between the fundamental and the octave -- 794Hz on the C5 this
    // was fitted against -- the real air sits 9dB under the plateau, and a
    // bandpass's 6dB an octave left that band 11dB hot, which is audible as
    // roughness under the note rather than as air around it.
    float air = high;

    // Two state variable filters at their lowpass outputs for the top edge,
    // and it takes both.  Three cascaded one-poles were tried first and gave
    // 12.5dB an octave, because a one-pole's response flattens approaching
    // Nyquist: at this corner one stage is only 10dB down at 24kHz however
    // far past it you go, so stacking them buys far less than the 6dB an
    // octave each promises.  Fitted against the measured air this lands
    // within 2.1dB rms from 794Hz to 12.7kHz.
    // 1.75x the bottom corner, which is where the fit put it (1600 and 2800).
    // Tied together rather than set separately so that moving the band for a
    // larger instrument slides the shape instead of reshaping it.
    const float lp_hz = fc * 1.75f;
    const float q2 = 1.0f / 0.7f;
    float f2 = 2 * sinf((float)M_PI * lp_hz / s->sample_rate);
    // Chamberlin filters go unstable once f + 1/Q reaches 2, so pin it rather
    // than trusting the sample rate.
    if (f2 > 2.0f - q2 - 0.05f) {
      f2 = 2.0f - q2 - 0.05f;
    }
    for (int stage = 0; stage < 2; stage++) {
      float* slp = &s->breath_post[stage][0];
      float* sbp = &s->breath_post[stage][1];
      *slp += f2 * *sbp;
      float high2 = air - *slp - q2 * *sbp;
      *sbp += f2 * high2;
      air = *slp;
    }

    // Modulated by the tone, but only slightly.  The jet at the embouchure
    // makes both the note and the noise, so some correlation is right and it
    // is what puts the noise around the harmonics rather than under them --
    // but at any depth worth noticing it stops being a flute and becomes a
    // snare, which is a band of noise switched on and off at a low pitch.
    // 15% is enough to bind the air to the note and not enough to rattle.
    //
    // A flat fraction of the tone, with no dynamics term.  Measured over a
    // real crescendo -- 15dB of it on one held note -- the tone-to-noise
    // ratio does not move: 25.3dB at the quietest, 24.8dB at the loudest.
    // The air is made by the same jet as the note and scales with it.
    out += air * p->breath * air_env * (0.9f + 0.15f * out);
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

  // Same envelope and gain as the mid, so width changes where a note sits
  // and not how loud it is.  It skips the high-pass: only single-oscillator
  // voices ask for one, and those have no spread to begin with.
  //
  // Left is mid+side and right is mid-side, so the two channels sum back to
  // exactly twice the mono output -- a stereo image that folds down without
  // anything cancelling, which for detuned copies is otherwise exactly what
  // goes wrong.
  s->side = spread * drive_gain * p->stereo_width * env * p->out_gain *
            s->audibility_comp;

  return out * env * p->out_gain * s->audibility_comp;
}
