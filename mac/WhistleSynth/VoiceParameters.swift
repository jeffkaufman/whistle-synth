import Foundation

/// One editable field of `SynthParams`, described well enough that the UI can
/// be generated from a list of these rather than hand-written twenty-four
/// times over.
///
/// `id` is also the persistence key, so renaming one silently discards a
/// player's edit to that field.  Rename the `label` instead.
struct ParamSpec: Identifiable, Hashable {
    enum Group: String, CaseIterable, Identifiable {
        case pitch = "Pitch"
        case tone = "Tone"
        case movement = "Movement"
        case dynamics = "Dynamics"
        case output = "Output"

        var id: String { rawValue }

        var blurb: String {
            switch self {
            case .pitch: return "Where the whistle is resynthesized, and how it gets there."
            case .tone: return "The shape of the harmonic series the oscillator builds."
            case .movement: return "What keeps a held note from sounding like a test tone."
            case .dynamics: return "How the sound answers how hard you are blowing."
            case .output: return "Level."
            }
        }
    }

    let id: String
    let label: String
    let group: Group
    let range: ClosedRange<Double>
    /// Non-nil for the fields that are whole numbers in C.
    let step: Double?
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

private func whole(_ value: Double) -> String {
    String(Int(value.rounded()))
}

/// Octave is stored as a frequency multiplier, but nobody thinks in
/// multipliers -- the presets are all exact octaves, and semitones are what a
/// player would reach for to put a voice somewhere between two of them.
private func semitones(from multiplier: Double) -> Double {
    guard multiplier > 0 else { return -60 }
    return (log2(multiplier) * 12).rounded()
}

enum VoiceParameters {
    static let specs: [ParamSpec] = [
        ParamSpec(
            id: "octave", label: "Transpose", group: .pitch,
            range: -60...12, step: 1,
            help: "How far below the whistle the note is played. The lead sits an octave down; the bass four; sub-bass five.",
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
            id: "pwm_center", label: "Pulse width", group: .tone,
            range: 0.06...0.5, step: nil,
            help: "0.5 nulls the even partials and sounds hollow and square; narrower fills them back in and turns nasal and bright.",
            format: fixed(3),
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
            help: "How many partials to synthesize. Anything that would land above Nyquist is dropped anyway, so this only bites on the low voices.",
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
            id: "stretch", label: "Partial stretch", group: .tone,
            range: 0...0.05, step: nil,
            help: "Pushes the upper partials progressively sharp instead of onto exact multiples. On its own a static clang; through the drive it becomes a growl.",
            format: fixed(4),
            get: { Double($0.stretch) }, set: { $0.stretch = Float($1) }),
        ParamSpec(
            id: "min_partial_hz", label: "Low cutoff", group: .tone,
            range: 0...200, step: nil,
            help: "Partials below this are never synthesized, and the output is high-passed just under it. 0 disables both. Headroom spent below here only shifts woofers.",
            format: { $0 < 0.5 ? "off" : String(format: "%.0f Hz", $0) },
            get: { Double($0.min_partial_hz) }, set: { $0.min_partial_hz = Float($1) }),
        ParamSpec(
            id: "unison", label: "Unison voices", group: .tone,
            range: 1...3, step: 1,
            help: "Detuned copies of the oscillator. One is what a bass wants: detuned copies beat against each other, and down low that beating takes seconds.",
            format: whole,
            get: { Double($0.unison) }, set: { $0.unison = Int32($1.rounded()) }),
        ParamSpec(
            id: "stereo_width", label: "Stereo width", group: .tone,
            range: 0...1, step: nil,
            help: "How far apart the unison copies are placed across the stereo field. Real stereo, not a widener: the copies are genuinely different signals. A voice running one copy stays centred whatever this says, which is what a bass wants.",
            format: { $0 < 0.005 ? "centred" : String(format: "%.0f%%", $0 * 100) },
            get: { Double($0.stereo_width) }, set: { $0.stereo_width = Float($1) }),
        ParamSpec(
            id: "detune_cents", label: "Detune", group: .tone,
            range: 0...50, step: nil,
            help: "How far apart the unison voices sit. No effect with one voice.",
            format: { String(format: "%.1f ¢", $0) },
            get: { Double($0.detune_cents) }, set: { $0.detune_cents = Float($1) }),

        ParamSpec(
            id: "pwm_slow_hz", label: "Width sweep rate", group: .movement,
            range: 0...5, step: nil,
            help: "The slow pulse-width drift that keeps a held note moving.",
            format: { String(format: "%.2f Hz", $0) },
            get: { Double($0.pwm_slow_hz) }, set: { $0.pwm_slow_hz = Float($1) }),
        ParamSpec(
            id: "pwm_slow_depth", label: "Width sweep depth", group: .movement,
            range: 0...0.3, step: nil,
            help: "How far that drift travels. Brass barely chorus, so the trombone keeps this small.",
            format: fixed(3),
            get: { Double($0.pwm_slow_depth) }, set: { $0.pwm_slow_depth = Float($1) }),
        ParamSpec(
            id: "growl_hz", label: "Growl rate", group: .movement,
            range: 0...20, step: nil,
            help: "A faster width wobble that fades in over a long note.",
            format: { String(format: "%.2f Hz", $0) },
            get: { Double($0.growl_hz) }, set: { $0.growl_hz = Float($1) }),
        ParamSpec(
            id: "growl_depth", label: "Growl depth", group: .movement,
            range: 0...0.3, step: nil,
            help: "How strong the growl gets. Pushed hard it stops reading as a growl and starts reading as distortion.",
            format: fixed(3),
            get: { Double($0.growl_depth) }, set: { $0.growl_depth = Float($1) }),
        ParamSpec(
            id: "growl_onset_s", label: "Growl onset", group: .movement,
            range: 0.01...3, step: nil,
            help: "How long a note has to be held before the growl is fully in. Driven by note length, not level, so runs stay clean and long notes grow a growl.",
            format: { String(format: "%.2f s", $0) },
            get: { Double($0.growl_onset_s) }, set: { $0.growl_onset_s = Float($1) }),

        ParamSpec(
            id: "level_full", label: "Full-blow level", group: .dynamics,
            range: 0.01...1, step: nil,
            help: "The input level that counts as playing as hard as you are going to. Whistle your loudest in the Play tab and set this to the peak it shows.",
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
            id: "attack_s", label: "Attack", group: .dynamics,
            range: 0.001...0.2, step: nil,
            help: "How fast a note arrives.",
            format: milliseconds,
            get: { Double($0.attack_s) }, set: { $0.attack_s = Float($1) }),
        ParamSpec(
            id: "release_s", label: "Release", group: .dynamics,
            range: 0.005...0.5, step: nil,
            help: "How fast a note leaves. Nothing here gates hard, so an uncertain detector costs a fade rather than a click.",
            format: milliseconds,
            get: { Double($0.release_s) }, set: { $0.release_s = Float($1) }),
        ParamSpec(
            id: "articulation_s", label: "Articulation", group: .dynamics,
            range: 0.001...0.1, step: nil,
            help: "How fast the level may move within a note. This is the difference between a run of tongued notes and one long smear, and it has to be much quicker than the release.",
            format: milliseconds,
            get: { Double($0.articulation_s) }, set: { $0.articulation_s = Float($1) }),

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
