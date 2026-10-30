#!/bin/sh
# Packages the installed SDL3 dylib into a flat iOS .framework bundle.
# Run this after: cmake --install <sdl3-build-dir> --config RelWithDebInfo
#
# Usage: ./package-sdl3-ios-framework.sh [SDK_DIR] [SDL3_SRC]
#   SDK_DIR defaults to ~/SDK/ios/arm64
#   SDL3_SRC is the SDL source tree, used for the version (default: ~/src/libs/SDL3)

set -e

SDK="${1:-${HOME}/SDK/ios/arm64}"
SDL3_SRC="${2:-${HOME}/src/libs/SDL3}"

DYLIB_LINK="${SDK}/lib/libSDL3.dylib"
if [ ! -L "${DYLIB_LINK}" ] && [ ! -f "${DYLIB_LINK}" ]; then
    echo "error: ${DYLIB_LINK} not found. Run cmake --install first." >&2
    exit 1
fi

# Resolve the real versioned dylib (e.g. libSDL3.0.dylib)
REAL_DYLIB="$(readlink -f "${DYLIB_LINK}")"

# Extract version from CMakeLists.txt
SDL3_VERSION=$(grep 'project(SDL3.*VERSION' "${SDL3_SRC}/CMakeLists.txt" 2>/dev/null \
    | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [ -z "${SDL3_VERSION}" ]; then
    SDL3_VERSION="3.0.0"
    echo "warning: could not determine SDL3 version, using ${SDL3_VERSION}" >&2
fi
MAJOR=$(echo "${SDL3_VERSION}" | cut -d. -f1)
MINOR=$(echo "${SDL3_VERSION}" | cut -d. -f2)
MICRO=$(echo "${SDL3_VERSION}" | cut -d. -f3)
BUNDLE_VERSION=$(( MAJOR * 1000 + MINOR * 100 + MICRO ))

FW="${SDK}/SDL3.framework"
mkdir -p "${FW}"

echo "Copying ${REAL_DYLIB} -> ${FW}/SDL3"
cp "${REAL_DYLIB}" "${FW}/SDL3"

echo "Fixing install name: @rpath/SDL3.framework/SDL3"
install_name_tool -id "@rpath/SDL3.framework/SDL3" "${FW}/SDL3"

echo "Removing ad-hoc signature"
codesign --remove-signature "${FW}/SDL3"

cat > "${FW}/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>SDL3</string>
    <key>CFBundleIdentifier</key>
    <string>org.libsdl.SDL3</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>SDL3</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>${SDL3_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${BUNDLE_VERSION}</string>
    <key>MinimumOSVersion</key>
    <string>15.0</string>
</dict>
</plist>
PLIST

# Copy dSYM alongside the framework, renaming the DWARF file to match the
# framework executable so Xcode can collect it into xcarchive dSYMs.
SDL3_DYLIB_NAME="$(basename "${REAL_DYLIB}")"
DSYM_SRC="${SDK}/lib/${SDL3_DYLIB_NAME}.dSYM"
DSYM_DST="${SDK}/SDL3.framework.dSYM"
if [ -d "${DSYM_SRC}" ]; then
    echo "Copying dSYM: ${DSYM_SRC} -> ${DSYM_DST}"
    rm -rf "${DSYM_DST}"
    cp -r "${DSYM_SRC}" "${DSYM_DST}"
    DWARF_OLD="${DSYM_DST}/Contents/Resources/DWARF/${SDL3_DYLIB_NAME}"
    DWARF_NEW="${DSYM_DST}/Contents/Resources/DWARF/SDL3"
    [ -f "${DWARF_OLD}" ] && mv "${DWARF_OLD}" "${DWARF_NEW}"
else
    echo "warning: dSYM not found at ${DSYM_SRC}, skipping" >&2
fi

echo "Done: ${FW}"
