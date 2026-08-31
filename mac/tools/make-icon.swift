// Draws the app icon and writes the asset catalog Xcode wants, the .icns the
// Xcode-free build script wants, and the 1024px PNG App Store Connect wants,
// so there is one drawing rather than three that can drift apart.
//
//   swift mac/tools/make-icon.swift
//
// The picture is what the app is: a referee's whistle that is also a synth,
// panel and all.  It is drawn rather than traced so the small sizes can shed
// detail -- the panel goes below 128px, and below 64px the resonator's rim
// becomes a solid hole -- while every size keeps the same silhouette.
//
// Every measurement lives in Geometry below, in grid units where one unit is
// 7.66px of the 1024px reference drawing, x counted right from the artwork's
// left edge and y up from its bottom.  Keeping them in one struct is what lets
// the fitting harness sweep them against that reference; the values here are
// what that search settled on.  The tube slopes down to the right, and the
// slope lives in the geometry rather than in a rotation, because the chamber is
// an upright ellipse that tipping over would spoil.

import AppKit
import CoreGraphics
import Foundation

// MARK: - shared drawing (the fitting harness splices out everything to the
// matching end marker, so nothing above it may touch the filesystem)

let navy = CGColor(red: 0.106, green: 0.169, blue: 0.263, alpha: 1)
let green = CGColor(red: 0.592, green: 0.902, blue: 0.012, alpha: 1)
let orange = CGColor(red: 0.918, green: 0.518, blue: 0.200, alpha: 1)

struct Geometry {
    // The resonator, and the navy rim inside it.
    var chamberX: CGFloat = 28.6
    var chamberY: CGFloat = 21.6
    var chamberRX: CGFloat = 19.2
    var chamberRY: CGFloat = 22.5
    var rimRX: CGFloat = 13.35
    var rimRY: CGFloat = 15.50
    var rimStroke: CGFloat = 2.575
    var chamberTilt: CGFloat = 28.5   // degrees; the reference draws it off-axis

    // The fold between the top face and the front face.  It is a navy gap
    // rather than a stroked line: a stroke has to stop somewhere, and its cap
    // was left hanging in mid air off the end of the shoulder.  A gap instead
    // closes itself where the two faces meet.  The straight run is tangent to
    // the chamber, and the top face comes to its point at the tangency.
    var foldGap: CGFloat = 2.525
    var foldSlope: CGFloat = -0.389
    var foldTipAngle: CGFloat = 83.5    // in the chamber's own tilted frame

    // The tube.
    var bodyRight: CGFloat = 83.6
    var bottomEndY: CGFloat = 10.43
    var bottomSlope: CGFloat = -0.255
    var capFarX: CGFloat = 99.3         // the far top corner of the end cap

    // The outer edge of the swoosh: a curve off the chamber that straightens
    // into the top edge and runs out to the cap.
    var topPeakX: CGFloat = 44.2
    var topPeakY: CGFloat = 58.0
    var topSlope: CGFloat = -0.371
    var swooshC1X: CGFloat = 26.5, swooshC1Y: CGFloat = 52.0
    var swooshC2X: CGFloat = 33.0, swooshC2Y: CGFloat = 58.6
    var swooshJoinX: CGFloat = 44.5

    // The mouthpiece opening: the end cap stood in from all four of its own
    // edges by one width, so the frame around it is even the whole way round.
    var holeBorder: CGFloat = 2.525

    // The whistle's mouth: a wedge hanging free below the body.  Its inner
    // edge is an arc of the chamber stood off by a fixed gap, so the channel
    // between the two stays even instead of opening out as it descends.  Its
    // top edge is the tube's own underside dropped by the seam's width, so
    // that channel is even as well, and its outer edge runs out on the
    // swoosh's heading -- the drawing's one strong diagonal, here on the far
    // side of the tube.  All that is dialled in is how far the point hangs.
    var chinGap: CGFloat = 2.2                  // about 9px at 512, and constant
    var chinTipAngle: CGFloat = -66             // in the chamber's own frame
    var mouthGap: CGFloat = 2.525

    // The panel.
    var displayX0: CGFloat = 40.0, displayX1: CGFloat = 48.3
    var displayV0: CGFloat = 0.17, displayV1: CGFloat = 0.80
    var knobX: CGFloat = 56.7
    var knobV0: CGFloat = 0.31, knobV1: CGFloat = 0.71
    var knobBaseRX: CGFloat = 3.6, knobBaseRY: CGFloat = 2.1
    var knobCapRX: CGFloat = 3.15, knobCapRY: CGFloat = 1.6
    var knobCapRise: CGFloat = 1.938
    var faderX: CGFloat = 66.2
    var faderV0: CGFloat = 0.1825, faderV1: CGFloat = 0.78, faderHandleV: CGFloat = 0.545

    // Everything below is derived, so the drawing cannot contradict itself.
    var chamber: CGPoint { CGPoint(x: chamberX, y: chamberY) }
    var bottomEnd: CGPoint { CGPoint(x: bodyRight, y: bottomEndY) }
    func topAt(_ x: CGFloat) -> CGFloat { topPeakY + topSlope * (x - topPeakX) }
    var capFar: CGPoint { CGPoint(x: capFarX, y: topAt(capFarX)) }

    // The shoulder is the chamber grown by the fold's width, so the gap between
    // them is the fold, and it closes itself at the tangency by construction.
    var shoulderRX: CGFloat { chamberRX + foldGap }
    var shoulderRY: CGFloat { chamberRY + foldGap }

    // A point on the shoulder, its angle measured in the chamber's own frame.
    func shoulder(_ degrees: CGFloat) -> CGPoint {
        let a = degrees * .pi / 180, t = chamberTilt * .pi / 180
        let x = shoulderRX * cos(a), y = shoulderRY * sin(a)
        return CGPoint(x: chamberX + x * cos(t) - y * sin(t),
                       y: chamberY + x * sin(t) + y * cos(t))
    }

    // Where a line of the fold's slope touches the shoulder.  Solved rather
    // than dialled in, so the straight run can never cut across the chamber.
    var foldTangentAngle: CGFloat {
        let t = chamberTilt * .pi / 180
        let dx = cos(t) + foldSlope * sin(t)          // the slope, in that frame
        let dy = -sin(t) + foldSlope * cos(t)
        var nx = -dy / shoulderRY, ny = dx / shoulderRX
        if ny < 0 { nx = -nx; ny = -ny }              // the upper tangent
        return atan2(ny, nx) * 180 / .pi
    }
    var foldTangent: CGPoint { shoulder(foldTangentAngle) }

    // A point on the chin's inner edge: the chamber, stood off by chinGap.
    func chin(_ degrees: CGFloat) -> CGPoint {
        let a = degrees * .pi / 180, t = chamberTilt * .pi / 180
        let x = (chamberRX + chinGap) * cos(a), y = (chamberRY + chinGap) * sin(a)
        return CGPoint(x: chamberX + x * cos(t) - y * sin(t),
                       y: chamberY + x * sin(t) + y * cos(t))
    }
    func foldAt(_ x: CGFloat) -> CGFloat {
        foldTangent.y + foldSlope * (x - foldTangent.x)
    }
    var foldEnd: CGPoint { CGPoint(x: bodyRight, y: foldAt(bodyRight)) }
    var depth: CGVector { CGVector(dx: capFar.x - foldEnd.x, dy: capFar.y - foldEnd.y) }

    func bottomAt(_ x: CGFloat) -> CGFloat { bottomEndY + bottomSlope * (x - bodyRight) }

    // A point on the top face: x along the tube, v across the depth.
    func face(_ x: CGFloat, _ v: CGFloat) -> CGPoint {
        CGPoint(x: x + depth.dx * v, y: foldAt(x) + depth.dy * v)
    }

    // The top face's lower edge: round the shoulder, then straight to the end.
    var foldTip: CGPoint { shoulder(foldTipAngle) }
    var fold: [CGPoint] {
        var points: [CGPoint] = []
        let tangent = foldTangentAngle
        for step in 0...32 {
            points.append(shoulder(foldTipAngle
                - (foldTipAngle - tangent) * CGFloat(step) / 32))
        }
        points.append(foldEnd)
        return points
    }

    // The heading the swoosh leaves its point on.  Its first control point is
    // very nearly on the line to its second, so this is the direction of the
    // straight-looking run out of the point, not just of an instant of it.
    var swooshHeading: CGVector {
        CGVector(dx: swooshC1X - foldTip.x, dy: swooshC1Y - foldTip.y)
    }

    // The tube's underside dropped by the mouth's gap: the line the wedge's
    // top edge lies along.  Dropped down the normal rather than vertically, so
    // the channel is as wide as it looks.
    func mouthAt(_ x: CGFloat) -> CGFloat {
        bottomAt(x) - mouthGap * sqrt(1 + bottomSlope * bottomSlope)
    }

    var chinTip: CGPoint { chin(chinTipAngle) }

    // The wedge's wide end: out from the point on the swoosh's heading, as far
    // as the mouth line.  Both edges therefore end where they should rather
    // than at a corner that has to be kept in step with them by hand.
    var toothA: CGPoint {
        let slope = swooshHeading.dy / swooshHeading.dx
        let x = (mouthAt(0) - chinTip.y + slope * chinTip.x) / (slope - bottomSlope)
        return CGPoint(x: x, y: chinTip.y + slope * (x - chinTip.x))
    }

    // The wedge's other top corner: where the mouth line crosses the chin's
    // arc.  Solved, so the top edge meets the arc instead of overshooting it.
    var chinTopAngle: CGFloat {
        let t = chamberTilt * .pi / 180
        let rx = chamberRX + chinGap, ry = chamberRY + chinGap
        let o = CGPoint(x: bodyRight - chamberX, y: mouthAt(bodyRight) - chamberY)
        let px = o.x * cos(t) + o.y * sin(t), py = -o.x * sin(t) + o.y * cos(t)
        let dx = cos(t) + bottomSlope * sin(t)        // the mouth line's heading
        let dy = -sin(t) + bottomSlope * cos(t)
        let a = dx * dx / (rx * rx) + dy * dy / (ry * ry)
        let b = 2 * (px * dx / (rx * rx) + py * dy / (ry * ry))
        let c = px * px / (rx * rx) + py * py / (ry * ry) - 1
        let s = (-b + sqrt(max(0, b * b - 4 * a * c))) / (2 * a)   // the near one
        return atan2((py + s * dy) / ry, (px + s * dx) / rx) * 180 / .pi
    }
}

func ellipse(_ c: CGPoint, _ rx: CGFloat, _ ry: CGFloat,
             _ tilt: CGFloat = 0) -> CGPath {
    var t = CGAffineTransform(translationX: c.x, y: c.y)
        .rotated(by: tilt * .pi / 180)
    return CGPath(ellipseIn: CGRect(x: -rx, y: -ry, width: rx * 2, height: ry * 2),
                  transform: &t)
}

struct Shapes {
    let frontFace, topFace, endCap, capHole, tooth: CGPath
    let bounds: CGRect

    init(_ g: Geometry) {
        let fold = g.fold
        let tip = fold[0]

        // The top face is a lens: a point against the chamber's upper left,
        // swelling over the shoulder and running out to the end cap.
        let top = CGMutablePath()
        top.move(to: tip)
        top.addCurve(to: CGPoint(x: g.swooshJoinX, y: g.topAt(g.swooshJoinX)),
                     control1: CGPoint(x: g.swooshC1X, y: g.swooshC1Y),
                     control2: CGPoint(x: g.swooshC2X, y: g.swooshC2Y))
        top.addLine(to: g.capFar)
        top.addLine(to: g.foldEnd)
        for point in fold.reversed().dropFirst() { top.addLine(to: point) }
        top.closeSubpath()
        topFace = top

        // The front face: the fold above, the tube's underside below.  The
        // chamber is filled separately and simply overlaps it.
        // It starts at the tangency: further left the fold line rises off the
        // shoulder, and the quad would poke through the gap.  The chamber
        // covers everything to the left of that anyway.
        let left = g.foldTangent.x
        // Dropping by foldGap would measure the channel vertically, while the
        // chamber's edge is offset along the normal.  The mismatch is small but
        // it shows as a jog where the straight run meets the shoulder.
        let drop = g.foldGap * sqrt(1 + g.foldSlope * g.foldSlope)
        let face = CGMutablePath()
        face.addLines(between: [CGPoint(x: left, y: g.foldAt(left) - drop),
                                CGPoint(x: g.bodyRight, y: g.foldEnd.y - drop),
                                g.bottomEnd,
                                CGPoint(x: left, y: g.bottomAt(left))])
        face.closeSubpath()
        frontFace = face

        // The cap is the end of the box, so it is a parallelogram: two upright
        // edges a depth apart, closed by two edges along the depth.  The two it
        // shares with the other faces are stood in by the same navy gap as
        // every other seam -- along their own normals, which for the upright
        // one is simply sideways -- and the two on the silhouette stay put.
        // Standing the corners in one at a time instead left it a lopsided
        // quad, and a lopsided quad has no square frame for an opening to sit
        // in.  It used to be a stroked line, and a stroke has a join: the one
        // where these three faces meet was showing as a notch in the corner.
        let d = g.depth
        let span = sqrt(d.dx * d.dx + d.dy * d.dy)
        // Standing a line of the depth's slope off by a gap moves it this far
        // vertically, which is what lets the two horizontal edges be written
        // as heights above a shared line.
        let rise = span / d.dx
        let capTopY = g.capFar.y - rise * g.foldGap
        func capTop(_ x: CGFloat) -> CGFloat { capTopY + (x - g.capFar.x) * d.dy / d.dx }
        func capBottom(_ x: CGFloat) -> CGFloat {
            g.bottomEnd.y + (x - g.bottomEnd.x) * d.dy / d.dx
        }
        let nearX = g.bodyRight + g.foldGap, farX = g.capFar.x
        let cap = CGMutablePath()
        cap.addLines(between: [CGPoint(x: nearX, y: capTop(nearX)),
                               CGPoint(x: nearX, y: capBottom(nearX)),
                               CGPoint(x: farX, y: capBottom(farX)),
                               CGPoint(x: farX, y: capTop(farX))])
        cap.closeSubpath()
        endCap = cap

        // The opening is that parallelogram stood in from all four edges by the
        // one width, so the frame around it is even the whole way round.
        let holeX0 = nearX + g.holeBorder, holeX1 = farX - g.holeBorder
        let holeDrop = rise * g.holeBorder
        let hole = CGMutablePath()
        hole.addLines(between: [CGPoint(x: holeX0, y: capTop(holeX0) - holeDrop),
                                CGPoint(x: holeX0, y: capBottom(holeX0) + holeDrop),
                                CGPoint(x: holeX1, y: capBottom(holeX1) + holeDrop),
                                CGPoint(x: holeX1, y: capTop(holeX1) - holeDrop)])
        hole.closeSubpath()
        capHole = hole

        let wedge = CGMutablePath()
        var inner: [CGPoint] = []
        for step in 0...24 {
            inner.append(g.chin(g.chinTopAngle
                + (g.chinTipAngle - g.chinTopAngle) * CGFloat(step) / 24))
        }
        wedge.addLines(between: inner)
        wedge.addLine(to: g.toothA)
        wedge.closeSubpath()
        tooth = wedge

        bounds = frontFace.boundingBoxOfPath
            .union(topFace.boundingBoxOfPath)
            .union(endCap.boundingBoxOfPath)
            .union(ellipse(g.chamber, g.chamberRX, g.chamberRY, g.chamberTilt).boundingBoxOfPath)
    }
}

func draw(size: CGFloat, _ g: Geometry = Geometry()) -> CGImage {
    let shapes = Shapes(g)
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
    context.setFillColor(navy)
    context.fill(rect)

    // Fit the drawing into the plate, and let every length scale with it.
    let field = rect.insetBy(dx: rect.width * 0.055, dy: rect.width * 0.055)
    let scale = min(field.width / shapes.bounds.width,
                    field.height / shapes.bounds.height)
    context.translateBy(x: field.midX - shapes.bounds.midX * scale,
                        y: field.midY - shapes.bounds.midY * scale)
    context.scaleBy(x: scale, y: scale)

    // Hairlines are in grid units, so the small sizes have to ask for their
    // minimums in device pixels or the detail thins away to nothing.
    func px(_ n: CGFloat) -> CGFloat { n / scale }

    let panel = size >= 128            // the display, the knobs, the fader
    let fine = size >= 256             // traces, pointers, tick marks
    let rim = size >= 64               // the ring inside the resonator

    context.setLineCap(.round)
    context.setLineJoin(.round)

    context.setFillColor(green)
    for path in [ellipse(g.chamber, g.chamberRX, g.chamberRY, g.chamberTilt),
                 shapes.frontFace, shapes.topFace, shapes.endCap, shapes.tooth] {
        context.addPath(path)
        context.fillPath()
    }

    if rim {
        context.setStrokeColor(navy)
        context.setLineWidth(max(g.rimStroke, px(1.6)))
        context.addPath(ellipse(g.chamber, g.rimRX, g.rimRY, g.chamberTilt))
        context.strokePath()
    } else {
        // Too small for a rim: the hole is the only thing that still reads,
        // and it is what makes the shape a whistle rather than a blob.
        context.setFillColor(navy)
        context.addPath(ellipse(g.chamber, g.chamberRX * 0.62, g.chamberRY * 0.62, g.chamberTilt))
        context.fillPath()
    }

    context.setFillColor(navy)
    context.addPath(shapes.capHole)
    context.fillPath()

    if panel {
        // The display, sunk into the top face.
        let display = CGMutablePath()
        display.addLines(between: [g.face(g.displayX0, g.displayV0),
                                   g.face(g.displayX0, g.displayV1),
                                   g.face(g.displayX1, g.displayV1),
                                   g.face(g.displayX1, g.displayV0)])
        display.closeSubpath()
        context.setFillColor(navy)
        context.addPath(display)
        context.fillPath()

        if fine {
            // A trace on the screen, jagged the way a waveform is.
            // Time runs across the panel's depth and the amplitude swings
            // along the tube, which is the way the reference reads it.
            let w = g.displayX1 - g.displayX0, h = g.displayV1 - g.displayV0
            let steps: [(CGFloat, CGFloat)] = [   // (time, amplitude)
                (0.10, 0.50), (0.21, 0.50), (0.21, 0.28), (0.31, 0.28),
                (0.31, 0.62), (0.43, 0.62), (0.43, 0.36), (0.55, 0.36),
                (0.55, 0.74), (0.67, 0.74), (0.67, 0.44), (0.79, 0.44),
                (0.79, 0.58), (0.90, 0.58)]
            context.setStrokeColor(orange)
            context.setLineWidth(0.55)
            context.setLineJoin(.miter)
            context.addLines(between: steps.map {
                g.face(g.displayX0 + w * $0.1, g.displayV0 + h * $0.0) })
            context.strokePath()
            context.setLineJoin(.round)
        }

        // Two knobs on the top face: a navy body with an orange cap on it.
        for v in [g.knobV0, g.knobV1] {
            let c = g.face(g.knobX, v)
            let cap = CGPoint(x: c.x, y: c.y + g.knobCapRise)
            context.setFillColor(navy)
            context.addPath(ellipse(c, g.knobBaseRX, g.knobBaseRY))
            context.fillPath()
            // The knob's side wall.  Without it the panel shows between base
            // and cap, and the cap reads as floating rather than sitting on it.
            let body = CGMutablePath()
            body.addLines(between: [CGPoint(x: c.x - g.knobBaseRX, y: c.y),
                                    CGPoint(x: c.x + g.knobBaseRX, y: c.y),
                                    CGPoint(x: cap.x + g.knobCapRX, y: cap.y),
                                    CGPoint(x: cap.x - g.knobCapRX, y: cap.y)])
            body.closeSubpath()
            context.addPath(body)
            context.fillPath()
            context.setFillColor(orange)
            context.addPath(ellipse(cap, g.knobCapRX, g.knobCapRY))
            context.fillPath()
            if fine {
                context.setStrokeColor(navy)
                context.setLineWidth(0.75)
                context.move(to: cap)
                context.addLine(to: CGPoint(x: cap.x - g.knobCapRX * 0.76,
                                            y: cap.y + g.knobCapRY * 0.43))
                context.strokePath()
            }
        }

        // The fader, running across the depth of the top face.
        context.setStrokeColor(navy)
        context.setLineWidth(1.0)
        context.move(to: g.face(g.faderX, g.faderV0))
        context.addLine(to: g.face(g.faderX, g.faderV1))
        context.strokePath()
        context.setStrokeColor(orange)
        context.setLineWidth(2.1)
        let handle = g.face(g.faderX, g.faderHandleV)
        let reach: CGFloat = 2.3          // parallel to the tube's own edges
        context.move(to: CGPoint(x: handle.x - reach,
                                 y: handle.y - reach * g.foldSlope))
        context.addLine(to: CGPoint(x: handle.x + reach,
                                    y: handle.y + reach * g.foldSlope))
        context.strokePath()

    }

    context.restoreGState()
    return context.makeImage()!
}

// MARK: - end shared drawing

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let iconset = root.appendingPathComponent("mac/build/AppIcon.iconset")
let appiconset = root.appendingPathComponent(
    "mac/WhistleSynth/Assets.xcassets/AppIcon.appiconset")
let icns = root.appendingPathComponent("mac/WhistleSynth/AppIcon.icns")
let storePNG = root.appendingPathComponent("mac/build/AppIcon-1024.png")

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

// App Store Connect wants a flat 1024 with no rounding of its own; ours is
// already rounded with transparent corners, which is what it accepts.
try write(rendered[1024]!, to: storePNG)

// build.sh has no actool, so it copies this .icns straight into the bundle.
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", icns.path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else { exit(iconutil.terminationStatus) }

print("wrote \(appiconset.path), \(icns.path), and \(storePNG.path)")
