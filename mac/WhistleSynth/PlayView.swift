import SwiftUI

struct PlayView: View {
    @EnvironmentObject private var synth: SynthController

    /// A voice may opt out of the sustain entirely (`no_sustain`), and
    /// silently doing nothing is exactly how a switch gets a reputation for
    /// being broken.  Read from the live parameters rather than from a list
    /// of voice names, so an edited voice reports what it will actually do.
    private var sustainExemption: String {
        guard !synth.isPassthrough, synth.currentParams.no_sustain else { return "" }
        return " \(synth.currentVoiceDisplayName) opts out of this: its whole shape is a note speaking and getting out of the way, and a tail is the opposite instruction."
    }

    var body: some View {
        Form {
            Section {
                Picker("Voice", selection: $synth.voice) {
                    ForEach(0..<synth.voiceCount, id: \.self) { voice in
                        Text(SynthController.displayName(ofVoice: voice))
                            .tag(voice)
                    }
                }
                .pickerStyle(.radioGroup)
            } header: {
                Text("Voice")
            } footer: {
                Text(synth.isPassthrough
                     ? "Passthrough plays the microphone straight back, for setting the input level and the gate."
                     : "Edit this voice's sound in the Voice tab.")
                .foregroundStyle(.secondary)
            }

            Section("Level") {
                StepSlider(title: "Volume", value: $synth.volume,
                           caption: "Roughly 3.5 dB a step.")
                StepSlider(title: "Gate", value: $synth.gate,
                           caption: "How far above the room a note has to be before it counts. Higher gates less.")
            }

            // Not in the Voice tab, because neither is a property of the
            // patch: which voice you are playing and what interval you are
            // playing it at are separate decisions, and so is whether the
            // line breathes with you or carries through.  Both stay put
            // across a voice change and apply to all of them.
            Section {
                Toggle("Down a fifth", isOn: $synth.fifth)
                Text("What you whistle becomes the fifth of what you hear rather than the root, so a tune whistled in D comes out in G. A just fifth, two cents flat of a tempered one.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)

                Toggle("Sustain held notes", isOn: $synth.sustain)
                Text("A note you hold for half a second slides onto the nearest real note, settles under itself and stays there — so one note every couple of bars holds a drone under the tune. It does not time out: one short note ends it, a long one moves it. Shorter notes are untouched, so a fast phrase sounds the same either way. Still monophonic: a new note takes the tail with it.\(sustainExemption)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } header: {
                Text("Playing")
            }

            Section("Listening") {
                StatRow(title: "Input") {
                    MeterView(level: synth.inputPeak, warn: 0.9)
                }
                StatRow(title: "Output") {
                    MeterView(level: synth.outputPeak, warn: 0.98)
                }
                StatRow(title: "While playing") {
                    Text(synth.playingLevel > 0.0005
                         ? String(format: "%.3f", synth.playingLevel)
                         : "—")
                        .font(.system(.body, design: .monospaced))
                }
                Text("What the detector heard while a note was actually sounding, which is the number the Voice tab's full-blow level is compared against — not the input meter above, which is a sample peak and counts the room between notes. Whistle your loudest and set full-blow level to what this reads.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                StatRow(title: "Detected") {
                    Text(synth.detectedNote ?? "—")
                        .font(.system(.body, design: .monospaced))
                        .foregroundStyle(synth.voiced ? .primary : .secondary)
                }
                StatRow(title: "Pitch") {
                    Text(synth.voiced
                         ? String(format: "%.1f Hz", synth.detectedHz)
                         : "—")
                        .font(.system(.body, design: .monospaced))
                        .foregroundStyle(synth.voiced ? .primary : .secondary)
                }
            }
        }
        .formStyle(.grouped)
    }
}

/// The 0-9 knobs, kept as 0-9 because that is what they have always been and
/// because ten steps is the right number for something adjusted while playing.
struct StepSlider: View {
    let title: String
    @Binding var value: Int
    let caption: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title)
                Spacer()
                Text("\(value)")
                    .font(.system(.body, design: .monospaced))
                    .foregroundStyle(.secondary)
            }
            Slider(
                value: Binding(get: { Double(value) },
                               set: { value = Int($0.rounded()) }),
                in: 0...9, step: 1)
                // Unlabelled, VoiceOver reads out a percentage and
                // nothing else -- true of every slider in the app.
                .accessibilityLabel(title)
                .accessibilityValue("\(value) of 9")
            Text(caption)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

/// Stands in for `LabeledContent`, which cannot be used here.
///
/// `LabeledContent` inside a grouped `Form` inside a `TabView` puts SwiftUI's
/// attribute graph into a cycle on macOS 26 the moment a tab is re-inserted --
/// switch to another tab and back and it starts spinning.  What actually
/// freezes the app is the *reporting*: AttributeGraph writes several million
/// "cycle detected" lines a second to stderr, which is a synchronous write on
/// the main thread.  Bisected to this view and nothing else; every other part
/// of these two tabs is innocent, and a plain HStack does the same job.
struct StatRow<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        HStack {
            Text(title)
            Spacer(minLength: 12)
            content
                // What LabeledContent would have done to the value side.
                .foregroundStyle(.secondary)
        }
    }
}

struct MeterView: View {
    let level: Float
    let warn: Float

    private let width: CGFloat = 220
    private let height: CGFloat = 10

    var body: some View {
        ZStack(alignment: .leading) {
            Capsule().fill(Color.secondary.opacity(0.15))
            Capsule()
                .fill(level >= warn ? Color.red : Color.accentColor)
                .frame(width: width * CGFloat(scaled))
        }
        .frame(width: width, height: height)
        .accessibilityLabel("Level")
        .accessibilityValue(String(format: "%.2f", level))
    }

    /// A square root, so that quiet playing is visible at all: the useful
    /// range for setting a microphone is the bottom of the linear scale.
    private var scaled: Float {
        min(1, level > 0 ? sqrt(level) : 0)
    }
}
