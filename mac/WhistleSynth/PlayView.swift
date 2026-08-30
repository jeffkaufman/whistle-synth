import SwiftUI

/// The tab the app opens on, and the only one that has to work while someone
/// is playing.
///
/// So it is a page rather than a `Form`: the things a player touches --
/// voice, volume, gate, the octave, the range, the fifth, the sustain and a
/// mute -- all visible at once, all big enough to hit without aiming, and each one showing
/// its state from across a room.  Nothing here scrolls, because a control you
/// have to go looking for mid-tune is a control you do not use.
///
/// The three switches share a row and the range is one line, which is what
/// pays for the grid of voices and the two rows of ten being as big as they
/// are: the things adjusted *while* playing get the space, and the things set
/// once get a line.
///
/// Everything that is a *reading* rather than a control has moved to the tabs
/// that use it: the meters and the detected note are on the Voice tab, next
/// to the full-blow level they are for.  What survives here is one line of
/// "it can hear you", which is the only readout worth a glance while playing.
struct PlayView: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            voices
            levels
            pitchControls
            switches
            Spacer(minLength: 0)
            HearingStrip()
        }
        .padding(20)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    // MARK: - Voice

    private var voices: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                SectionLabel("Voice")
                Spacer()
                // Passthrough is a tool, not a voice -- it plays the
                // microphone straight back for setting the input level and
                // the gate -- so it sits beside the heading rather than
                // taking a tile in the grid and being picked by accident.
                //
                // It is a round trip: pressing it again puts back the voice
                // that was playing.  The label does not change to say so,
                // because a wider label would move the button, and the moment
                // this matters most is the moment it is howling and someone
                // is clicking the place they just clicked.
                Button("Passthrough") { synth.togglePassthrough() }
                    .buttonStyle(TileButtonStyle(selected: synth.isPassthrough,
                                                 minHeight: 22,
                                                 font: .caption))
                    .fixedSize()
                    .help(synth.isPassthrough
                          ? "Go back to \(SynthController.displayName(ofVoice: synth.comeBackToVoice))."
                          : "Plays the microphone straight back, for setting the input level and the gate. Click it again to come back.")
            }

            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8),
                                     count: 4),
                      spacing: 8) {
                ForEach(Array(1..<synth.voiceCount), id: \.self) { voice in
                    Button(SynthController.displayName(ofVoice: voice)) {
                        synth.voice = voice
                    }
                    .buttonStyle(TileButtonStyle(selected: synth.voice == voice,
                                                 minHeight: 42))
                    .accessibilityAddTraits(synth.voice == voice ? [.isSelected] : [])
                }
            }
        }
    }

    // MARK: - Level

    private var levels: some View {
        VStack(alignment: .leading, spacing: 10) {
            StepBar(title: "Volume", value: $synth.volume,
                    hint: "how loud it plays")
            StepBar(title: "Gate", value: $synth.gate,
                    // Which way it runs is the whole of what a caption here
                    // can usefully say: the number goes up as the gating goes
                    // down, and nobody guesses that from "gate".
                    hint: "how far above the room a note has to be — higher gates less")
            // A playing control, not a voice one: it is an input level in
            // input units, so what it describes is the microphone and the
            // player rather than any sound.  The mark under the bar is where
            // your loudest whistle actually landed, which turns "set this to
            // the number you measured" into "click where the mark is".
            StepBar(title: "Full blow", value: $synth.fullBlow,
                    hint: "how hard you blow to reach the top of a voice",
                    valueText: String(format: "%.3f", synth.fullBlowLevel),
                    marker: synth.playingStep)
        }
    }

    // MARK: - Octave and range

    /// The two pitch controls, on one line so that each label sits against
    /// the thing it labels: where the voice plays, and what it will play at
    /// all.  Neither is a knob -- they are set and then left -- so they share
    /// the row that the volume and the gate each get to themselves.
    ///
    /// The captions they used to carry are gone with the second line.  The
    /// controls say what they are: two buttons and a signed number, and two
    /// note names with "to" between them.  What is left to explain is why you
    /// would move them, which is what the tooltips and the Help window are
    /// for.
    private var pitchControls: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            octave
            Spacer(minLength: 16)
            range
        }
    }

    /// Whole octaves on top of wherever the voice already sits, which is the
    /// one transposition that costs nothing musically: an octave is a power
    /// of two, so the tuning and the sustain's snap are exactly where they
    /// were.  Two buttons and a number rather than a menu, because it is
    /// reached for mid-set -- the same bass voice down one to sit under a
    /// tune, up two to play it.
    @ViewBuilder private var octave: some View {
        SectionLabel("Octave")
        HStack(spacing: 4) {
            BumpButton(systemImage: "minus", label: "Octave down") {
                synth.bumpOctave(-1)
            }
            .disabled(synth.octaveShift <= -SynthController.octaveLimit)

            // Fixed width, so the two buttons do not shuffle sideways as the
            // number changes under them.
            Text(synth.octaveLabel)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .monospacedDigit()
                .frame(width: 30)
                .accessibilityHidden(true)

            BumpButton(systemImage: "plus", label: "Octave up") {
                synth.bumpOctave(1)
            }
            .disabled(synth.octaveShift >= SynthController.octaveLimit)
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Octave")
        .accessibilityValue(synth.octaveShift == 0
                            ? "the voice as written"
                            : "\(synth.octaveLabel) octaves")
        .help("Moves every voice up or down whole octaves, on top of the octave it already plays at. Nothing about the tuning changes.")

        // After the buttons rather than before them, so that it appearing
        // does not shift the thing it is about.
        if synth.octaveShift != 0 {
            Button("Default") { synth.octaveShift = 0 }
                .buttonStyle(.link)
                .font(.caption)
        }
    }

    /// Which notes count as notes.  Two menus, because the choice is a note
    /// name and a note name is what they show.
    ///
    /// It narrows *and* widens.  Narrowing is the common use: a whistle's own
    /// wobble at the start of a note, and the octave the detector
    /// occasionally hears under a soft one, both land below the tune, and a
    /// room -- a chair, a cymbal, a squeak -- can be periodic enough to be
    /// played as a note.  Widening reaches down to F3 and up to E9, the
    /// lowest and highest notes anyone has been recorded whistling, and the
    /// low end of that is paid for in detection lag: the menu says so, and
    /// the Audio tab shows the number.
    @ViewBuilder private var range: some View {
        SectionLabel("Range")
        NotePicker(selection: $synth.lowNote, label: "Lowest note")
            .help("The lowest note that will play. Below \(SynthController.noteName(SynthController.defaultLowNote)) the detector needs a longer analysis window to find a period that long, which adds detection lag — the Audio tab shows how much.")
        Text("to")
            .font(.caption)
            .foregroundStyle(.secondary)
        NotePicker(selection: $synth.highNote, label: "Highest note")
            .help("The highest note that will play. Costs nothing to raise.")
        if synth.rangeIsCustom {
            Button("Default") { synth.resetRange() }
                .buttonStyle(.link)
                .font(.caption)
        }
    }

    // MARK: - The two switches

    /// Neither belongs to the patch: which voice you are playing, what
    /// interval you are playing it at, and whether the line breathes with you
    /// are three separate decisions.  Both stay put across a voice change.
    private var switches: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                SwitchTile(title: "Down a fifth",
                           caption: "whistle in D, hear G",
                           systemImage: "arrow.down.right",
                           isOn: $synth.fifth)
                SwitchTile(title: "Sustain",
                           caption: "held notes carry on",
                           systemImage: "waveform.path.ecg",
                           isOn: $synth.sustain)
                // The same shape as the other two because it is the same kind
                // of thing -- on or off, and it stays where you put it -- but
                // red, because the one question it has to answer instantly is
                // "is this why there is no sound".
                SwitchTile(title: "Mute",
                           caption: "or the M key",
                           systemImage: synth.muted ? "speaker.slash.fill"
                                                    : "speaker.wave.2.fill",
                           isOn: $synth.muted,
                           tint: .red)
                    // No modifier: a modifier is two hands, and nothing in
                    // this app takes typed text.
                    .keyboardShortcut("m", modifiers: [])
            }
            // A switch that silently does nothing is how a switch gets a
            // reputation for being broken.  Read from the live parameters
            // rather than a list of names, so an edited voice reports what it
            // will actually do.
            if synth.sustain, !synth.isPassthrough, synth.currentParams.no_sustain {
                Label("\(synth.currentVoiceDisplayName) ignores the sustain: its whole shape is a note speaking and getting out of the way.",
                      systemImage: "info.circle")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

// MARK: - Pieces

private struct SectionLabel: View {
    let text: String
    init(_ text: String) { self.text = text }

    var body: some View {
        Text(text.uppercased())
            .font(.caption.weight(.semibold))
            .kerning(0.6)
            .foregroundStyle(.secondary)
    }
}

/// One look for everything that is either on or off, because "which one is
/// selected" has to survive a glance from further away than a mouse pointer:
/// selected is a filled accent block, everything else is a faint outline.
struct TileButtonStyle: ButtonStyle {
    var selected: Bool
    var minHeight: CGFloat = 42
    var font: Font = .system(size: 13)

    func makeBody(configuration: Configuration) -> some View {
        let shape = RoundedRectangle(cornerRadius: 7, style: .continuous)
        configuration.label
            .font(selected ? font.weight(.semibold) : font)
            .lineLimit(1)
            .minimumScaleFactor(0.75)
            .padding(.horizontal, 8)
            .frame(maxWidth: .infinity, minHeight: minHeight)
            .foregroundStyle(selected ? Color.white : Color.primary)
            .background {
                shape.fill(selected
                           ? Color.accentColor.opacity(configuration.isPressed ? 0.8 : 1)
                           : Color.primary.opacity(configuration.isPressed ? 0.14 : 0.05))
            }
            .overlay {
                shape.strokeBorder(Color.primary.opacity(selected ? 0 : 0.15))
            }
            .contentShape(shape)
    }
}

/// The 0-9 knobs, still 0-9 because that is what they have always been and
/// because ten steps is the right number for something adjusted while
/// playing -- but as ten targets you hit directly rather than a slider you
/// drag to a number you then have to read.  It fills like a meter, so the
/// setting is legible without reading anything at all.
struct StepBar: View {
    let title: String
    @Binding var value: Int
    let hint: String
    /// What to show on the right instead of the step number, for a knob whose
    /// step number is not the interesting part.
    var valueText: String? = nil
    /// Where to put a mark under the bar, on the same 0-9 scale, or nil.
    var marker: Double? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                SectionLabel(title)
                Text(hint)
                    .font(.caption)
                    .foregroundStyle(.tertiary)
                Spacer()
                Text(valueText ?? "\(value)")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            HStack(spacing: 3) {
                ForEach(0...9, id: \.self) { step in
                    Button("\(step)") { value = step }
                        .buttonStyle(StepTileStyle(filled: step <= value,
                                                   current: step == value))
                }
            }
            .overlay(alignment: .bottomLeading) { markerView }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(title)
            .accessibilityValue("\(value) of 9")
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: value = min(9, value + 1)
                case .decrement: value = max(0, value - 1)
                @unknown default: break
                }
            }
        }
    }

    /// A caret sitting on the bar at the marked step.  Placed by fraction of
    /// the whole bar rather than by tile, so it can land between two of them
    /// -- which is the point: it says "about here", and where it points is
    /// the button to press.
    ///
    /// Big, because it is only on screen while someone is looking for it --
    /// see `showingFullBlowMarker` -- and something that appears for half a
    /// minute and then goes may as well be seen from the far side of the
    /// room.
    @ViewBuilder private var markerView: some View {
        if let marker {
            GeometryReader { geometry in
                Image(systemName: "arrowtriangle.up.fill")
                    .font(.system(size: 14))
                    .foregroundStyle(Color.orange)
                    .offset(x: geometry.size.width * (marker + 0.5) / 10 - 8,
                            y: geometry.size.height - 6)
                    .animation(.easeOut(duration: 0.2), value: marker)
            }
            .allowsHitTesting(false)
        }
    }
}

private struct StepTileStyle: ButtonStyle {
    var filled: Bool
    var current: Bool

    func makeBody(configuration: Configuration) -> some View {
        let shape = RoundedRectangle(cornerRadius: 5, style: .continuous)
        configuration.label
            .font(.system(size: 11, weight: current ? .bold : .regular))
            .monospacedDigit()
            .frame(maxWidth: .infinity, minHeight: 32)
            .foregroundStyle(current ? Color.white
                             : filled ? Color.primary : Color.secondary)
            .background {
                shape.fill(current ? Color.accentColor
                           : filled ? Color.accentColor.opacity(0.28)
                           : Color.primary.opacity(configuration.isPressed ? 0.14 : 0.05))
            }
            .opacity(configuration.isPressed ? 0.75 : 1)
            .contentShape(shape)
    }
}

/// One of the octave buttons: a plain square target, sized to be hit without
/// aiming rather than to match the menus beside it.
private struct BumpButton: View {
    let systemImage: String
    let label: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: 11, weight: .semibold))
                .frame(width: 34, height: 26)
        }
        .buttonStyle(TileButtonStyle(selected: false, minHeight: 26))
        .fixedSize()
        .accessibilityLabel(label)
    }
}

/// The note menus.  Fixed width, so picking a note does not move the one
/// beside it, and every note the detector can find is in the list -- the
/// range is the player's to narrow, not the app's to suggest.
private struct NotePicker: View {
    @Binding var selection: Int
    let label: String

    var body: some View {
        Picker(label, selection: $selection) {
            ForEach(SynthController.lowestNote...SynthController.highestNote,
                    id: \.self) { note in
                Text(SynthController.noteName(note)).tag(note)
            }
        }
        .labelsHidden()
        .pickerStyle(.menu)
        .frame(width: 78)
        .accessibilityLabel(label)
    }
}

/// A big two-state control with room to say what it does, for the two
/// switches someone who has never played this has no way to guess at.
private struct SwitchTile: View {
    let title: String
    let caption: String
    let systemImage: String
    @Binding var isOn: Bool
    var tint: Color = .accentColor

    var body: some View {
        Button { isOn.toggle() } label: {
            HStack(spacing: 9) {
                Image(systemName: systemImage)
                    .font(.system(size: 15))
                    .frame(width: 18)
                VStack(alignment: .leading, spacing: 1) {
                    Text(title)
                        .font(.system(size: 13, weight: .semibold))
                    Text(caption)
                        .font(.caption)
                        .opacity(0.75)
                }
                Spacer(minLength: 4)
                Image(systemName: isOn ? "checkmark.circle.fill" : "circle")
                    .font(.system(size: 15))
                    .opacity(isOn ? 1 : 0.35)
            }
            .lineLimit(1)
            .padding(.horizontal, 11)
            .frame(maxWidth: .infinity, minHeight: 46)
        }
        .buttonStyle(SwitchTileStyle(isOn: isOn, tint: tint))
        .accessibilityLabel(title)
        .accessibilityValue(isOn ? "On" : "Off")
        .accessibilityAddTraits(isOn ? [.isSelected] : [])
    }
}

private struct SwitchTileStyle: ButtonStyle {
    var isOn: Bool
    var tint: Color = .accentColor

    func makeBody(configuration: Configuration) -> some View {
        let shape = RoundedRectangle(cornerRadius: 7, style: .continuous)
        configuration.label
            .foregroundStyle(isOn ? Color.white : Color.primary)
            .background {
                shape.fill(isOn
                           ? tint.opacity(configuration.isPressed ? 0.8 : 1)
                           : Color.primary.opacity(configuration.isPressed ? 0.14 : 0.05))
            }
            .overlay { shape.strokeBorder(Color.primary.opacity(isOn ? 0 : 0.15)) }
            .contentShape(shape)
    }
}

/// The one readout worth having while playing: is it hearing me, and does it
/// agree with me about what note that was.  Everything else about the signal
/// is a setting-up question and lives where the setting is.
private struct HearingStrip: View {
    @EnvironmentObject private var synth: SynthController

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "mic.fill")
                .font(.caption)
                .foregroundStyle(.secondary)
            MeterView(level: synth.inputPeak, warn: 0.9)
            Text(synth.detectedNote ?? "—")
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(synth.voiced ? .primary : .secondary)
                .frame(width: 90, alignment: .leading)
            // "Nothing is happening" is the hardest thing to work out while
            // playing, and a range someone set and forgot is one of the ways
            // to arrive at it.  Say which note was refused, not just that one
            // was.
            if let refused = synth.refusedNote {
                Text("\(refused) — outside the range")
                    .font(.caption)
                    .foregroundStyle(.orange)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
        }
    }
}

/// Stands in for `LabeledContent`, which cannot be used here.
///
/// `LabeledContent` inside a grouped `Form` inside a `TabView` puts SwiftUI's
/// attribute graph into a cycle on macOS 26 the moment a tab is re-inserted --
/// switch to another tab and back and it starts spinning.  What actually
/// freezes the app is the *reporting*: AttributeGraph writes several million
/// "cycle detected" lines a second to stderr, which is a synchronous write on
/// the main thread.  Bisected to this view and nothing else; every other part
/// of these tabs is innocent, and a plain HStack does the same job.
struct StatRow<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        HStack {
            Text(title)
            Spacer(minLength: 12)
            content
                // What LabeledContent would have done to the value side.
                .foregroundStyle(.secondary)
        }
    }
}

struct MeterView: View {
    let level: Float
    let warn: Float

    private let width: CGFloat = 220
    private let height: CGFloat = 10

    var body: some View {
        ZStack(alignment: .leading) {
            Capsule().fill(Color.secondary.opacity(0.15))
            Capsule()
                .fill(level >= warn ? Color.red : Color.accentColor)
                .frame(width: width * CGFloat(scaled))
        }
        .frame(width: width, height: height)
        .accessibilityLabel("Level")
        .accessibilityValue(String(format: "%.2f", level))
    }

    /// A square root, so that quiet playing is visible at all: the useful
    /// range for setting a microphone is the bottom of the linear scale.
    private var scaled: Float {
        min(1, level > 0 ? sqrt(level) : 0)
    }
}
