import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        Group {
            switch synth.permission {
            case .authorized:
                MainTabs()
            case .notDetermined:
                PermissionView(state: .asking)
            default:
                PermissionView(state: .denied)
            }
        }
        .frame(minWidth: 620, minHeight: 560)
        .task {
            await synth.requestPermission()
        }
    }
}

private struct MainTabs: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        VStack(spacing: 0) {
            TabView {
                PlayView().tabItem { Label("Play", systemImage: "waveform") }
                VoiceView().tabItem { Label("Voice", systemImage: "slider.horizontal.3") }
                AudioView().tabItem { Label("Audio", systemImage: "hifispeaker") }
            }
            .padding(.top, 8)

            Divider()
            StatusBar()
        }
    }
}

/// The one line that is worth having in front of you the whole time: whether
/// it is running, on what, and how late it is.
private struct StatusBar: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(synth.running ? Color.green : Color.secondary.opacity(0.4))
                .frame(width: 8, height: 8)

            if let error = synth.errorMessage {
                Text(error)
                    .foregroundStyle(.red)
                    .lineLimit(1)
                Button("Try again") { synth.start() }
                    .controlSize(.small)
            } else if synth.running {
                Text("\(synth.inputName) → \(synth.outputName)")
                    .lineLimit(1)
                    .truncationMode(.middle)
                Text(String(format: "%.1f ms round trip", synth.roundTripMs))
                    .foregroundStyle(.secondary)
                if synth.xruns > 0 {
                    Text("\(synth.xruns) dropout\(synth.xruns == 1 ? "" : "s")")
                        .foregroundStyle(.orange)
                }
            } else {
                Text("Stopped").foregroundStyle(.secondary)
            }
            Spacer()
        }
        .font(.callout)
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }
}

private struct PermissionView: View {
    enum State { case asking, denied }
    let state: State
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "mic.slash")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)
            Text("Whistle Synth needs the microphone")
                .font(.title3.weight(.semibold))
            Text("It listens for a whistle, works out what note it is, and plays a synth lead following it. Nothing is recorded and nothing leaves your Mac.")
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
                .frame(maxWidth: 380)

            if state == .denied {
                Button("Open Privacy Settings") {
                    let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")!
                    NSWorkspace.shared.open(url)
                }
                .buttonStyle(.borderedProminent)
            } else {
                ProgressView().controlSize(.small)
            }
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
