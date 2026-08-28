import Foundation

/// One editable field of `SynthParams`, described well enough that the UI can
/// be generated from a list of these rather than hand-written fifty times
/// over.
///
/// `id` is also the persistence key, so renaming one silently discards a
/// player's edit to that field.  Rename the `label` instead.
struct ParamSpec: Identifiable, Hashable {
    enum Group: String, CaseIterable, Identifiable {
        case pitch = "Pitch"
        case tone = "Tone"
        case fm = "FM"
        case stack = "Octave stack"
        case movement = "Movement"
        case note = "Note shape"
        case dynamics = "Dynamics"
        case output = "Output"

        var id: String { rawValue }

        var blurb: String {
            switch self {
            case .pitch:
                return "Where the whistle is resynthesized, and how it gets there."
            case .tone:
                return "The shape of the harmonic series the oscillator builds."
            case .fm:
                return "Two-operator FM, which replaces the pulse oscillator entirely when the index is above zero. Brightness here moves energy around by Bessel functions rather than uncovering partials in order, which is a sound no filter setting reaches."
            case .stack:
                return "Partials an octave apart instead of a harmonic series, weighted by a bell fixed in Hz. Off at zero. When it is on it replaces pulse width, tilt, brightness and resonance, because a fixed-in-Hz envelope is the only shape that survives the wrap."
            case .movement:
                return "What keeps a held note from sounding like a test tone."
            case .note:
                return "The shape of a single note, beyond starting and stopping."
            case .dynamics:
                return "How the sound answers how hard you are blowing."
            case .output:
                return "Level."
            }
        }
    }

    let id: String
    let label: String
    let group: Group
    let range: ClosedRange<Double>
    /// Non-nil for the fields that are whole numbers in C.
    let step: Double?
    /// True for the fields that are `bool` in C, which get a switch rather
    /// than a two-position slider.
    var isToggle: Bool = false
    let help: String
    let format: (Double) -> String
    let get: (SynthParams) -> Double
    let set: (inout SynthParams, Double) -> Void

    static func == (lhs: ParamSpec, rhs: ParamSpec) -> Bool { lhs.id == rhs.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

private func fixed(_ places: Int) -> (Double) -> String {
    { String(format: "%.\(places)f", $0) }
}

private func milliseconds(_ value: Double) -> String {
    String(format: "%.0f ms", value * 1000)
}

private func seconds(_ value: Double) -> String {
    String(format: "%.2f s", value)
}

private func hertz(_ value: Double) -> String {
    String(format: "%.2f Hz", value)
}

private func whole(_ value: Double) -> String {
    String(Int(value.rounded()))
}

/// For the fields where zero is not a small value but a switch: reading "off"
/// is the difference between a slider parked at the bottom and one that is
/// doing nothing at all.
private func offAtZero(_ inner: @escaping (Double) -> String) -> (Double) -> String {
    { $0 <= 0 ? "off" : inner($0) }
}

/// Octave is stored as a frequency multiplier, but nobody thinks in
/// multipliers -- the presets are all exact octaves, and semitones are what a
/// player would reach for to put a voice somewhere between two of them.
private func semitones(from multiplier: Double) -> Double {
    guard multiplier > 0 else { return -96 }
    return (log2(multiplier) * 12).rounded()
}

enum VoiceParameters {
    static let specs: [ParamSpec] = [
        ParamSpec(
            id: "octave", label: "Transpose", group: .pitch,
            // Down to eight octaves, which is not a register but what
            // `octaveless` needs: there the fundamental is the *spacing* of
            // the stack rather than a pitch, and it sits at 2-12Hz.
            range: -96...12, step: 1,
            help: "How far below the whistle the note is played. The bass voices sit four octaves down, where an electric bass actually plays; the sub voices five.",
            format: { value in
                let steps = Int(value.rounded())
                if steps % 12 == 0 {
                    return "\(steps / 12) oct"
                }
                return "\(steps) st"
            },
            get: { semitones(from: Double($0.octave)) },
            set: { $0.octave = Float(pow(2, $1 / 12)) }),
        ParamSpec(
            id: "glide_s", label: "Glide", group: .pitch,
            range: 0...0.1, step: nil,
            help: "How long the pitch takes to catch up within a note. Also smooths the detector's hop-to-hop jitter, so very low is not automatically better.",
            format: milliseconds,
            get: { Double($0.glide_s) }, set: { $0.glide_s = Float($1) }),
        ParamSpec(
            id: "drop_octaves", label: "Pitch drop", group: .pitch,
            range: 0...2, step: nil,
            help: "How far sharp each note starts before falling to pitch. This is the whole of an 808: the drop is what a drum machine put there to fake the thump of a skin, and without it the voice is just a sub.",
            format: offAtZero({ String(format: "%.2f oct", $0) }),
            get: { Double($0.drop_octaves) }, set: { $0.drop_octaves = Float($1) }),
        ParamSpec(
            id: "drop_s", label: "Pitch drop time", group: .pitch,
            range: 0...0.5, step: nil,
            help: "How fast it falls. A few tens of milliseconds is a thump; a second is a hoover's swoop. Same mechanism, two settings.",
            format: milliseconds,
            get: { Double($0.drop_s) }, set: { $0.drop_s = Float($1) }),

        ParamSpec(
            id: "pwm_center", label: "Pulse width", group: .tone,
            // Down to zero rather than to a usable floor, because the voices
            // that replace the pulse oscillator outright -- FM, the octave
            // stack -- leave this unset, and a slider parked at its own floor
            // would report 0.060 for a field that is actually 0.
            range: 0...0.5, step: nil,
            help: "0.5 nulls the even partials and sounds hollow and square; narrower fills them back in and turns nasal and bright. A voice whose sound comes from FM or from the octave stack does not use the pulse oscillator at all and leaves this unused.",
            format: { $0 < 0.005 ? "unused" : fixed(3)($0) },
            get: { Double($0.pwm_center) }, set: { $0.pwm_center = Float($1) }),
        ParamSpec(
            id: "tilt", label: "Spectral tilt", group: .tone,
            range: 0...2, step: nil,
            help: "Exponent on the 1/n of the pulse series. 1.0 is a true pulse wave; lower flattens the spectrum and moves the energy above the fundamental, which is much of what brass sounds like.",
            format: fixed(2),
            get: { Double($0.tilt) }, set: { $0.tilt = Float($1) }),
        ParamSpec(
            id: "harmonics", label: "Partials", group: .tone,
            range: 1...32, step: 1,
            help: "How many partials to synthesize. Anything that would land above Nyquist is dropped anyway, so this only bites on the low voices — and they need the count: four octaves down, twelve partials only reaches 2kHz and sounds like a blanket.",
            format: whole,
            get: { Double($0.harmonics) }, set: { $0.harmonics = Int32($1.rounded()) }),
        ParamSpec(
            id: "cutoff_soft", label: "Brightness, soft", group: .tone,
            range: 0.5...32, step: nil,
            help: "Where the harmonic rolloff is 3dB down when you back off, counted in partials.",
            format: fixed(2),
            get: { Double($0.cutoff_soft) }, set: { $0.cutoff_soft = Float($1) }),
        ParamSpec(
            id: "cutoff_loud", label: "Brightness, loud", group: .tone,
            range: 0.5...32, step: nil,
            help: "The same, when you lean in. The distance between the two is how much the voice opens up as it is pushed.",
            format: fixed(2),
            get: { Double($0.cutoff_loud) }, set: { $0.cutoff_loud = Float($1) }),
        ParamSpec(
            id: "rolloff_exp", label: "Rolloff slope", group: .tone,
            range: 0.5...8, step: nil,
            help: "How steeply it falls past the cutoff: 2 behaves like a 2-pole filter, 4 like a 4-pole.",
            format: fixed(1),
            get: { Double($0.rolloff_exp) }, set: { $0.rolloff_exp = Float($1) }),
        ParamSpec(
            id: "resonance", label: "Resonance", group: .tone,
            range: 0...12, step: nil,
            help: "A peak sitting on the rolloff at the cutoff. This is the difference between a filter and a tone control: without it a brightness sweep only gets brighter and darker, and with it the sweep is audible as a filter moving.",
            format: offAtZero(fixed(2)),
            get: { Double($0.resonance) }, set: { $0.resonance = Float($1) }),
        ParamSpec(
            id: "resonance_width", label: "Resonance width", group: .tone,
            // Zero is "unset", not "infinitely narrow": the engine fills in a
            // default, and only when there is a resonance to shape.  A floor
            // above zero would report that default's absence as a real value.
            range: 0...2, step: nil,
            help: "How wide that peak is, in octaves. Narrow is a whistle; wide is a hump. Only means anything with a resonance set.",
            format: { $0 <= 0 ? "auto (0.25 oct)" : String(format: "%.2f oct", $0) },
            get: { Double($0.resonance_width) }, set: { $0.resonance_width = Float($1) }),
        ParamSpec(
            id: "stretch", label: "Partial stretch", group: .tone,
            range: 0...0.05, step: nil,
            help: "Pushes the upper partials progressively sharp instead of onto exact multiples. On its own a static clang; through the drive it becomes a growl, because the difference tones land a few Hz from the harmonics.",
            format: fixed(4),
            get: { Double($0.stretch) }, set: { $0.stretch = Float($1) }),
        ParamSpec(
            id: "min_partial_hz", label: "Low cutoff", group: .tone,
            range: 0...200, step: nil,
            help: "Partials below this are never synthesized, and the output is high-passed just under it. 0 disables both. Headroom spent below here only shifts woofers.",
            format: offAtZero({ String(format: "%.0f Hz", $0) }),
            get: { Double($0.min_partial_hz) }, set: { $0.min_partial_hz = Float($1) }),
        ParamSpec(
            id: "unison", label: "Unison voices", group: .tone,
            range: 1...3, step: 1,
            help: "Detuned copies of the oscillator. One is what most basses want: detuned copies beat against each other, and down low that beating takes seconds. `reese` ignores that on purpose — there the smear is the patch.",
            format: whole,
            get: { Double($0.unison) }, set: { $0.unison = Int32($1.rounded()) }),
        ParamSpec(
            id: "detune_cents", label: "Detune", group: .tone,
            range: 0...50, step: nil,
            help: "How far apart the unison voices sit. No effect with one voice.",
            format: { String(format: "%.1f ¢", $0) },
            get: { Double($0.detune_cents) }, set: { $0.detune_cents = Float($1) }),
        ParamSpec(
            id: "mono_partials", label: "Mono partials", group: .tone,
            range: 0...8, step: 1,
            help: "How many of the lowest partials come from one copy instead of all of them. Beating is cancellation: three copies nine cents apart swing the fundamental over 31dB, which in stereo is a Reese and in mono is the bass falling out of the tune. Taking the bottom partial or two from one copy keeps the low end steady and leaves the churn in the harmonics.",
            format: { $0 < 0.5 ? "all copies" : whole($0) },
            get: { Double($0.mono_partials) },
            set: { $0.mono_partials = Int32($1.rounded()) }),
        ParamSpec(
            id: "stereo_width", label: "Stereo width", group: .tone,
            range: 0...1, step: nil,
            help: "How far apart the unison copies are placed across the stereo field. Real stereo, not a widener: the copies are genuinely different signals. A voice running one copy stays centred whatever this says, which is what a bass wants.",
            format: { $0 < 0.005 ? "centred" : String(format: "%.0f%%", $0 * 100) },
            get: { Double($0.stereo_width) }, set: { $0.stereo_width = Float($1) }),

        ParamSpec(
            id: "fm_ratio", label: "Modulator ratio", group: .fm,
            range: 0...8, step: nil,
            help: "Where the modulator sits, as a multiple of the fundamental. Whole numbers give a harmonic spectrum; anything else is a bell or a clang.",
            format: fixed(2),
            get: { Double($0.fm_ratio) }, set: { $0.fm_ratio = Float($1) }),
        ParamSpec(
            id: "fm_index_soft", label: "Index, soft", group: .fm,
            range: 0...8, step: nil,
            help: "How many radians of phase the modulator pushes the carrier through when you back off. Zero at both ends leaves FM switched off and the pulse oscillator in charge.",
            format: offAtZero(fixed(2)),
            get: { Double($0.fm_index_soft) }, set: { $0.fm_index_soft = Float($1) }),
        ParamSpec(
            id: "fm_index_loud", label: "Index, loud", group: .fm,
            range: 0...8, step: nil,
            help: "The same, when you lean in. Index takes the place of brightness here — partials rise and fall out of order and some of them invert on the way, which is what a DX bass sounds like.",
            format: offAtZero(fixed(2)),
            get: { Double($0.fm_index_loud) }, set: { $0.fm_index_loud = Float($1) }),

        ParamSpec(
            id: "octave_stack_hz", label: "Bell centre", group: .stack,
            range: 0...400, step: nil,
            help: "Where the loudness bell sits, in Hz, and the switch for the whole stack. Whistling an octave higher slides every component one slot along a curve that has not moved, so the spectrum repeats exactly and the voice has no octave: a Shepard tone you can play.",
            format: offAtZero({ String(format: "%.0f Hz", $0) }),
            get: { Double($0.octave_stack_hz) },
            set: { $0.octave_stack_hz = Float($1) }),
        ParamSpec(
            id: "octave_stack_width", label: "Bell width", group: .stack,
            range: 0...3, step: nil,
            help: "How wide the bell is, in octaves. It has to fit inside the stack with room at both ends, or the tails run off an end and the wrap stops being seamless. Only means anything with a bell centre set.",
            format: { $0 <= 0 ? "auto (1.00 oct)" : String(format: "%.2f oct", $0) },
            get: { Double($0.octave_stack_width) },
            set: { $0.octave_stack_width = Float($1) }),
        ParamSpec(
            id: "octave_stack_track", label: "Bell tracking", group: .stack,
            range: 0...1, step: nil,
            help: "How far the bell follows the pitch, in octaves per octave. 0 is the pure Shepard, with no register at all. At 0.5 an octave of whistle moves the bass a fifth, so the line keeps real melodic contour while staying inside the window a PA can reproduce; the timbre still repeats exactly, just every two octaves instead of every one.",
            format: { $0 <= 0 ? "none (pure Shepard)" : fixed(2)($0) },
            get: { Double($0.octave_stack_track) },
            set: { $0.octave_stack_track = Float($1) }),
        ParamSpec(
            id: "octave_stack_ref_hz", label: "Tracking reference", group: .stack,
            range: 0...3200, step: nil,
            help: "The whistled pitch at which the bell sits exactly where its centre puts it. Only means anything when tracking is above zero.",
            format: { $0 < 1 ? "auto (1000 Hz)" : String(format: "%.0f Hz", $0) },
            get: { Double($0.octave_stack_ref_hz) },
            set: { $0.octave_stack_ref_hz = Float($1) }),

        ParamSpec(
            id: "pwm_slow_hz", label: "Width sweep rate", group: .movement,
            range: 0...5, step: nil,
            help: "The slow pulse-width drift that keeps a held note moving.",
            format: offAtZero(hertz),
            get: { Double($0.pwm_slow_hz) }, set: { $0.pwm_slow_hz = Float($1) }),
        ParamSpec(
            id: "pwm_slow_depth", label: "Width sweep depth", group: .movement,
            range: 0...0.3, step: nil,
            help: "How far that drift travels. A wobbling bass fights a piano's left hand and turns to mud in a PA, so the bass voices keep this small.",
            format: fixed(3),
            get: { Double($0.pwm_slow_depth) }, set: { $0.pwm_slow_depth = Float($1) }),
        ParamSpec(
            id: "growl_hz", label: "Growl rate", group: .movement,
            range: 0...20, step: nil,
            help: "A faster width wobble that fades in over a long note.",
            format: offAtZero(hertz),
            get: { Double($0.growl_hz) }, set: { $0.growl_hz = Float($1) }),
        ParamSpec(
            id: "growl_depth", label: "Growl depth", group: .movement,
            range: 0...0.3, step: nil,
            help: "How strong the growl gets. Pushed hard it stops reading as a growl and starts reading as distortion — which a sub-bass least wants, because down there the ear takes any harshness in the partials as the whole character.",
            format: fixed(3),
            get: { Double($0.growl_depth) }, set: { $0.growl_depth = Float($1) }),
        ParamSpec(
            id: "growl_onset_s", label: "Growl onset", group: .movement,
            range: 0...3, step: nil,
            help: "How long a note has to be held before the growl is fully in. Driven by note length, not level, so runs stay clean and long notes grow a growl. Zero means it is there from the attack.",
            format: { $0 <= 0 ? "immediate" : seconds($0) },
            get: { Double($0.growl_onset_s) }, set: { $0.growl_onset_s = Float($1) }),
        ParamSpec(
            id: "wobble_hz", label: "Filter wobble rate", group: .movement,
            range: 0...12, step: nil,
            help: "A free-running LFO on the cutoff, rather than on the pulse width.",
            format: offAtZero(hertz),
            get: { Double($0.wobble_hz) }, set: { $0.wobble_hz = Float($1) }),
        ParamSpec(
            id: "wobble_octaves", label: "Filter wobble depth", group: .movement,
            range: 0...2, step: nil,
            help: "How far it travels, in octaves of cutoff. Zero switches it off whatever the rate says.",
            format: offAtZero({ String(format: "%.2f oct", $0) }),
            get: { Double($0.wobble_octaves) },
            set: { $0.wobble_octaves = Float($1) }),
        ParamSpec(
            id: "vibrato_hz", label: "Vibrato rate", group: .movement,
            range: 0...12, step: nil,
            help: "Pitch vibrato, which fades in over a held note the way the growl does.",
            format: offAtZero(hertz),
            get: { Double($0.vibrato_hz) }, set: { $0.vibrato_hz = Float($1) }),
        ParamSpec(
            id: "vibrato_cents", label: "Vibrato depth", group: .movement,
            range: 0...100, step: nil,
            help: "How far it swings. Zero switches it off whatever the rate says.",
            format: offAtZero({ String(format: "%.0f ¢", $0) }),
            get: { Double($0.vibrato_cents) }, set: { $0.vibrato_cents = Float($1) }),
        ParamSpec(
            id: "vibrato_onset_s", label: "Vibrato onset", group: .movement,
            range: 0...3, step: nil,
            help: "How long a note has to be held before the vibrato is fully in. Zero means it is there from the attack.",
            format: seconds,
            get: { Double($0.vibrato_onset_s) },
            set: { $0.vibrato_onset_s = Float($1) }),
        ParamSpec(
            id: "shimmer_depth", label: "Tail movement", group: .movement,
            range: 0...1, step: nil,
            help: "How deep the movement is in a sustained tail, overriding the default. Only exists when the sustain switch on the Play tab is on and a note has been held long enough to earn a tail; it moves the balance between partials rather than the level. A voice whose partials are an octave apart moves very differently under the same number from one whose partials are a harmonic series.",
            format: { $0 <= 0 ? "default (0.35)" : fixed(2)($0) },
            get: { Double($0.shimmer_depth) }, set: { $0.shimmer_depth = Float($1) }),
        ParamSpec(
            id: "wander_db", label: "Partial wander", group: .movement,
            range: 0...6, step: nil,
            help: "How much each partial's level drifts on its own while a note is sounding, at the top of the spectrum. Unlike every other movement here it does not move the partials together: each one goes its own way, so the balance between them keeps changing while the total level stays put. That is what a held flute note does, and it is the difference between an instrument and an oscillator. Turning it up past what was measured (2.5) starts to sound like a bad tape rather than a player.",
            format: offAtZero({ String(format: "%.1f dB", $0) }),
            get: { Double($0.wander_db) }, set: { $0.wander_db = Float($1) }),
        ParamSpec(
            id: "wander_hz", label: "Wander rate", group: .movement,
            range: 0.5...15, step: nil,
            help: "How fast that drifting is, as a corner rather than a rate: the movement is noise shaped by this, not an LFO at this frequency, so there is no speed to hear in it. Low is a note that swells and sags; high stops reading as a player and starts reading as roughness.",
            format: { String(format: "%.1f Hz", $0) },
            get: { Double($0.wander_hz) }, set: { $0.wander_hz = Float($1) }),

        ParamSpec(
            id: "attack_s", label: "Attack", group: .note,
            range: 0.001...0.2, step: nil,
            help: "How fast a note arrives.",
            format: milliseconds,
            get: { Double($0.attack_s) }, set: { $0.attack_s = Float($1) }),
        ParamSpec(
            id: "decay_s", label: "Decay", group: .note,
            range: 0...2, step: nil,
            help: "How fast a note falls from its attack to a held level. Off, the voice sounds for exactly as long as you whistle, which is an organ; a plucked bass speaks hard and gets out of the way, so the pulse comes from the attacks rather than from the gaps.",
            format: offAtZero(milliseconds),
            get: { Double($0.decay_s) }, set: { $0.decay_s = Float($1) }),
        ParamSpec(
            id: "sustain_level", label: "Sustain level", group: .note,
            range: 0...1, step: nil,
            help: "Where the decay lands, as a fraction of the peak. Only means anything with a decay set.",
            format: fixed(2),
            get: { Double($0.sustain_level) }, set: { $0.sustain_level = Float($1) }),
        ParamSpec(
            id: "release_s", label: "Release", group: .note,
            range: 0.005...0.5, step: nil,
            help: "How fast a note leaves. Nothing here gates hard, so an uncertain detector costs a fade rather than a click.",
            format: milliseconds,
            get: { Double($0.release_s) }, set: { $0.release_s = Float($1) }),
        ParamSpec(
            id: "articulation_s", label: "Articulation", group: .note,
            range: 0.001...0.1, step: nil,
            help: "How fast the level may move within a note. This is the difference between a run of tongued notes and one long smear, and it has to be much quicker than the release.",
            format: milliseconds,
            get: { Double($0.articulation_s) }, set: { $0.articulation_s = Float($1) }),
        ParamSpec(
            id: "cutoff_env_octaves", label: "Filter envelope", group: .note,
            range: 0...4, step: nil,
            help: "How far above its resting place each note's cutoff starts, in octaves. The filter falling with the note is what a plucked string does, and it is what makes an attack read as an attack rather than as a volume bump.",
            format: offAtZero({ String(format: "%.2f oct", $0) }),
            get: { Double($0.cutoff_env_octaves) },
            set: { $0.cutoff_env_octaves = Float($1) }),
        ParamSpec(
            id: "cutoff_env_s", label: "Filter envelope time", group: .note,
            range: 0...1, step: nil,
            help: "How fast it falls back.",
            format: milliseconds,
            get: { Double($0.cutoff_env_s) }, set: { $0.cutoff_env_s = Float($1) }),
        ParamSpec(
            id: "no_sustain", label: "Ignore the sustain switch", group: .note,
            range: 0...1, step: 1, isToggle: true,
            help: "Opts this voice out of the Play tab's sustain entirely, so switching that on leaves this voice exactly as it is. For a voice whose whole shape is a note speaking and getting out of the way, a tail is the opposite instruction and what comes out is neither.",
            format: { $0 > 0.5 ? "opted out" : "follows it" },
            get: { $0.no_sustain ? 1 : 0 },
            set: { $0.no_sustain = $1 > 0.5 }),

        ParamSpec(
            id: "level_full", label: "Full-blow level", group: .dynamics,
            range: 0.01...1, step: nil,
            help: "The input level that counts as playing as hard as you are going to. Whistle your loudest and set this to what \"While playing\" reads on the Play tab — not the input meter, which measures something else.",
            format: fixed(3),
            get: { Double($0.level_full) }, set: { $0.level_full = Float($1) }),
        ParamSpec(
            id: "drive_soft", label: "Drive, soft", group: .dynamics,
            range: 0...8, step: nil,
            help: "Saturation when you back off.",
            format: fixed(2),
            get: { Double($0.drive_soft) }, set: { $0.drive_soft = Float($1) }),
        ParamSpec(
            id: "drive_loud", label: "Drive, loud", group: .dynamics,
            range: 0...8, step: nil,
            help: "Saturation when you lean in.",
            format: fixed(2),
            get: { Double($0.drive_loud) }, set: { $0.drive_loud = Float($1) }),
        ParamSpec(
            id: "drive_bias", label: "Drive bias", group: .dynamics,
            range: 0...2, step: nil,
            help: "Pushes the signal off centre before it saturates, which is the difference between a transistor and a valve: an odd function can only make odd harmonics however hard it is driven, and the bias is what puts the even ones in. Those land an octave up, in the gap between a bass and a mandolin's low G. Adds no DC — the saturator's value at the offset is subtracted back out.",
            format: offAtZero(fixed(2)),
            get: { Double($0.drive_bias) }, set: { $0.drive_bias = Float($1) }),
        ParamSpec(
            id: "breath", label: "Breath noise", group: .dynamics,
            range: 0...1, step: nil,
            help: "Noise banded around the note, as a fraction of the tone, and louder when you back off. That is how a large flute actually behaves: on a contrabass flute the breath is nearly as loud as the note.",
            format: offAtZero(fixed(3)),
            get: { Double($0.breath) }, set: { $0.breath = Float($1) }),

        ParamSpec(
            id: "out_gain", label: "Output gain", group: .output,
            range: 0...2, step: nil,
            help: "Not a taste control: each preset's value is set so every voice measures the same loudness (ITU-R BS.1770). Changing one means re-running the loudness match.",
            format: fixed(3),
            get: { Double($0.out_gain) }, set: { $0.out_gain = Float($1) }),
    ]

    static func specs(in group: ParamSpec.Group) -> [ParamSpec] {
        specs.filter { $0.group == group }
    }

    static let byID: [String: ParamSpec] =
        Dictionary(uniqueKeysWithValues: specs.map { ($0.id, $0) })

    /// The built-in values for a voice, untouched by anything a player has done.
    static func defaults(forVoice voice: Int) -> SynthParams {
        var params = SynthParams()
        whistle_preset_defaults(Int32(voice), &params)
        return params
    }

    /// Applies stored edits on top of the built-in values.  Unknown keys are
    /// ignored, so a preference file written by a newer version does not stop
    /// an older one from starting.
    static func params(forVoice voice: Int, overrides: [String: Double]) -> SynthParams {
        var params = defaults(forVoice: voice)
        for (id, value) in overrides {
            guard let spec = byID[id] else { continue }
            spec.set(&params, min(max(value, spec.range.lowerBound), spec.range.upperBound))
        }
        return params
    }
}
