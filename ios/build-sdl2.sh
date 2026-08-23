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

case "$SDL2_VERSION" in
    2.30.7) SDL2_SHA256="2508c80438cd5ff3bbeb8fe36b8f3ce7805018ff30303010b61b03bb83ab9694" ;;
    *) echo "No authenticated SDL2 archive checksum is recorded for $SDL2_VERSION" >&2; exit 1 ;;
esac

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
    curl --proto '=https' --tlsv1.2 --retry 3 -fLo "$TARBALL" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz"
fi

ACTUAL_SHA256="$(shasum -a 256 "$TARBALL" | awk '{print $1}')"
if [ "$ACTUAL_SHA256" != "$SDL2_SHA256" ]; then
    echo "SDL2 archive checksum mismatch for $TARBALL" >&2
    echo "Expected: $SDL2_SHA256" >&2
    echo "Actual:   $ACTUAL_SHA256" >&2
    exit 1
fi

if [ ! -d "SDL2-$SDL2_VERSION" ]; then
    tar xzf "$TARBALL"
fi

SCENE_PATCH="$SCRIPT_DIR/patches/SDL2-$SDL2_VERSION-uiscene.patch"
if [ ! -f "$SCENE_PATCH" ]; then
    echo "No UIScene lifecycle patch is available for SDL2 $SDL2_VERSION" >&2
    exit 1
fi

# The extracted tree is ignored and reused between builds. If the maintained
# patch changes, start from the tarball again rather than leaving an older
# already-patched tree in place (where neither forward nor reverse patching is
# valid anymore).
PATCH_STAMP="$WORK_DIR/.SDL2-$SDL2_VERSION-uiscene.sha256"
PATCH_HASH="$(shasum -a 256 "$SCENE_PATCH" | awk '{print $1}')"
if [ -d "SDL2-$SDL2_VERSION" ] &&
   { [ ! -f "$PATCH_STAMP" ] || [ "$(<"$PATCH_STAMP")" != "$PATCH_HASH" ]; }; then
    echo "UIScene patch changed; refreshing the SDL2 source tree..."
    rm -rf "SDL2-$SDL2_VERSION"
    tar xzf "$TARBALL"
fi

if patch --dry-run -s -N -d "SDL2-$SDL2_VERSION" -p1 < "$SCENE_PATCH" >/dev/null 2>&1; then
    echo "Applying SDL2 UIScene lifecycle patch..."
    patch -s -N -d "SDL2-$SDL2_VERSION" -p1 < "$SCENE_PATCH"
elif patch --dry-run -s -R -d "SDL2-$SDL2_VERSION" -p1 < "$SCENE_PATCH" >/dev/null 2>&1; then
    echo "SDL2 UIScene lifecycle patch is already applied."
else
    echo "SDL2 UIScene lifecycle patch does not apply cleanly" >&2
    exit 1
fi
echo "$PATCH_HASH" > "$PATCH_STAMP"

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
