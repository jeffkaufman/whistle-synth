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
                        Button("Switch to \(SynthController.name(ofVoice: 1))") {
                            synth.voice = 1
                        }
                    }
                }
            } else {
                Section {
                    Picker("Editing", selection: $synth.voice) {
                        ForEach(1..<synth.voiceCount, id: \.self) { voice in
                            Text(SynthController.name(ofVoice: voice)).tag(voice)
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
        } else {
            Slider(value: binding, in: spec.range)
        }
    }

    private var value: Double { synth.value(for: spec) }
}
