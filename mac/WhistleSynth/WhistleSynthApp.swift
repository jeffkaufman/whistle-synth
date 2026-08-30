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
                // Injected separately rather than reached through `synth`,
                // so that a view can observe the 24Hz readouts without
                // thereby observing everything else -- and, more to the
                // point, so that a view that wants everything else does not
                // get re-rendered 24 times a second.  See `Meters`.
                .environmentObject(synth.meters)
                .onDisappear { synth.shutdown() }
        }
        // Sized so that a window screenshot comes out at exactly 2560x1600,
        // which is one of the four sizes the Mac App Store accepts.
        //
        // The arithmetic, all in points, on a 2x display: a shadow-inclusive
        // capture -- what Cmd-Shift-4 then space gives you -- adds exactly
        // 112pt to each dimension, measured on this machine rather than
        // looked up.  So 1280x800 of capture, less 112 of shadow, is a window
        // 1168x688, which is what these numbers produce: SwiftUI applies them
        // to the frame rather than to the content, so the 32pt title bar is
        // already inside the 688.
        //
        // The help window below is deliberately the same size, because every
        // screenshot in an App Store set has to have identical dimensions.
        //
        // Two things break it.  The window has to be the *frontmost* one when
        // you capture: an inactive window gets a smaller shadow and comes out
        // 2792x1712.  And a saved "NSWindow Frame main" in the app's
        // preferences overrides this entirely, so a Mac that has run an older
        // build keeps whatever size it was left at until that key is removed.
        .defaultSize(width: 1168, height: 688)
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
        // The same size as the main window, so that a capture of this one
        // belongs in the same App Store screenshot set.  See above.
        .defaultSize(width: 1168, height: 688)
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
