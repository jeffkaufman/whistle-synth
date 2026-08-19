import SwiftUI

@main
struct WhistleSynthApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var synth = SynthController()

    var body: some Scene {
        Window("Whistle Synth", id: "main") {
            ContentView()
                .environmentObject(synth)
                .onDisappear { synth.stop() }
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(replacing: .newItem) { }
        }
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
        whistle_stop()
    }
}
