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
            "Whistle Synth listens to you whistle, works out what note you are on, and plays that note on a synth voice. It follows the pitch continuously, so a slide comes out as a slide, and it follows how hard you are blowing, so a note you lean into opens up.",
            "It is monophonic: one note at a time, the one you are whistling.",
        ]),
        HelpTopic(title: "You need headphones", paragraphs: [
            "This Mac's speakers sit a few inches from its microphone and point at it. Played through them, the synth hears its own output, follows it, and howls within a second — so the app will not run in that configuration, and says so until you fix it.",
            "Headphones fix it. So does any audio interface, or any microphone that is not the built-in one. Plug something in and it starts on its own.",
        ]),
        HelpTopic(title: "Getting a sound", paragraphs: [
            "Whistle steadily. The Play tab's Detected row should show a note name; if it stays blank, whistle louder or lower the Gate.",
            "Gate is how far above the room a sound has to be before it counts as a note. Raise it if the app plays notes when you are not whistling; lower it if quiet whistling is ignored.",
            "Volume is the output level, about 3.5 dB a step.",
        ]),
        HelpTopic(title: "Choosing a voice", paragraphs: [
            "The Play tab lists the voices. They are all bass sounds of one kind or another, chosen to carry through a PA in mono.",
            "Passthrough is not a synth voice: it plays your microphone straight back, which is how you set your input level and check that the right microphone is selected.",
        ]),
        HelpTopic(title: "Down a fifth, and sustain", paragraphs: [
            "Down a fifth makes what you whistle the fifth of what you hear rather than the root, so a tune whistled in D comes out in G. Useful for playing a bass line under a tune you are thinking of in its own key.",
            "Sustain lets a note you hold for half a second carry on after your breath does: it settles onto the nearest real note and stays there, so one held note every couple of bars puts a drone under the tune. The drone does not time out — play one short note to end it, or another long one to move it. Short notes are untouched. Some voices opt out, and the Play tab says so when the one you are on does.",
        ]),
        HelpTopic(title: "Editing a voice", paragraphs: [
            "The Voice tab edits whichever voice you are playing, and you hear the change as you make it. Anything you change is marked \"edited\" and kept between launches; anything you leave alone follows the built-in preset, including when a later version improves it.",
            "Full-blow level is the one worth setting for yourself: whistle your loudest, read the \"While playing\" number on the Play tab, and set full-blow level to what it says. That is what tells the voice how hard you are actually blowing.",
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
