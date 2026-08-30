import AVFoundation
import AppKit
import Combine
import Foundation

/// The readouts that change on every tick, deliberately kept off
/// `SynthController`.
///
/// An `ObservableObject` has exactly one `objectWillChange`, so writing *any*
/// `@Published` on it re-renders *every* view holding it, whatever that view
/// actually reads.  These six change 24 times a second by design.  While they
/// lived on the controller, that meant the entire window -- the tab bar
/// included -- was rebuilt 24 times a second for as long as the app was open.
///
/// That was not merely wasted CPU.  Rebuilding the `TabView` rebuilds the
/// three `.tabItem { Label(...) }` in `ContentView`, and SwiftUI holds on to
/// those: after one nine-hour run there were 293,000 of them, 285,000
/// observation registrars behind them, and 875MB resident.  Because each
/// invalidation then has to walk that accumulated registry, the cost of a
/// render grew with the number already leaked -- so the app got measurably
/// slower the longer it ran, which is how this was found.  It had dropped
/// from 24 renders a second to 1.4.
///
/// So the fast numbers live here, and only the two small views that actually
/// draw them observe this object.  Anything added here should be something
/// that changes on a tick; anything that changes when a *person* does
/// something belongs on the controller.
@MainActor
final class Meters: ObservableObject {
    @Published fileprivate(set) var inputPeak: Float = 0
    @Published fileprivate(set) var outputPeak: Float = 0
    @Published fileprivate(set) var detectedHz: Float = 0
    /// The detector's own level while a note was sounding, which is what
    /// `level_full` is measured against.  See `WhistleStatus.playing_level`.
    @Published fileprivate(set) var playingLevel: Float = 0
    /// The same, held far longer: the mark on the full-blow knob has to stay
    /// where your loudest whistle put it while you take your mouth off the
    /// microphone and reach for the mouse.
    @Published fileprivate(set) var playingHold: Float = 0
    @Published fileprivate(set) var voiced = false

    /// The nearest note name to what is being detected, which is the quickest
    /// way to tell a detector problem from a whistling problem.
    var detectedNote: String? {
        guard voiced, detectedHz > 20 else { return nil }
        return SynthController.noteName(withCents: detectedHz)
    }

    fileprivate func update(from status: WhistleStatus) {
        voiced = status.voiced
        detectedHz = status.freq

        // Held and let fall like the peaks below, and slower: this is read
        // while whistling a long note to set `level_full`, so it has to stay
        // legible between the analysis hops that produce it.
        playingLevel = max(status.playing_level, playingLevel * 0.94)
        // About eight seconds to fall by half at 24Hz, which is long enough
        // to whistle, stop, look and click, and short enough that it is
        // still about what you just did.
        playingHold = max(status.playing_level, playingHold * 0.9964)

        // The C side reports the peak since it was last asked and then
        // resets, so hold the needle and let it fall, or a meter sampled at
        // 24Hz mostly shows the gaps between notes.
        inputPeak = max(status.input_peak, inputPeak * 0.82)
        outputPeak = max(status.output_peak, outputPeak * 0.82)
    }
}

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
        static let fullBlow = "fullBlow"
        static let octaveShift = "octaveShift"
        static let lowNote = "lowNote"
        static let highNote = "highNote"
        static let inputUID = "inputDeviceUID"
        static let outputUID = "outputDeviceUID"
        static let sampleRate = "sampleRate"
        static let bufferFrames = "bufferFrames"
        static let overrides = "voiceOverrides"
    }

    @Published var voice: Int {
        didSet {
            if voice != 0 { comeBackTo = voice }
            defaults.set(voice, forKey: Key.voice)
            publishProgram()
        }
    }
    /// Where `togglePassthrough` goes back to: the last actual voice that was
    /// playing.  Tracked here rather than in the view so that it follows a
    /// voice change made anywhere, and not stored, because it is only ever
    /// the answer to "what was I just playing".
    private var comeBackTo: Int
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
    /// What counts as blowing as hard as you are going to, as a 0-9 knob
    /// applying to every voice.  A player control and not a voice one: it is
    /// an input level in input units, so it describes the microphone, the
    /// preamp and this player's whistle rather than any sound.  Every preset
    /// in the table asks for the same value, which is the table saying the
    /// same thing.
    @Published var fullBlow: Int {
        didSet {
            defaults.set(fullBlow, forKey: Key.fullBlow)
            whistle_set_level_full(Int32(fullBlow))
            showFullBlowMarker()
        }
    }

    /// Whether to draw the mark showing where the loudest recent whistle
    /// landed on that bar.
    ///
    /// Only while the knob is being set, and never at startup.  It is exactly
    /// the right thing to look at for the half minute you are calibrating and
    /// exactly the wrong thing to have twitching under a bar for the rest of
    /// the set -- a live readout on a page whose whole point is that there is
    /// little to read.
    @Published private(set) var showingFullBlowMarker = false
    private var fullBlowMarkerTimer: Timer?

    private func showFullBlowMarker() {
        showingFullBlowMarker = true
        fullBlowMarkerTimer?.invalidate()
        // Its own timer rather than a deadline checked by the meter tick, so
        // that it still goes away when the meters are not running.
        let timer = Timer(timeInterval: 30, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.showingFullBlowMarker = false }
        }
        RunLoop.main.add(timer, forMode: .common)
        fullBlowMarkerTimer = timer
    }

    /// Whole octaves up or down, on top of wherever the voice already sits.
    /// A player control like the two above -- where a line sits is a decision
    /// about the arrangement rather than about the timbre -- so it survives a
    /// voice change and is stored once rather than per voice.
    @Published var octaveShift: Int {
        didSet {
            defaults.set(octaveShift, forKey: Key.octaveShift)
            whistle_set_octave(Int32(octaveShift))
        }
    }

    /// The lowest and highest notes that will trigger, as MIDI note numbers.
    /// A player control like the two above: it is about the range being
    /// played rather than about the sound, so it survives a voice change.
    ///
    /// Pushing one end past the other moves both rather than refusing, which
    /// is what a pair of menus that can each be set to anything has to do; the
    /// two ends may meet, and a range of one note is a legitimate thing to
    /// ask for.
    @Published var lowNote: Int {
        didSet {
            defaults.set(lowNote, forKey: Key.lowNote)
            if highNote < lowNote { highNote = lowNote }
            publishRange()
        }
    }
    @Published var highNote: Int {
        didSet {
            defaults.set(highNote, forKey: Key.highNote)
            if lowNote > highNote { lowNote = highNote }
            publishRange()
        }
    }

    /// Deliberately *not* stored.  Everything else here is a setting and
    /// wants to come back the way it was left; the mute is a thing done for a
    /// moment, and an app that starts silent because of something someone did
    /// last week is an app that appears broken.
    @Published var muted = false {
        didSet { whistle_set_mute(muted) }
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
    /// A start is in flight on the audio queue.  Worth showing, because
    /// opening a device is not always instant and a window that says nothing
    /// for half a second reads as broken.
    @Published private(set) var starting = false
    @Published private(set) var errorMessage: String?
    @Published private(set) var permission: AVAuthorizationStatus
    /// What the device settings resolve to, and whether it can be played.
    /// Kept up to date whether or not anything is running, so the window can
    /// say "connect headphones" the whole time it is true rather than only
    /// when someone tries to start.
    @Published private(set) var route = AudioRoute()

    /// The 24Hz readouts, on their own object so that they invalidate only
    /// the two views that draw them.  See `Meters` for why that matters.
    let meters = Meters()

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
    private var routeTimer: Timer?
    private var restartWork: DispatchWorkItem?
    private var activationObserver: NSObjectProtocol?

    // MARK: - Setup

    init() {
        defaults.register(defaults: [
            Key.voice: SynthController.defaultVoice,
            Key.volume: 5,
            Key.gate: 5,
            Key.fifth: false,
            Key.sustain: false,
            // 0 means "leave the device alone", and the sample rate stays
            // there: a device runs at one rate for everything using it, so
            // picking one is a decision about the whole machine and not ours
            // to make for someone who never asked.
            Key.sampleRate: 0,
            // The buffer size is not shared -- measured, not assumed: with
            // this app running at 64 frames, another process on the same
            // device still reads 512.  The HAL gives each client its own and
            // adapts.  So this one is ours to set, and 64 is the point of the
            // app: the device's own minimum is usually smaller, but 64 is
            // already past where the hardware's fixed converter and safety
            // offset dominate, and it leaves room for a slow machine.
            Key.bufferFrames: 64,
            // The ordinary whistle range rather than the widest one on
            // offer.  The extremes are what has ever been recorded, not what
            // anyone plays, and the bottom of them is paid for in detection
            // lag -- so they are somewhere to go, not somewhere to start.
            // The middle of the knob, which is the value every preset in
            // the table carries: the app starts out doing exactly what it
            // did when this was a per-voice number.
            Key.fullBlow: 5,
            Key.lowNote: Int(whistle_default_low_note()),
            Key.highNote: Int(whistle_default_high_note()),
        ])

        let storedVoice = defaults.integer(forKey: Key.voice)
        voice = storedVoice
        // A launch that comes up in passthrough has nothing to come back to
        // yet, so the way out of it is the same voice a first run would have
        // started on.
        comeBackTo = storedVoice == 0 ? SynthController.defaultVoice : storedVoice
        volume = defaults.integer(forKey: Key.volume)
        gate = defaults.integer(forKey: Key.gate)
        fifth = defaults.bool(forKey: Key.fifth)
        sustain = defaults.bool(forKey: Key.sustain)
        fullBlow = defaults.integer(forKey: Key.fullBlow)
        octaveShift = defaults.integer(forKey: Key.octaveShift)
        lowNote = defaults.integer(forKey: Key.lowNote)
        highNote = defaults.integer(forKey: Key.highNote)
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
        whistle_set_octave(Int32(octaveShift))
        publishRange()
        publishProgram()
        watchDevices()
        refreshRoute()
        watchActivation()
    }

    deinit {
        if let activationObserver {
            NotificationCenter.default.removeObserver(activationObserver)
        }
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

    /// The internal name, which is what the preset table and the
    /// command-line build call it, and what stored edits are keyed by.  Not
    /// for showing to anyone.
    static func name(ofVoice voice: Int) -> String {
        String(cString: whistle_voice_name(Int32(voice)))
    }

    /// What the UI calls it.  Written out by hand rather than derived,
    /// because the derivations all get something wrong: `fm` wants both
    /// letters capital, `subbass` is two words, `eight-oh-eight` is a number
    /// spelled out.  A voice with no entry falls back to its internal name
    /// with the first letter raised, which is wrong in a small way rather
    /// than absent, and is the thing to notice when adding a preset.
    static func displayName(ofVoice voice: Int) -> String {
        let internalName = name(ofVoice: voice)
        if let display = displayNames[internalName] {
            return display
        }
        return internalName.prefix(1).uppercased() + internalName.dropFirst()
    }

    private static let displayNames = [
        "raw input (passthrough)": "Passthrough (raw input)",
        "bass": "Bass",
        "subbass": "Sub Bass",
        "octaveless": "Octaveless",
        "reese": "Reese",
        "eight-oh-eight": "808",
        "pluck": "Pluck",
        "fm": "FM",
        "fm-sub": "FM Sub",
        "grind": "Grind",
        "square": "Square",
        "drawbar": "Drawbar",
        "drawbar-hi": "Drawbar Hi",
    ]

    var voiceCount: Int { Int(whistle_preset_count()) + 1 }

    var currentVoiceName: String { SynthController.name(ofVoice: voice) }

    var currentVoiceDisplayName: String {
        SynthController.displayName(ofVoice: voice)
    }

    var isPassthrough: Bool { voice == 0 }

    /// The input level one step of that knob stands for, from the engine
    /// rather than worked out again here.
    static func levelFull(atStep step: Int) -> Double {
        Double(whistle_level_full_for_step(Int32(step)))
    }

    var fullBlowLevel: Double { SynthController.levelFull(atStep: fullBlow) }

    /// Where the loudest recent playing actually landed, on the same 0-9
    /// scale the knob uses -- nil until something has been played.
    ///
    /// This is the whole reason the knob can live on the Play tab at all.
    /// "Whistle your loudest and set this to what you measured" is two
    /// numbers to read and a tab to change; the same instruction with a mark
    /// on the bar is "whistle your loudest and click where the mark is".
    ///
    /// nil unless the knob has been touched in the last 30 seconds: see
    /// `showingFullBlowMarker`.
    func playingStep(_ meters: Meters) -> Double? {
        guard showingFullBlowMarker, meters.playingHold > 0.0005 else { return nil }
        let steps = log10(Double(meters.playingHold) / SynthController.levelFull(atStep: 5))
                    / 0.15
        return min(9, max(0, 5 + steps))
    }

    /// How far the octave buttons go either way, from the synth rather than
    /// written down again here.
    static let octaveLimit = Int(SYNTH_OCTAVE_SHIFT)

    /// What the buttons and the readout say.  Signed, and "0" rather than
    /// "+0", because zero is the voice as written rather than a shift of
    /// none.
    var octaveLabel: String {
        octaveShift == 0 ? "0"
            : octaveShift > 0 ? "+\(octaveShift)" : "\(octaveShift)"
    }

    func bumpOctave(_ delta: Int) {
        let wanted = octaveShift + delta
        octaveShift = min(SynthController.octaveLimit,
                          max(-SynthController.octaveLimit, wanted))
    }

    // MARK: - Notes

    /// The ends of the menu: the lowest and highest notes anyone has been
    /// recorded whistling, F3 and E9, which is what the detector can be asked
    /// for.
    static let lowestNote = Int(whistle_lowest_note())
    static let highestNote = Int(whistle_highest_note())

    /// And where it starts, and what "Default" goes back to: the ordinary
    /// whistle range.  Reaching past it is a decision -- at the bottom it is
    /// paid for in detection lag -- so it is offered rather than assumed.
    static let defaultLowNote = Int(whistle_default_low_note())
    static let defaultHighNote = Int(whistle_default_high_note())

    /// Sharps below the tonic and flats above it, which is how the note names
    /// in this app have always read.  One copy, because the readout and the
    /// range menus have to agree.
    static func noteName(_ midi: Int) -> String {
        let names = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"]
        return "\(names[((midi % 12) + 12) % 12])\(midi / 12 - 1)"
    }

    /// True when either end has been moved off the default -- in either
    /// direction now, since the range can be opened as well as narrowed.
    var rangeIsCustom: Bool {
        lowNote != SynthController.defaultLowNote ||
        highNote != SynthController.defaultHighNote
    }

    func resetRange() {
        lowNote = SynthController.defaultLowNote
        highNote = SynthController.defaultHighNote
    }

    /// Whether a pitch would be refused for being outside the range, which is
    /// what turns "nothing is happening" into "that note is outside the
    /// range you set".  Half a semitone either side, matching what the C side
    /// does with these two numbers.
    func isOutOfRange(hz: Float) -> Bool {
        guard hz > 20 else { return false }
        let midi = 69 + 12 * log2(Double(hz) / 440)
        return midi < Double(lowNote) - 0.5 || midi > Double(highNote) + 0.5
    }

    /// A pitch the detector is hearing clearly and refusing for being outside
    /// the range, named -- or nil, which is every other case.
    ///
    /// Guarded on the input meter as well as on the pitch, because
    /// `detectedHz` holds its last trustworthy reading rather than going to
    /// zero: without that guard, one refused note would leave the message on
    /// screen for the rest of a silent room.
    ///
    /// Takes the meters rather than reading them off `self`, because they no
    /// longer live here: the range is a setting and the pitch is a reading,
    /// and this is the one place the two have to meet.
    func refusedNote(_ meters: Meters) -> String? {
        guard !meters.voiced, meters.inputPeak > 0.02,
              isOutOfRange(hz: meters.detectedHz) else {
            return nil
        }
        return SynthController.noteName(withCents: meters.detectedHz)
    }

    private func publishRange() {
        whistle_set_note_range(Int32(lowNote), Int32(highNote))
    }

    /// Passthrough is somewhere you go to check something -- the input level,
    /// the gate, whether the right microphone is selected -- rather than
    /// somewhere you play from, and on a machine whose speakers can hear its
    /// own microphone it is an unconditional feedback loop that howls within
    /// a second.  So the button that goes there is the button that comes
    /// back, to the voice that was playing rather than to a fixed one: with a
    /// room howling, the way out has to be the control you just pressed and
    /// not a second one you have to find.
    func togglePassthrough() {
        voice = isPassthrough ? comeBackTo : 0
    }

    /// What that button will put back, for the button to say so.
    var comeBackToVoice: Int { comeBackTo }

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

    /// Someone who denies the microphone, goes to System Settings, grants it,
    /// and picks "Later" at the "Quit & Reopen" prompt comes back to this
    /// window.  Without a re-check that window stays dead for the rest of the
    /// launch, with nothing on it but the button that sent them to Settings.
    func refreshPermission() {
        let current = AVCaptureDevice.authorizationStatus(for: .audio)
        guard current != permission else { return }
        permission = current
        if current == .authorized {
            start()
        } else if running {
            stop()
        }
    }

    /// Coming back to the app is when a permission granted elsewhere is worth
    /// noticing, and it is also when the devices are most likely to have
    /// changed under us -- headphones get plugged in while the app is behind
    /// something else.
    private func watchActivation() {
        activationObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.didBecomeActiveNotification,
            object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.refreshPermission()
                self.devicesChanged()
            }
        }
    }

    // MARK: - Running

    /// Everything `whistle_start` needs, in one place, so that the route
    /// check and the start itself cannot disagree about what they are
    /// talking about.
    private var config: WhistleConfig {
        var config = WhistleConfig()
        setCString(&config.input_uid, inputUID)
        setCString(&config.output_uid, outputUID)
        config.sample_rate = Double(sampleRate)
        config.buffer_frames = Int32(bufferFrames)
        return config
    }

    private func refreshRoute() {
        route = AudioLifecycle.route(for: config)
        watchBlockedRoute()
    }

    /// Headphones going into the jack fire no CoreAudio notification worth
    /// listening for: the device list does not change, the default output
    /// does not change, and the device ID does not change -- it is the same
    /// built-in device reporting a different data source.  There is nothing
    /// to subscribe to, so while the route is refused, ask.
    ///
    /// Once a second, and only while blocked, which is the only state where
    /// anyone is waiting on the answer.  A USB interface or a pair of AirPods
    /// does change the device list and arrives through the callback like
    /// everything else; this is for the one case that does not.
    private func watchBlockedRoute() {
        let wanted = route.problem != nil && permission == .authorized
        guard wanted != (routeTimer != nil) else { return }

        routeTimer?.invalidate()
        routeTimer = nil
        guard wanted else { return }

        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.devicesChanged() }
        }
        RunLoop.main.add(timer, forMode: .common)
        routeTimer = timer
    }

    func start() {
        refreshRoute()
        guard permission == .authorized, route.usable else {
            // Nothing to play into, or nothing we are willing to play into.
            // The window says which; there is no point starting to find out.
            if running || starting { stop() }
            return
        }

        starting = true
        errorMessage = nil
        AudioLifecycle.start(config) { [weak self] error in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.starting = false
                if let error {
                    self.running = false
                    self.errorMessage = error.isEmpty
                        ? "The audio device would not start." : error
                } else {
                    self.errorMessage = nil
                }
                // `running` comes from the C side rather than from which
                // callback this is, so a start that was overtaken by a newer
                // one cannot leave a stale answer behind.
                self.refreshStatus()
                self.startMetering()
            }
        }
    }

    func stop() {
        meterTimer?.invalidate()
        meterTimer = nil
        restartWork?.cancel()
        starting = false
        running = false
        AudioLifecycle.stop { [weak self] in
            MainActor.assumeIsolated { self?.refreshStatus() }
        }
    }

    /// The app is going away.  An async stop would race the process exiting,
    /// and the device has to be handed back -- with the sample rate and
    /// buffer size we changed put back -- before that happens.
    func shutdown() {
        meterTimer?.invalidate()
        meterTimer = nil
        routeTimer?.invalidate()
        routeTimer = nil
        restartWork?.cancel()
        starting = false
        running = false
        AudioLifecycle.shutdown()
    }

    /// Settings that need the stream rebuilt.  Coalesced, because dragging
    /// through a device menu would otherwise tear the stream down once per
    /// item the highlight passes over.
    private func restart() {
        restartWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            MainActor.assumeIsolated { self?.start() }
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
        let found = AudioDevice.all()
        let resolved = AudioLifecycle.route(for: config)
        // Asked on every activation as well as on every CoreAudio
        // notification, so it has to be free when nothing moved: restarting
        // the stream each time the window is clicked would be a gap in the
        // sound for no reason.
        guard found != devices || resolved != route else { return }
        devices = found
        route = resolved
        watchBlockedRoute()

        // The stream holds a device ID, and IDs do not survive a device going
        // away.  Rebuilding is cheap and is the only way to follow a player
        // unplugging their interface mid-tune -- or plugging headphones in,
        // which is what turns a refused route into a playable one.
        restart()
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

    /// Assign only if the value actually changed.
    ///
    /// Every `@Published` write sends `objectWillChange`, so an unconditional
    /// assignment invalidates the whole window whether or not it changed
    /// anything.  The fields below are read from the engine 24 times a second
    /// and almost never differ -- the device names and the latencies change
    /// when a device changes, which is to say hardly ever -- so writing them
    /// blind was sending some 400 invalidations a second to say nothing.
    private func set<T: Equatable>(_ path: ReferenceWritableKeyPath<SynthController, T>,
                                   _ value: T) {
        if self[keyPath: path] != value { self[keyPath: path] = value }
    }

    private func refreshStatus() {
        var status = WhistleStatus()
        whistle_status(&status)

        // The meters are the only thing here that really does change on
        // every tick, and they are on their own object so that they
        // re-render the two views that draw them rather than the window.
        meters.update(from: status)

        set(\.running, status.running)
        set(\.actualSampleRate, status.sample_rate)
        set(\.actualBufferFrames, Int(status.buffer_frames))
        set(\.inputLatencyMs, status.input_latency_ms)
        set(\.outputLatencyMs, status.output_latency_ms)
        set(\.detectionLagMs, status.detection_lag_ms)
        set(\.splitDevices, status.split_devices)
        set(\.inputName, whistleString(status.input_name))
        set(\.outputName, whistleString(status.output_name))
        set(\.xruns, Int(status.xruns))
        set(\.dropouts, Int(status.dropouts))
    }

    // MARK: - Readouts

    var roundTripMs: Double { inputLatencyMs + outputLatencyMs }

    /// The nearest note name to what is being detected is on `Meters`, which
    /// is where the pitch it reads now lives.

    /// The same, without asking whether it is being played -- for saying what
    /// was heard and refused.
    static func noteName(withCents hz: Float) -> String {
        let midi = 69 + 12 * log2(Double(hz) / 440)
        let rounded = Int(midi.rounded())
        let cents = Int(((midi - Double(rounded)) * 100).rounded())
        return "\(noteName(rounded)) \(cents >= 0 ? "+" : "")\(cents)¢"
    }
}
