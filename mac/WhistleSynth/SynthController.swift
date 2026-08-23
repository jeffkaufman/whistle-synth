import AVFoundation
import Combine
import Foundation

/// Everything the UI binds to, and the only thing that talks to the C side.
///
/// Settings live in UserDefaults, which is what a Mac app is expected to do:
/// the sandbox gives each app its own container, the system backs it up and
/// syncs it with the app, and nothing has to ask for permission to write it.
/// The command-line build's control files could not have been reached from a
/// sandboxed app anyway.
@MainActor
final class SynthController: ObservableObject {

    // MARK: - Stored settings

    enum Key {
        static let voice = "voice"
        static let volume = "volume"
        static let gate = "gate"
        static let fifth = "fifth"
        static let sustain = "sustain"
        static let inputUID = "inputDeviceUID"
        static let outputUID = "outputDeviceUID"
        static let sampleRate = "sampleRate"
        static let bufferFrames = "bufferFrames"
        static let overrides = "voiceOverrides"
    }

    @Published var voice: Int {
        didSet { defaults.set(voice, forKey: Key.voice); publishProgram() }
    }
    @Published var volume: Int {
        didSet { defaults.set(volume, forKey: Key.volume); whistle_set_volume(Int32(volume)) }
    }
    @Published var gate: Int {
        didSet { defaults.set(gate, forKey: Key.gate); whistle_set_gate(Int32(gate)) }
    }
    /// Both of these are the player's, not the patch's: they survive a voice
    /// change and are stored once rather than per voice.  See
    /// `whistle_set_fifth`.
    @Published var fifth: Bool {
        didSet { defaults.set(fifth, forKey: Key.fifth); whistle_set_fifth(fifth) }
    }
    @Published var sustain: Bool {
        didSet { defaults.set(sustain, forKey: Key.sustain); whistle_set_sustain(sustain) }
    }
    @Published var inputUID: String {
        didSet { defaults.set(inputUID, forKey: Key.inputUID); restart() }
    }
    @Published var outputUID: String {
        didSet { defaults.set(outputUID, forKey: Key.outputUID); restart() }
    }
    /// 0 means "whatever the device is already doing".
    @Published var sampleRate: Int {
        didSet { defaults.set(sampleRate, forKey: Key.sampleRate); restart() }
    }
    @Published var bufferFrames: Int {
        didSet { defaults.set(bufferFrames, forKey: Key.bufferFrames); restart() }
    }

    /// Per-voice edits, keyed by voice name so that reordering the preset
    /// table does not shuffle a player's edits onto the wrong voice.  Only
    /// values that differ from the built-in preset are stored, so improving a
    /// preset in a later version still reaches anyone who never touched it.
    @Published private(set) var overrides: [String: [String: Double]]

    // MARK: - Live state

    @Published private(set) var devices: [AudioDevice] = []
    @Published private(set) var running = false
    @Published private(set) var errorMessage: String?
    @Published private(set) var permission: AVAuthorizationStatus

    @Published private(set) var inputPeak: Float = 0
    @Published private(set) var outputPeak: Float = 0
    @Published private(set) var detectedHz: Float = 0
    /// The detector's own level while a note was sounding, which is what
    /// `level_full` is measured against.  See `WhistleStatus.playing_level`.
    @Published private(set) var playingLevel: Float = 0
    @Published private(set) var voiced = false
    @Published private(set) var xruns = 0
    @Published private(set) var dropouts = 0

    @Published private(set) var actualSampleRate: Double = 0
    @Published private(set) var actualBufferFrames = 0
    @Published private(set) var inputLatencyMs: Double = 0
    @Published private(set) var outputLatencyMs: Double = 0
    @Published private(set) var detectionLagMs: Double = 0
    @Published private(set) var splitDevices = false
    @Published private(set) var inputName = ""
    @Published private(set) var outputName = ""

    private let defaults = UserDefaults.standard
    private var meterTimer: Timer?
    private var restartWork: DispatchWorkItem?

    // MARK: - Setup

    init() {
        defaults.register(defaults: [
            Key.voice: SynthController.defaultVoice,
            Key.volume: 5,
            Key.gate: 5,
            Key.fifth: false,
            Key.sustain: false,
            Key.sampleRate: 48_000,
            // The device's own minimum is usually smaller, but 64 frames is
            // already past the point where the hardware's fixed converter and
            // safety offset dominate, and it leaves room for a slow machine.
            Key.bufferFrames: 64,
        ])

        voice = defaults.integer(forKey: Key.voice)
        volume = defaults.integer(forKey: Key.volume)
        gate = defaults.integer(forKey: Key.gate)
        fifth = defaults.bool(forKey: Key.fifth)
        sustain = defaults.bool(forKey: Key.sustain)
        inputUID = defaults.string(forKey: Key.inputUID) ?? ""
        outputUID = defaults.string(forKey: Key.outputUID) ?? ""
        sampleRate = defaults.integer(forKey: Key.sampleRate)
        bufferFrames = defaults.integer(forKey: Key.bufferFrames)
        overrides = SynthController.loadOverrides(from: defaults)
        permission = AVCaptureDevice.authorizationStatus(for: .audio)

        devices = AudioDevice.all()
        whistle_set_volume(Int32(volume))
        whistle_set_gate(Int32(gate))
        whistle_set_fifth(fifth)
        whistle_set_sustain(sustain)
        publishProgram()
        watchDevices()
    }

    /// What a first run should sound like: the plainest voice in the table,
    /// by name rather than by index, since the index of any given voice has
    /// drifted every time presets came and went.  Falling back to the first
    /// preset is what happens when a name here is retired -- which is the
    /// failure this is written to survive, so keep more than one candidate.
    private static var defaultVoice: Int {
        let wanted = ["bass", "lead"]
        for name in wanted {
            for voice in 1...Int(whistle_preset_count())
            where self.name(ofVoice: voice) == name {
                return voice
            }
        }
        return 1
    }

    static func name(ofVoice voice: Int) -> String {
        String(cString: whistle_voice_name(Int32(voice)))
    }

    var voiceCount: Int { Int(whistle_preset_count()) + 1 }

    var currentVoiceName: String { SynthController.name(ofVoice: voice) }

    var isPassthrough: Bool { voice == 0 }

    // MARK: - Microphone permission

    /// Must be granted before the input renders anything but silence.  The
    /// sandbox entitlement gets us the right to ask; this is the asking.
    func requestPermission() async {
        if permission == .notDetermined {
            _ = await AVCaptureDevice.requestAccess(for: .audio)
            permission = AVCaptureDevice.authorizationStatus(for: .audio)
        }
        if permission == .authorized {
            start()
        }
    }

    // MARK: - Running

    func start() {
        guard permission == .authorized else { return }

        var config = WhistleConfig()
        setCString(&config.input_uid, inputUID)
        setCString(&config.output_uid, outputUID)
        config.sample_rate = Double(sampleRate)
        config.buffer_frames = Int32(bufferFrames)

        if whistle_start(&config) {
            running = true
            errorMessage = nil
        } else {
            running = false
            let message = String(cString: whistle_last_error())
            errorMessage = message.isEmpty ? "The audio device would not start." : message
        }
        refreshStatus()
        startMetering()
    }

    func stop() {
        meterTimer?.invalidate()
        meterTimer = nil
        whistle_stop()
        running = false
        refreshStatus()
    }

    /// Settings that need the stream rebuilt.  Coalesced, because dragging
    /// through a device menu would otherwise tear the stream down once per
    /// item the highlight passes over.
    private func restart() {
        restartWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.permission == .authorized else { return }
            self.start()
        }
        restartWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: work)
    }

    // MARK: - Devices

    private func watchDevices() {
        let context = Unmanaged.passUnretained(self).toOpaque()
        whistle_set_devices_changed_callback({ context in
            guard let context else { return }
            let controller = Unmanaged<SynthController>
                .fromOpaque(context).takeUnretainedValue()
            // Called on a CoreAudio thread.
            DispatchQueue.main.async { controller.devicesChanged() }
        }, context)
    }

    private func devicesChanged() {
        devices = AudioDevice.all()
        // The stream holds a device ID, and IDs do not survive a device going
        // away.  Rebuilding is cheap and is the only way to follow a player
        // unplugging their interface mid-tune.
        if running {
            restart()
        }
    }

    func inputDevices() -> [AudioDevice] { devices.filter(\.canRecord) }
    func outputDevices() -> [AudioDevice] { devices.filter(\.canPlay) }

    // MARK: - Voice parameters

    var currentParams: SynthParams {
        VoiceParameters.params(forVoice: voice,
                               overrides: overrides[currentVoiceName] ?? [:])
    }

    func value(for spec: ParamSpec) -> Double {
        spec.get(currentParams)
    }

    func setValue(_ value: Double, for spec: ParamSpec) {
        guard voice != 0 else { return }
        let name = currentVoiceName
        var forVoice = overrides[name] ?? [:]

        // Store only what differs from the preset, so a later version's
        // improved preset still reaches anyone who left this field alone.
        let defaultValue = spec.get(VoiceParameters.defaults(forVoice: voice))
        if abs(value - defaultValue) < 1e-9 {
            forVoice.removeValue(forKey: spec.id)
        } else {
            forVoice[spec.id] = value
        }

        if forVoice.isEmpty {
            overrides.removeValue(forKey: name)
        } else {
            overrides[name] = forVoice
        }
        saveOverrides()
        publishProgram()
    }

    func isEdited(_ spec: ParamSpec) -> Bool {
        overrides[currentVoiceName]?[spec.id] != nil
    }

    var currentVoiceIsEdited: Bool {
        !(overrides[currentVoiceName] ?? [:]).isEmpty
    }

    func resetCurrentVoice() {
        overrides.removeValue(forKey: currentVoiceName)
        saveOverrides()
        publishProgram()
    }

    func resetAllVoices() {
        overrides = [:]
        saveOverrides()
        publishProgram()
    }

    private func publishProgram() {
        guard voice != 0 else {
            whistle_set_program(0, nil)
            return
        }
        var params = currentParams
        whistle_set_program(Int32(voice), &params)
    }

    private static func loadOverrides(from defaults: UserDefaults) -> [String: [String: Double]] {
        guard let data = defaults.data(forKey: Key.overrides),
              let decoded = try? JSONDecoder()
                  .decode([String: [String: Double]].self, from: data)
        else { return [:] }
        return decoded
    }

    private func saveOverrides() {
        if overrides.isEmpty {
            defaults.removeObject(forKey: Key.overrides)
        } else if let data = try? JSONEncoder().encode(overrides) {
            defaults.set(data, forKey: Key.overrides)
        }
    }

    // MARK: - Metering

    private func startMetering() {
        meterTimer?.invalidate()
        let timer = Timer(timeInterval: 1.0 / 24, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refreshStatus() }
        }
        RunLoop.main.add(timer, forMode: .common)
        meterTimer = timer
    }

    private func refreshStatus() {
        var status = WhistleStatus()
        whistle_status(&status)

        running = status.running
        actualSampleRate = status.sample_rate
        actualBufferFrames = Int(status.buffer_frames)
        inputLatencyMs = status.input_latency_ms
        outputLatencyMs = status.output_latency_ms
        detectionLagMs = status.detection_lag_ms
        splitDevices = status.split_devices
        inputName = whistleString(status.input_name)
        outputName = whistleString(status.output_name)
        xruns = Int(status.xruns)
        dropouts = Int(status.dropouts)
        voiced = status.voiced
        detectedHz = status.freq

        // Held and let fall like the peaks below, and slower: this is read
        // while whistling a long note to set `level_full`, so it has to stay
        // legible between the analysis hops that produce it.
        playingLevel = max(status.playing_level, playingLevel * 0.94)

        // The C side reports the peak since it was last asked and then
        // resets, so hold the needle and let it fall, or a meter sampled at
        // 24Hz mostly shows the gaps between notes.
        inputPeak = max(status.input_peak, inputPeak * 0.82)
        outputPeak = max(status.output_peak, outputPeak * 0.82)
    }

    // MARK: - Readouts

    var roundTripMs: Double { inputLatencyMs + outputLatencyMs }

    /// The nearest note name to what is being detected, which is the quickest
    /// way to tell a detector problem from a whistling problem.
    var detectedNote: String? {
        guard voiced, detectedHz > 20 else { return nil }
        let names = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"]
        let midi = 69 + 12 * log2(Double(detectedHz) / 440)
        let rounded = Int(midi.rounded())
        let cents = Int(((midi - Double(rounded)) * 100).rounded())
        let name = names[((rounded % 12) + 12) % 12]
        let octave = rounded / 12 - 1
        return "\(name)\(octave) \(cents >= 0 ? "+" : "")\(cents)¢"
    }
}
