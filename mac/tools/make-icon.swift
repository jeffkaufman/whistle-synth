// Draws the app icon and writes both the asset catalog Xcode wants and the
// .icns that the Xcode-free build script wants, so there is one drawing
// rather than two that can drift apart.
//
//   swift mac/tools/make-icon.swift
//
// The picture is what the app does: a whistle goes in on the left as a clean
// sine, and comes out on the right as the pulse wave the synth is built from.

import AppKit
import CoreGraphics
import Foundation

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let iconset = root.appendingPathComponent("mac/build/AppIcon.iconset")
let appiconset = root.appendingPathComponent(
    "mac/WhistleSynth/Assets.xcassets/AppIcon.appiconset")

func draw(size: CGFloat) -> CGImage {
    let space = CGColorSpaceCreateDeviceRGB()
    let context = CGContext(
        data: nil, width: Int(size), height: Int(size), bitsPerComponent: 8,
        bytesPerRow: 0, space: space,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.setAllowsAntialiasing(true)
    context.interpolationQuality = .high

    // macOS icons sit in a rounded square inset from the full canvas.
    let inset = size * 0.086
    let rect = CGRect(x: inset, y: inset,
                      width: size - inset * 2, height: size - inset * 2)
    let radius = rect.width * 0.2237
    let plate = CGPath(roundedRect: rect, cornerWidth: radius,
                       cornerHeight: radius, transform: nil)

    context.saveGState()
    context.addPath(plate)
    context.clip()
    let gradient = CGGradient(
        colorsSpace: space,
        colors: [CGColor(red: 0.129, green: 0.153, blue: 0.325, alpha: 1),
                 CGColor(red: 0.086, green: 0.408, blue: 0.451, alpha: 1),
                 CGColor(red: 0.180, green: 0.639, blue: 0.545, alpha: 1)] as CFArray,
        locations: [0, 0.62, 1])!
    context.drawLinearGradient(
        gradient, start: CGPoint(x: rect.minX, y: rect.maxY),
        end: CGPoint(x: rect.maxX, y: rect.minY), options: [])
    context.restoreGState()

    let midY = size / 2
    let line = size * 0.045
    context.setLineWidth(line)
    context.setLineCap(.round)
    context.setLineJoin(.round)
    context.setStrokeColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.96))
    context.setShadow(offset: CGSize(width: 0, height: -size * 0.012),
                      blur: size * 0.03,
                      color: CGColor(red: 0, green: 0, blue: 0, alpha: 0.35))

    let left = rect.minX + rect.width * 0.125
    let right = rect.maxX - rect.width * 0.125
    let split = left + (right - left) * 0.5
    let amplitude = rect.height * 0.2

    // The whistle: one clean cycle, ending at the centre on its way up.
    let path = CGMutablePath()
    path.move(to: CGPoint(x: left, y: midY))
    var x = left
    while x <= split {
        let progress = (x - left) / (split - left)
        path.addLine(to: CGPoint(x: x, y: midY + sin(progress * .pi * 2) * amplitude))
        x += size / 512
    }

    // The synth: the pulse wave it becomes.  The sine arrives moving upward,
    // so the first edge continues it rather than interrupting it.
    let half = (right - split) / 2
    var edge = split
    var high = true
    while edge < right - 1 {
        let next = min(right, edge + half)
        path.addLine(to: CGPoint(x: edge, y: midY + (high ? amplitude : -amplitude)))
        path.addLine(to: CGPoint(x: next, y: midY + (high ? amplitude : -amplitude)))
        edge = next
        high.toggle()
    }

    context.addPath(path)
    context.strokePath()
    return context.makeImage()!
}

func write(_ image: CGImage, to url: URL) throws {
    let rep = NSBitmapImageRep(cgImage: image)
    rep.size = NSSize(width: image.width, height: image.height)
    guard let data = rep.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "icon", code: 1)
    }
    try data.write(to: url)
}

let fm = FileManager.default
try? fm.removeItem(at: iconset)
try fm.createDirectory(at: iconset, withIntermediateDirectories: true)
try fm.createDirectory(at: appiconset, withIntermediateDirectories: true)

// (point size, scale) -- the full set macOS asks for.
let variants: [(Int, Int)] = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1),
                              (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)]

var entries: [[String: String]] = []
var rendered: [Int: CGImage] = [:]

for (points, scale) in variants {
    let pixels = points * scale
    let image = rendered[pixels] ?? draw(size: CGFloat(pixels))
    rendered[pixels] = image

    let name = "icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"
    try write(image, to: iconset.appendingPathComponent(name))
    try write(image, to: appiconset.appendingPathComponent(name))
    entries.append(["size": "\(points)x\(points)", "idiom": "mac",
                    "filename": name, "scale": "\(scale)x"])
}

let contents: [String: Any] = [
    "images": entries,
    "info": ["version": 1, "author": "xcode"],
]
try JSONSerialization
    .data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
    .write(to: appiconset.appendingPathComponent("Contents.json"))

print("wrote \(iconset.path) and \(appiconset.path)")
