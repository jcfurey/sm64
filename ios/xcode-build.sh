#!/bin/bash
# Build bridge used by SM64.xcodeproj. Xcode creates and signs the app bundle;
# the existing GNU Make build remains responsible for the game executable.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${SM64_VERSION:-us}"
PLATFORM="${PLATFORM_NAME:-iphonesimulator}"
ARCH="${CURRENT_ARCH:-}"
MIN_VERSION="${IPHONEOS_DEPLOYMENT_TARGET:-14.0}"

if [[ -z "$ARCH" || "$ARCH" == "undefined_arch" ]]; then
    ARCH="${ARCHS%% *}"
fi
ARCH="${ARCH:-arm64}"

if [[ "${CONFIGURATION:-Debug}" == "Release" ]]; then
    CONFIG_KEY=release
else
    CONFIG_KEY=debug
fi

if [[ "${SM64_ENABLE_OPENGL:-NO}" == "YES" ]]; then
    RENDERER=gles
else
    RENDERER=metal
fi

case "$PLATFORM" in
    iphoneos)
        BUILD_SUFFIX="ios"
        SDL_PREFIX="$REPO_ROOT/ios/SDL2"
        ;;
    iphonesimulator)
        BUILD_SUFFIX="iossim"
        SDL_PREFIX="$REPO_ROOT/ios/SDL2-simulator"
        ;;
    *)
        echo "error: SM64 supports the iphoneos and iphonesimulator platforms, not '$PLATFORM'" >&2
        exit 1
        ;;
esac

if [[ ! "$VERSION" =~ ^(us|jp)$ ]]; then
    echo "error: SM64_VERSION must be 'us' or 'jp', not '$VERSION'" >&2
    exit 1
fi

if [[ ! -f "$REPO_ROOT/baserom.$VERSION.z64" ]]; then
    echo "error: Missing baserom.$VERSION.z64 in $REPO_ROOT" >&2
    echo "error: Add your original $VERSION ROM, then build again." >&2
    exit 1
fi

if [[ ! -f "$SDL_PREFIX/lib/libSDL2.a" || ! -f "$SDL_PREFIX/include/SDL2/SDL.h" ]]; then
    echo "error: SDL2 for $PLATFORM has not been built." >&2
    echo "error: Run: IOS_SDK=$PLATFORM ./ios/build-sdl2.sh" >&2
    exit 1
fi

if [[ -n "${GMAKE:-}" && -x "${GMAKE}" ]]; then
    GMAKE_BIN="$GMAKE"
elif [[ -x /opt/homebrew/bin/gmake ]]; then
    GMAKE_BIN=/opt/homebrew/bin/gmake
elif [[ -x /usr/local/bin/gmake ]]; then
    GMAKE_BIN=/usr/local/bin/gmake
elif GMAKE_BIN="$(command -v gmake 2>/dev/null)" && [[ -n "$GMAKE_BIN" ]]; then
    :
else
    echo "error: GNU Make (gmake) was not found. Install it with: brew install make" >&2
    exit 1
fi

if [[ -n "${SM64_BUILD_JOBS:-}" ]]; then
    JOBS="$SM64_BUILD_JOBS"
elif JOBS="$(/usr/bin/getconf _NPROCESSORS_ONLN 2>/dev/null)" && [[ -n "$JOBS" ]]; then
    :
else
    JOBS=4
fi

XCODE_BUILD_BASE="build/xcode/${CONFIG_KEY}-${RENDERER}"
MAKE_TARGET="$XCODE_BUILD_BASE/${VERSION}_${BUILD_SUFFIX}/sm64.${VERSION}"
SOURCE_EXECUTABLE="$REPO_ROOT/$MAKE_TARGET"
DESTINATION_EXECUTABLE="${TARGET_BUILD_DIR:?}/${EXECUTABLE_PATH:?}"

MAKE_ARGS=(
    TARGET_IOS=1
    TARGET_N64=0
    "VERSION=$VERSION"
    "IOS_SDK=$PLATFORM"
    "IOS_ARCH=$ARCH"
    "IOS_MIN_VERSION=$MIN_VERSION"
    "IOS_SDL2_PATH=$SDL_PREFIX"
    "BUILD_DIR_BASE=$XCODE_BUILD_BASE"
    "PORT_SYNTAX_WARNINGS="
    "PORT_DIAGNOSTIC_FLAGS=-fdiagnostics-absolute-paths -Wall -Wextra -Wpedantic -Wno-deprecated-declarations -Wno-constant-conversion -Wno-unused-parameter -Wno-unused-function -Wno-strict-prototypes"
)

if [[ "$CONFIG_KEY" == "debug" ]]; then
    # The executable is built by GNU Make and copied into Xcode's bundle, so
    # Xcode's compiler settings do not affect it. Retain assertions and frame
    # pointers, avoid LTO, and include full source-level debug information.
    MAKE_ARGS+=(
        "OPT_FLAGS=-Og -g3 -fno-omit-frame-pointer -fno-math-errno"
    )
fi

if [[ "${SM64_ENABLE_OPENGL:-NO}" == "YES" ]]; then
    MAKE_ARGS+=(ENABLE_OPENGL=1 ENABLE_METAL=0)
fi

echo "Building SM64 $VERSION for $PLATFORM ($ARCH)..."
"$GMAKE_BIN" -C "$REPO_ROOT" "${MAKE_ARGS[@]}" -j"$JOBS" "$MAKE_TARGET"

mkdir -p "$(dirname "$DESTINATION_EXECUTABLE")"
cp "$SOURCE_EXECUTABLE" "$DESTINATION_EXECUTABLE"
chmod 0755 "$DESTINATION_EXECUTABLE"

if [[ "$CONFIG_KEY" == "debug" && -n "${DWARF_DSYM_FOLDER_PATH:-}" && -n "${DWARF_DSYM_FILE_NAME:-}" ]]; then
    DSYM_PATH="$DWARF_DSYM_FOLDER_PATH/$DWARF_DSYM_FILE_NAME"
    rm -rf "$DSYM_PATH"
    /usr/bin/dsymutil "$DESTINATION_EXECUTABLE" -o "$DSYM_PATH"
fi

ICON_NAMES=(
    Icon-60@2x.png
    Icon-60@3x.png
    Icon-76.png
    Icon-76@2x.png
    Icon-83.5@2x.png
    Icon-Small-40.png
    Icon-Small-40@2x.png
    Icon-Small-40@3x.png
    Icon-Small.png
    Icon-Small@2x.png
    Icon-Small@3x.png
)

for icon in "${ICON_NAMES[@]}"; do
    cp "$REPO_ROOT/ios/icons/$icon" "${TARGET_BUILD_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/$icon"
done

# Compile the 1024 px source into a modern asset catalog. The explicit legacy
# files above remain for iOS 14 compatibility; Assets.car supplies the named
# AppIcon and App Store marketing artwork expected by current tooling.
ASSET_WORK="$DERIVED_FILE_DIR/SM64Assets.xcassets"
APPICON_SET="$ASSET_WORK/AppIcon.appiconset"
ASSET_OUTPUT="${TARGET_BUILD_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}"
rm -rf "$ASSET_WORK"
mkdir -p "$APPICON_SET"
cp "$REPO_ROOT/ios/icons/AppIcon-1024.png" "$APPICON_SET/AppIcon-1024.png"
cp "$REPO_ROOT/ios/AppIconContents.json" "$APPICON_SET/Contents.json"
/usr/bin/xcrun actool "$ASSET_WORK" \
    --compile "$ASSET_OUTPUT" \
    --app-icon AppIcon \
    --platform "$PLATFORM" \
    --minimum-deployment-target "$MIN_VERSION" \
    --target-device iphone \
    --target-device ipad \
    --bundle-identifier "${PRODUCT_BUNDLE_IDENTIFIER:?}" \
    --output-partial-info-plist "$DERIVED_FILE_DIR/SM64AppIcon.plist"
