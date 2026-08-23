import SwiftUI

@main
struct WhistleSynthApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var synth = SynthController()
    @Environment(\.openWindow) private var openWindow

    var body: some Scene {
        Window("Whistle Synth", id: "main") {
            ContentView()
                .environmentObject(synth)
                .onDisappear { synth.shutdown() }
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(replacing: .newItem) { }
            // The default item points at a help book that does not exist,
            // which shows "Help isn't available for Whistle Synth."  This
            // one opens the window below.
            CommandGroup(replacing: .help) {
                Button("Whistle Synth Help") { openWindow(id: "help") }
                    .keyboardShortcut("?", modifiers: .command)
            }
        }

        Window("Whistle Synth Help", id: "help") {
            HelpView()
        }
        .windowResizability(.contentMinSize)
        .defaultPosition(.center)
    }
}

/// The window is the app: closing it stops the audio and quits, which is what
/// a single-window Mac app is expected to do and what keeps a synth from
/// being left running with nothing on screen to say so.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Synchronous: this is the last chance to hand the audio device back
        // with the sample rate and buffer size we changed put back the way we
        // found them.  See AudioLifecycle.shutdown.
        AudioLifecycle.shutdown()
    }
}
