#!/bin/bash
# Builds Whistle Synth.app without Xcode, using only the Command Line Tools.
#
# This exists so the app can be built and run on a machine that has no Xcode
# on it, and so the build is one readable file rather than a project format.
# For the App Store you still need Xcode -- see mac/README.md -- but what it
# produces is the same set of sources compiled the same way.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
OUT="${1:-$ROOT/mac/build}"
APP="$OUT/WhistleSynth.app"
DEPLOY_TARGET="13.0"

# arm64 and x86_64 both, so the same bundle runs on any Mac the App Store
# would ship it to.  Drop one arch here if you only care about your own.
ARCHS=(arm64 x86_64)

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$OUT/obj"

CORE_C=(pitch.c synth.c engine.c mac/core/whistle.c mac/core/whistle_audio.c)
SWIFT=(mac/WhistleSynth/*.swift)

THIN=()
for ARCH in "${ARCHS[@]}"; do
  TARGET="$ARCH-apple-macos$DEPLOY_TARGET"
  OBJS=()

  for SRC in "${CORE_C[@]}"; do
    OBJ="$OUT/obj/$ARCH-$(basename "${SRC%.c}").o"
    clang -c "$SRC" -o "$OBJ" \
      -target "$TARGET" -I "$ROOT" -I "$ROOT/mac/core" \
      -std=c11 -Wall -Wextra -O2
    OBJS+=("$OBJ")
  done

  swiftc "${SWIFT[@]}" "${OBJS[@]}" \
    -o "$OUT/obj/WhistleSynth-$ARCH" \
    -target "$TARGET" \
    -swift-version 5 \
    -import-objc-header "$ROOT/mac/WhistleSynth/Bridging-Header.h" \
    -I "$ROOT" -I "$ROOT/mac/core" \
    -framework AppKit -framework AVFoundation \
    -framework AudioToolbox -framework CoreAudio \
    -O
  THIN+=("$OUT/obj/WhistleSynth-$ARCH")
done

if [ "${#THIN[@]}" -gt 1 ]; then
  lipo -create "${THIN[@]}" -output "$APP/Contents/MacOS/WhistleSynth"
else
  cp "${THIN[0]}" "$APP/Contents/MacOS/WhistleSynth"
fi

cp mac/WhistleSynth/Info.plist "$APP/Contents/Info.plist"
# Xcode compiles the asset catalog into exactly this file; without actool
# (which ships with Xcode, not the Command Line Tools) we use the .icns that
# mac/tools/make-icon.swift wrote from the same drawing.
cp mac/WhistleSynth/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
# Xcode copies this in as a resource; the store reads it out of the bundle
# root of Contents/Resources either way.
cp mac/WhistleSynth/PrivacyInfo.xcprivacy "$APP/Contents/Resources/PrivacyInfo.xcprivacy"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# The sandbox only applies to a signed binary, so even a local test build has
# to be signed for the entitlements to mean anything.  Ad-hoc is fine here;
# App Store submission re-signs with a real identity.
codesign --force --sign - \
  --entitlements mac/WhistleSynth/WhistleSynth.entitlements \
  --options runtime \
  "$APP" 2>&1 | sed 's/^/  /'

echo "built: $APP"
