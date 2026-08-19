import Foundation

/// One CoreAudio device, as the UI thinks of it.
///
/// Identified by UID rather than by AudioDeviceID: the numeric ID is only
/// good for one launch, but the UID survives reboots and unplugging, which is
/// what a remembered preference needs.
struct AudioDevice: Identifiable, Hashable {
    let uid: String
    let name: String
    let inputChannels: Int
    let outputChannels: Int
    let sampleRate: Double
    let isDefaultInput: Bool
    let isDefaultOutput: Bool

    var id: String { uid }

    var canRecord: Bool { inputChannels > 0 }
    var canPlay: Bool { outputChannels > 0 }

    /// "Scarlett 2i2 USB — 2 in, 2 out"
    func summary(forInput: Bool) -> String {
        let channels = forInput ? inputChannels : outputChannels
        return "\(name) — \(channels) ch"
    }

    static func all() -> [AudioDevice] {
        var raw = [WhistleDevice](repeating: WhistleDevice(),
                                  count: Int(WHISTLE_MAX_DEVICES))
        let count = raw.withUnsafeMutableBufferPointer { buffer in
            Int(whistle_list_devices(buffer.baseAddress, Int32(buffer.count)))
        }
        return raw.prefix(count).map { device in
            AudioDevice(uid: string(from: device.uid),
                        name: string(from: device.name),
                        inputChannels: Int(device.input_channels),
                        outputChannels: Int(device.output_channels),
                        sampleRate: device.sample_rate,
                        isDefaultInput: device.is_default_input,
                        isDefaultOutput: device.is_default_output)
        }
    }
}

/// Fixed-size C char arrays arrive in Swift as tuples, which is the only
/// awkward part of the bridge.
private func string<T>(from tuple: T) -> String {
    withUnsafeBytes(of: tuple) { raw in
        guard let base = raw.baseAddress?.assumingMemoryBound(to: CChar.self)
        else { return "" }
        return String(cString: base)
    }
}

func whistleString<T>(_ tuple: T) -> String { string(from: tuple) }

/// Copies a Swift string into one of the fixed C char arrays, truncating
/// rather than overrunning.
func setCString<T>(_ tuple: inout T, _ value: String) {
    withUnsafeMutableBytes(of: &tuple) { raw in
        guard let base = raw.baseAddress?.assumingMemoryBound(to: CChar.self)
        else { return }
        let bytes = Array(value.utf8.prefix(raw.count - 1))
        for (index, byte) in bytes.enumerated() {
            base[index] = CChar(bitPattern: byte)
        }
        base[bytes.count] = 0
    }
}
