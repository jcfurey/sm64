#!/bin/bash
# Downloads SDL2 and builds it as a static library for iOS, installing the
# result into ios/SDL2 (the layout the Makefile expects):
#
#   ios/SDL2/include/SDL2/*.h
#   ios/SDL2/lib/libSDL2.a
#
# Requires macOS with Xcode and its command line tools.

set -euo pipefail

SDL2_VERSION="${SDL2_VERSION:-2.30.7}"
IOS_MIN_VERSION="${IOS_MIN_VERSION:-14.0}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/SDL2"
WORK_DIR="$SCRIPT_DIR/sdl2-build"

mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

TARBALL="SDL2-$SDL2_VERSION.tar.gz"
if [ ! -f "$TARBALL" ]; then
    echo "Downloading SDL2 $SDL2_VERSION..."
    curl -fLo "$TARBALL" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz"
fi

if [ ! -d "SDL2-$SDL2_VERSION" ]; then
    tar xzf "$TARBALL"
fi

echo "Building SDL2 static library for iOS..."
xcodebuild build \
    -project "SDL2-$SDL2_VERSION/Xcode/SDL/SDL.xcodeproj" \
    -target "Static Library-iOS" \
    -configuration Release \
    -sdk iphoneos \
    ARCHS=arm64 \
    ONLY_ACTIVE_ARCH=NO \
    ENABLE_BITCODE=NO \
    IPHONEOS_DEPLOYMENT_TARGET="$IOS_MIN_VERSION" \
    CONFIGURATION_BUILD_DIR="$WORK_DIR/out"

echo "Installing into $PREFIX..."
rm -rf "$PREFIX"
mkdir -p "$PREFIX/include/SDL2" "$PREFIX/lib"
cp "SDL2-$SDL2_VERSION/include/"*.h "$PREFIX/include/SDL2/"
cp "$WORK_DIR/out/libSDL2.a" "$PREFIX/lib/"

echo "Done. SDL2 for iOS is installed in $PREFIX"
echo "You can remove $WORK_DIR to reclaim space."
