import SwiftUI

/// What the Help menu opens.
///
/// The default SwiftUI menu bar puts a "Whistle Synth Help" item in, and with
/// no help book behind it the item shows "Help isn't available for Whistle
/// Synth."  A window of plain text is a small thing to ship and it works with
/// no network, which a help book on a website does not.
struct HelpView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                ForEach(HelpTopic.all) { topic in
                    VStack(alignment: .leading, spacing: 6) {
                        Text(topic.title)
                            .font(.headline)
                        ForEach(Array(topic.paragraphs.enumerated()), id: \.offset) { _, text in
                            Text(text)
                                .fixedSize(horizontal: false, vertical: true)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
            .padding(28)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(minWidth: 460, idealWidth: 520, minHeight: 420, idealHeight: 620)
    }
}

struct HelpTopic: Identifiable {
    let title: String
    let paragraphs: [String]
    var id: String { title }

    static let all: [HelpTopic] = [
        HelpTopic(title: "What it does", paragraphs: [
            "Whistle Synth is a whistle-controlled synthesizer.  It listens to you whistle, works out what note you are on, and plays that note on a synth voice. It follows the pitch continuously, which means you can slide around.  It also tracks how loudly you're whistling, which on most voices controls the output volume.",
            "It is monophonic: one note at a time, the one you are whistling.",
        ]),
        HelpTopic(title: "You need headphones or a separate mic", paragraphs: [
            "On a typical Mac, the built-in mic and speakers are very close together.  This doesn't work: the synth will hear its own output.  To avoid this, the app refuses to run with the built-in mic and speaker.",
            "Instead, either use headphones or a mic that's right up by your mouth."
        ]),
        HelpTopic(title: "Getting a sound", paragraphs: [
            "Whistle steadily. The note name at the bottom of the Play tab should show what you are on; if it stays blank, whistle louder, lower the Gate, or bring your mouth closer to the mic.",
            "The mic in a typical Mac laptop doesn't work well for this.  You'll get best results with an sm58 or better plugged into an audio interface.",
            "Gate is how far above the room a sound has to be before it counts as a note, and the number runs the other way round: higher gates less. Raise it if quiet whistling is ignored; lower it if the app plays notes when you are not whistling.",
            "Volume is the output level, 3.5 dB a step.  Click the number you want.",
            "Full blow is the input level that counts as blowing as hard as you are going to — it is what the voices measure your dynamics against, so it depends on your microphone and on you rather than on the voice.",
            "To set it: whistle your loudest, then click anywhere on the Full blow bar. An orange mark appears showing where your loudest whistle actually landed — click the step it points at. The mark stays for half a minute and then gets out of the way, so it is only there while you are setting it.",
            "Mute silences the output without stopping anything, and you can trigger it by pressing the M key.",
        ]),
        HelpTopic(title: "Moving the octave", paragraphs: [
            "The − and + buttons move every voice up or down a whole octave, on top of wherever that voice already sits.",
        ]),
        HelpTopic(title: "Setting the range", paragraphs: [
            "Range is the lowest and highest note that will play anything. Outside it a whistle is still heard — it just does not trigger a note, and the line at the bottom of the Play tab says which note was refused.",
            "It starts on the ordinary whistle range, C♯5 to G7, and the menus go from F3 to E9 — the lowest and highest notes anyone has been recorded whistling. There are two reasons to narrow it. A whistle wobbles at the start of a note and the detector sometimes hears the octave below a soft one, both of which land under your tune: set the lowest note to the bottom of what you actually play and they stop. And a room — a chair, a cymbal, a squeak — can be periodic enough to be played as a note, which the highest note stops.",
            "Widening it upwards is free. Widening it downwards is not: finding a very low note takes a longer listen, so the app hears about your pitch changes a little later — 12ms instead of 4 at the very bottom, which the Audio tab shows. Nothing is delayed on the way through either way.",
            "Both ends include the note you named, so a lowest note of C5 still triggers on a C5 whistled a little flat. \"Default\" puts the range back to C♯5–G7.",
        ]),
        HelpTopic(title: "Choosing a voice", paragraphs: [
            "The Play tab is a grid of the voices; the one you are playing is the filled one.  They should sound good in either stereo or mono.",
            "Passthrough, in the corner above the grid, is not a synth voice: it plays your microphone straight back, which is how you set your input level and check that the right microphone is selected.  Note that if your mic can hear your speaker well this can cause feedback, so be careful with your mic placement.",
        ]),
        HelpTopic(title: "Down a fifth, and sustain", paragraphs: [
            "Down a fifth makes what you whistle the fifth of what you hear rather than the root, so a tune whistled in D comes out in G. You're effectively whistling the third harmonic where the synth plays the second, which is what makes it a just fifth — two cents flat of a tempered one.  This can be helpful for whistling something in a key that's not a good fit for your range.",
            "Sustain lets a note you hold for half a second carry on after your breath does: it settles onto the nearest concert pitch note and stays there.  The idea is that you can choose a drone by whistling, and then play another instrument until you're ready to choose another drone.  It doesn't stop on its own; whistle a short note (or turn off sustain) to quiet it.  Notes you hold for less than half a second are left alone.",
        ]),
        HelpTopic(title: "Editing a voice", paragraphs: [
            "The Voice tab edits whichever voice you are playing, and you hear the change as you make it. Anything you change is marked \"edited\" and kept between launches; anything you leave alone follows the built-in preset.",
            "Reset this voice puts one voice back; Reset all voices puts all of them back.",
        ]),
        HelpTopic(title: "Latency", paragraphs: [
            "The Audio tab shows the round trip: how long it takes a sound to get in, through, and back out. Smaller buffers make it shorter and ask more of the machine. If the dropout count climbs while you play, go up a buffer size.",
            "Detection lag is separate and is not buffering. Nothing is held up on the way through; it is how late the synth hears about a pitch change, because working out a pitch at all needs a window of signal to look at.",
            "The buffer size is Whistle Synth's own — other apps on the same device keep theirs, whatever you set here. The sample rate is different: a device runs at one rate for everything using it, so choosing one changes it for them too and interrupts whatever is playing. That is why it starts on \"Device default\", and why whatever you do change is put back when Whistle Synth stops.",
        ]),
        HelpTopic(title: "Privacy", paragraphs: [
            "The microphone is used to follow your whistling and nothing else. Audio is processed on this Mac, in memory, as it arrives. Nothing is recorded, nothing is written to disk, and nothing is sent anywhere. The app has no network access at all.",
        ]),
    ]
}
