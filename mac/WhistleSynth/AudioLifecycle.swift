import Foundation

/// Which devices the current settings resolve to, and whether they can be
/// played at all.  The Swift side of `WhistleRoute`, whose fixed C char
/// arrays arrive as tuples and are no use to a view.
struct AudioRoute: Equatable {
    var haveInput = false
    var haveOutput = false
    var inputName = ""
    var outputName = ""
    /// The Mac's own microphone into its own speakers.  See
    /// `WhistleRoute.builtin_loop`: refused rather than warned about, because
    /// there is no setting that makes it work.
    var builtinLoop = false
    var usable = false

    /// What is wrong, in a sentence for the player, or nil when nothing is.
    var problem: String? {
        if builtinLoop {
            return "This Mac's speakers are a few inches from its microphone and pointed at it, so the synth would hear itself and howl. Connect headphones, or an audio interface, or choose different devices below."
        }
        if !haveInput { return "No microphone is available." }
        if !haveOutput { return "No audio output is available." }
        return nil
    }

    var problemTitle: String {
        builtinLoop ? "Headphones needed" : "No audio device"
    }
}

/// The one place `whistle_start` and `whistle_stop` are called from.
///
/// Both are synchronous CoreAudio work -- opening units, negotiating formats,
/// and in split mode a wait of up to 200ms while the input ring primes -- so
/// neither belongs on the main thread.  `restart()` re-enters all of it on
/// every device change and every settings change, and doing that on the main
/// thread meant unplugging an interface mid-tune stalled the window for as
/// long as the hardware took to answer.
///
/// A serial queue rather than a lock, because the C side's rule is that only
/// one thread ever drives the stream's lifecycle.  Everything the audio
/// thread publishes goes through atomics, so metering keeps working from the
/// main thread while a start is in flight.
enum AudioLifecycle {
    private static let queue = DispatchQueue(
        label: "com.jefftk.WhistleSynth.audio", qos: .userInitiated)

    /// Calls back on the main queue: nil means it started, otherwise the
    /// sentence to show the player.
    static func start(_ config: WhistleConfig,
                      completion: @escaping (String?) -> Void) {
        queue.async {
            var config = config
            let started = whistle_start(&config)
            let error = started ? nil : String(cString: whistle_last_error())
            DispatchQueue.main.async { completion(error) }
        }
    }

    static func stop(completion: (() -> Void)? = nil) {
        queue.async {
            whistle_stop()
            if let completion {
                DispatchQueue.main.async(execute: completion)
            }
        }
    }

    /// Synchronous, and the only call here that is: at termination the
    /// process is about to go away, and the device has to be handed back --
    /// with its sample rate and buffer size restored -- before it does.
    static func shutdown() {
        queue.sync { whistle_stop() }
    }

    /// Property reads only, with no stream involved, so this stays on the
    /// caller's thread and a view can ask before anything has started.
    static func route(for config: WhistleConfig) -> AudioRoute {
        var config = config
        var raw = WhistleRoute()
        whistle_resolve_route(&config, &raw)
        return AudioRoute(haveInput: raw.have_input,
                          haveOutput: raw.have_output,
                          inputName: whistleString(raw.input_name),
                          outputName: whistleString(raw.output_name),
                          builtinLoop: raw.builtin_loop,
                          usable: raw.usable)
    }
}
