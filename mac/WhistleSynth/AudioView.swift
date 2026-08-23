import SwiftUI

struct AudioView: View {
    @EnvironmentObject private var synth: SynthController

    private static let rates = [0, 44_100, 48_000, 88_200, 96_000]
    private static let buffers = [0, 32, 64, 128, 256, 512, 1024]

    var body: some View {
        Form {
            Section {
                Picker("Input", selection: $synth.inputUID) {
                    Text("System default").tag("")
                    Divider()
                    ForEach(synth.inputDevices()) { device in
                        Text(device.summary(forInput: true)).tag(device.uid)
                    }
                }
                Picker("Output", selection: $synth.outputUID) {
                    Text("System default").tag("")
                    Divider()
                    ForEach(synth.outputDevices()) { device in
                        Text(device.summary(forInput: false)).tag(device.uid)
                    }
                }
            } header: {
                Text("Devices")
            } footer: {
                if synth.splitDevices {
                    Label("Input and output are different devices, so their clocks drift and the input goes through a small buffer. One interface doing both is a little quicker.",
                          systemImage: "info.circle")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else if synth.running {
                    Text("One device for both directions: one clock, nothing buffered between them.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section {
                Picker("Sample rate", selection: $synth.sampleRate) {
                    Text("Device default").tag(0)
                    ForEach(Self.rates.dropFirst(), id: \.self) { rate in
                        Text("\(rate) Hz").tag(rate)
                    }
                }
                Picker("Buffer size", selection: $synth.bufferFrames) {
                    Text("Device default").tag(0)
                    ForEach(Self.buffers.dropFirst(), id: \.self) { frames in
                        Text("\(frames) frames").tag(frames)
                    }
                }
            } header: {
                Text("Timing")
            } footer: {
                Text("Smaller buffers answer sooner and ask more of the machine. If the dropout count climbs while you play, go up a size. The buffer size is this app's own — other apps on the same device keep theirs. The sample rate is not: a device runs at one rate for everything using it, so changing it here changes it for them too. Whistle Synth puts it back when it stops.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("What it actually got") {
                StatRow(title: "Running at") {
                    Text(synth.running
                         ? String(format: "%.0f Hz, %d frames",
                                  synth.actualSampleRate, synth.actualBufferFrames)
                         : "—")
                }
                StatRow(title: "Input latency") { millis(synth.inputLatencyMs) }
                StatRow(title: "Output latency") { millis(synth.outputLatencyMs) }
                StatRow(title: "Round trip") {
                    Text(synth.running
                         ? String(format: "%.1f ms", synth.roundTripMs)
                         : "—")
                        .fontWeight(.semibold)
                        .foregroundStyle(.primary)
                }
                StatRow(title: "Detection lag") {
                    Text(synth.running
                         ? String(format: "+%.1f ms", synth.detectionLagMs)
                         : "—")
                }
                StatRow(title: "Dropouts") {
                    Text("\(synth.xruns + synth.dropouts)")
                        .foregroundStyle(synth.xruns + synth.dropouts > 0 ? .orange : .secondary)
                }
            }
            .monospacedDigit()

            Section {
                Text("Detection lag is not buffering. The synth free-runs and nothing is held up on the way through; it is how late the synth hears about a pitch change, because the detector needs a window of signal to see a pitch at all.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
    }

    private func millis(_ value: Double) -> some View {
        Text(synth.running ? String(format: "%.1f ms", value) : "—")
    }
}
