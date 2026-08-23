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
    @State private var tab = Tab.play

    enum Tab { case play, voice, audio }

    var body: some View {
        VStack(spacing: 0) {
            // Above the tabs rather than inside one, because it is true of
            // the whole app: nothing sounds while it is showing, whichever
            // tab is in front.
            if let problem = synth.route.problem {
                RouteBlockView(title: synth.route.problemTitle,
                               problem: problem,
                               showDevicesButton: tab != .audio) {
                    tab = .audio
                }
            }

            TabView(selection: $tab) {
                PlayView().tabItem { Label("Play", systemImage: "waveform") }
                    .tag(Tab.play)
                VoiceView().tabItem { Label("Voice", systemImage: "slider.horizontal.3") }
                    .tag(Tab.voice)
                AudioView().tabItem { Label("Audio", systemImage: "hifispeaker") }
                    .tag(Tab.audio)
            }
            .padding(.top, 8)

            Divider()
            StatusBar()
        }
    }
}

/// The one configuration the app refuses to run in, and the one it cannot.
///
/// Not a warning: a Mac's speakers sit a few inches from its microphone and
/// point at it, so the synth hears its own output, tracks it, and howls
/// within about a second.  Every voice does it, the gate does not help --
/// the detector is following a real signal, it is just the wrong one -- and
/// passthrough is the worst of them.  So the stream does not start, and this
/// says so and stays until it is fixed.
///
/// The Audio tab is still reachable behind this, which is the point of a
/// banner rather than a takeover: an interface that is plugged in but not
/// selected is fixed two clicks away.
private struct RouteBlockView: View {
    let title: String
    let problem: String
    let showDevicesButton: Bool
    let showDevices: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: "headphones")
                .font(.title2)
                .foregroundStyle(.orange)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.headline)
                Text(problem)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                if showDevicesButton {
                    Button("Choose audio devices", action: showDevices)
                        .controlSize(.small)
                        .padding(.top, 2)
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(Color.orange.opacity(0.12))
        .overlay(alignment: .bottom) { Divider() }
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
                    .help(error)
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
            } else if synth.starting {
                Text("Starting…").foregroundStyle(.secondary)
            } else if synth.route.problem != nil {
                Text(synth.route.problemTitle).foregroundStyle(.secondary)
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
                Text("Turn on Whistle Synth under Privacy & Security ▸ Microphone, then come back here.")
                    .font(.callout)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: 380)
                HStack(spacing: 12) {
                    Button("Open Privacy Settings") { openMicrophoneSettings() }
                        .buttonStyle(.borderedProminent)
                    // The permission is re-checked whenever the app comes
                    // back to the front, so this button is mostly belt and
                    // braces -- but "granted it, picked Later, nothing
                    // happened" is exactly the path someone testing the
                    // denied case walks, and a dead window at the end of it
                    // is worse than a redundant button.
                    Button("Check again") { synth.refreshPermission() }
                }
            } else {
                ProgressView().controlSize(.small)
            }
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    /// Ventura renamed the pane and moved the anchor into an extension; the
    /// pre-Ventura string lands on the top of Privacy & Security at best.
    /// The deployment target is 13.0, so the new one is the one to use, with
    /// the old one kept only for the case where the new URL will not open at
    /// all -- a button that appears to do nothing is the worst outcome here.
    private func openMicrophoneSettings() {
        let anchors = [
            "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Microphone",
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone",
        ]
        for anchor in anchors {
            if let url = URL(string: anchor), NSWorkspace.shared.open(url) {
                return
            }
        }
    }
}
