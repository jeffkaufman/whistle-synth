import SwiftUI

struct PlayView: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        Form {
            Section {
                Picker("Voice", selection: $synth.voice) {
                    ForEach(0..<synth.voiceCount, id: \.self) { voice in
                        Text(SynthController.name(ofVoice: voice)).tag(voice)
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

            Section("Listening") {
                StatRow(title: "Input") {
                    MeterView(level: synth.inputPeak, warn: 0.9)
                }
                StatRow(title: "Output") {
                    MeterView(level: synth.outputPeak, warn: 0.98)
                }
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
