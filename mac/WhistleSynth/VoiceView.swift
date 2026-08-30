import SwiftUI

struct VoiceView: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        Form {
            if synth.isPassthrough {
                Section {
                    // Editing the voice you are hearing is the whole point, so
                    // the tab follows the Play tab rather than keeping its own
                    // selection.  Passthrough has nothing to edit -- but
                    // switching away from it silently, just because you looked
                    // at this tab, would change what you hear behind your back.
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Passthrough has no sound of its own to edit.")
                        // The same round trip the Play tab's button makes:
                        // back to what was playing, not to a fixed voice.
                        Button("Switch to \(SynthController.displayName(ofVoice: synth.comeBackToVoice))") {
                            synth.togglePassthrough()
                        }
                    }
                }
            } else {
                Section {
                    Picker("Editing", selection: $synth.voice) {
                        ForEach(1..<synth.voiceCount, id: \.self) { voice in
                            Text(SynthController.displayName(ofVoice: voice))
                                .tag(voice)
                        }
                    }
                    HStack {
                        Button("Reset this voice") { synth.resetCurrentVoice() }
                            .disabled(!synth.currentVoiceIsEdited)
                        Spacer()
                        Button("Reset all voices") { synth.resetAllVoices() }
                            .disabled(synth.overrides.isEmpty)
                    }
                } footer: {
                    Text("This is the voice you are hearing. Changes take effect immediately and are kept between launches; anything you leave alone follows the built-in preset, including when a later version improves it.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            // Here rather than on the Play tab, which is now only the
            // controls someone touches mid-tune.  These are numbers you read
            // while setting the thing up, and the one that matters -- what
            // the detector hears while a note is sounding -- is the number
            // "Full-blow level" below is compared against, so it belongs on
            // the same page as the slider it is for.
            ListeningSection()

            if !synth.isPassthrough {
                ForEach(ParamSpec.Group.allCases) { group in
                    Section {
                        ForEach(VoiceParameters.specs(in: group)) { spec in
                            ParamRow(spec: spec)
                        }
                    } header: {
                        Text(group.rawValue)
                    } footer: {
                        Text(group.blurb)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
    }
}

/// The five live numbers, in their own view so that they are the only thing
/// on this tab that a 24Hz meter tick re-renders.  Inline in `VoiceView.body`
/// they would have rebuilt the whole form -- every parameter row and its
/// slider -- twenty-four times a second.  See `Meters`.
private struct ListeningSection: View {
    @EnvironmentObject private var meters: Meters

    var body: some View {
        Section {
            StatRow(title: "Input") {
                MeterView(level: meters.inputPeak, warn: 0.9)
            }
            StatRow(title: "Output") {
                MeterView(level: meters.outputPeak, warn: 0.98)
            }
            StatRow(title: "While playing") {
                Text(meters.playingLevel > 0.0005
                     ? String(format: "%.3f", meters.playingLevel)
                     : "—")
                    .font(.system(.body, design: .monospaced))
            }
            StatRow(title: "Detected") {
                Text(meters.detectedNote ?? "—")
                    .font(.system(.body, design: .monospaced))
                    .foregroundStyle(meters.voiced ? .primary : .secondary)
            }
            StatRow(title: "Pitch") {
                Text(meters.voiced
                     ? String(format: "%.1f Hz", meters.detectedHz)
                     : "—")
                    .font(.system(.body, design: .monospaced))
                    .foregroundStyle(meters.voiced ? .primary : .secondary)
            }
        } header: {
            Text("Listening")
        } footer: {
            Text("\"While playing\" is what the detector heard while a note was actually sounding — not the input meter above it, which is a sample peak and counts the room between notes. Whistle your loudest and set Full-blow level to what it reads.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

private struct ParamRow: View {
    let spec: ParamSpec
    @EnvironmentObject private var synth: SynthController
    @State private var showHelp = false

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 6) {
                Text(spec.label)
                if synth.isEdited(spec) {
                    Text("edited")
                        .font(.caption2)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.accentColor.opacity(0.18),
                                    in: Capsule())
                }
                Spacer()
                Text(spec.format(value))
                    .font(.system(.callout, design: .monospaced))
                    .foregroundStyle(.secondary)
            }
            slider
            Text(spec.help)
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(.vertical, 2)
    }

    @ViewBuilder private var slider: some View {
        let binding = Binding(
            get: { value },
            set: { synth.setValue($0, for: spec) })
        if spec.isToggle {
            // A two-position slider for a bool is a puzzle, not a control.
            Toggle(spec.label, isOn: Binding(get: { binding.wrappedValue > 0.5 },
                                             set: { binding.wrappedValue = $0 ? 1 : 0 }))
                .labelsHidden()
                .frame(maxWidth: .infinity, alignment: .leading)
        } else if let step = spec.step {
            Slider(value: binding, in: spec.range, step: step)
                .accessibilityLabel(spec.label)
                .accessibilityValue(spec.format(value))
        } else {
            Slider(value: binding, in: spec.range)
                .accessibilityLabel(spec.label)
                .accessibilityValue(spec.format(value))
        }
    }

    private var value: Double { synth.value(for: spec) }
}
