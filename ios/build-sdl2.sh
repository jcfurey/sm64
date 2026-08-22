#!/bin/bash
# Downloads SDL2 and builds it as a static library for iOS, installing the
# result into the layout the Makefile expects:
#
#   ios/SDL2/include/SDL2/*.h            ios/SDL2/lib/libSDL2.a
#   ios/SDL2-simulator/include/SDL2/*.h  ios/SDL2-simulator/lib/libSDL2.a
#
# Device is the default. Set IOS_SDK=iphonesimulator for the simulator slice,
# which is a separate binary -- the two are not interchangeable. Each lands in
# its own prefix, so building one leaves the other in place.
#
#   ./ios/build-sdl2.sh                        # device
#   IOS_SDK=iphonesimulator ./ios/build-sdl2.sh
#
# Requires macOS with Xcode and its command line tools.

set -euo pipefail

SDL2_VERSION="${SDL2_VERSION:-2.30.7}"
IOS_MIN_VERSION="${IOS_MIN_VERSION:-14.0}"
IOS_SDK="${IOS_SDK:-iphoneos}"

case "$IOS_SDK" in
    iphoneos)        PREFIX_NAME="SDL2" ;;
    iphonesimulator) PREFIX_NAME="SDL2-simulator" ;;
    *) echo "IOS_SDK must be iphoneos or iphonesimulator, not '$IOS_SDK'" >&2; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/$PREFIX_NAME"
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

echo "Building SDL2 static library for $IOS_SDK..."
xcodebuild build \
    -project "SDL2-$SDL2_VERSION/Xcode/SDL/SDL.xcodeproj" \
    -target "Static Library-iOS" \
    -configuration Release \
    -sdk "$IOS_SDK" \
    ARCHS=arm64 \
    ONLY_ACTIVE_ARCH=NO \
    ENABLE_BITCODE=NO \
    IPHONEOS_DEPLOYMENT_TARGET="$IOS_MIN_VERSION" \
    CONFIGURATION_BUILD_DIR="$WORK_DIR/out-$IOS_SDK"

echo "Installing into $PREFIX..."
rm -rf "$PREFIX"
mkdir -p "$PREFIX/include/SDL2" "$PREFIX/lib"
cp "SDL2-$SDL2_VERSION/include/"*.h "$PREFIX/include/SDL2/"
cp "$WORK_DIR/out-$IOS_SDK/libSDL2.a" "$PREFIX/lib/"

echo "Done. SDL2 for $IOS_SDK is installed in $PREFIX"
echo "You can remove $WORK_DIR to reclaim space."
