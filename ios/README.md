# Super Mario 64 for iOS

This directory contains the iOS packaging for the port. The build produces a
native arm64 app (`.app` bundle and sideloadable `.ipa`) with touch controls,
for the `us` and `jp` versions of the game.

Rendering uses **Metal** by default — Apple's modern graphics API — through a
backend that compiles a Metal pipeline for each N64 color-combiner mode at
runtime and paces presentation to the game's 30 fps with
`presentDrawable:afterMinimumDuration:`. An OpenGL ES 2.0 fallback renderer
is kept available with `ENABLE_OPENGL=1` (GLES is deprecated on iOS but still
functional; useful for A/B-testing rendering issues).

Because the game's assets are extracted from your ROM into the binary at build
time, **the resulting app contains copyrighted game data — build it for
yourself and do not distribute it.**

## Requirements

- macOS with [Xcode](https://apps.apple.com/us/app/xcode/id497799835) and its
  command line tools (`xcode-select --install`)
- `python3` (ships with macOS) and GNU make from Homebrew (`brew install make`)
- An original Super Mario 64 ROM for each version you want to build
  (`us` and/or `jp`)

## Building

1. Build SDL2 as a static library for iOS (one-time step):

   ```
   ./ios/build-sdl2.sh
   ```

2. Place your ROM(s) in the repository root, named `baserom.us.z64` /
   `baserom.jp.z64`, for asset extraction.

3. Build the app (use Homebrew's `gmake`; the make shipped with macOS is too
   old):

   ```
   gmake TARGET_IOS=1 VERSION=us -j$(sysctl -n hw.ncpu)
   gmake TARGET_IOS=1 VERSION=jp -j$(sysctl -n hw.ncpu)
   ```

   Outputs land in `build/<version>_ios/`:
   - `SM64-<version>.app` — the app bundle
   - `sm64.<version>.ipa` — the same bundle packaged for sideloading

   The two versions use different bundle identifiers
   (`com.sm64port.us` / `com.sm64port.jp`), so both can be installed at the
   same time.

## Installing on a device

Apps that are not on the App Store have to be signed with an identity your
device trusts. Two common routes:

- **Sideloading (no paid developer account):** install
  [AltStore](https://altstore.io/) or [Sideloadly](https://sideloadly.io/) and
  feed it the `.ipa`. It re-signs the app with your Apple ID. With a free
  account the signature lasts 7 days before the app must be refreshed.

- **Direct install (Apple Developer account):** build with your development
  identity and install with Xcode's tooling:

  ```
  gmake TARGET_IOS=1 VERSION=us IOS_SIGN_IDENTITY="Apple Development: you@example.com (TEAMID)"
  xcrun devicectl device install app --device <device-id> build/us_ios/SM64-us.app
  ```

  (On Xcode 14 or older use `ios-deploy` or the Devices and Simulators
  window instead of `devicectl`.)

## Controls

Touch controls are drawn as a semi-transparent overlay:

- **Left half of the screen** — floating analog stick: touch anywhere and
  drag; the touch point becomes the stick's neutral position
- **Blue button (right)** — A (jump)
- **Green button** — B (punch / read)
- **Gray button next to B** — Z (crouch)
- **Yellow cluster with arrows** — C buttons (camera)
- **Gray button (top right)** — R (camera mode)
- **Red button (top center)** — Start

Bluetooth game controllers (Xbox, PlayStation, MFi) are supported through
SDL's GameController API and can be used instead of the touch controls.

Save files and settings are stored in the app's sandbox and survive app
updates (but not uninstalling).

## Build options

| Variable | Default | Meaning |
|----------|---------|---------|
| `VERSION` | `us` | Game version: `us` or `jp` (`eu` also builds but is less tested on the port) |
| `ENABLE_OPENGL` | off | Use the OpenGL ES 2.0 renderer instead of Metal |
| `PEDANTIC` | `0` | Compile the platform layer (`src/pc`) with `-Wall -Wextra -Wpedantic` |
| `IOS_MIN_VERSION` | `14.0` | Minimum iOS version |
| `IOS_ARCH` | `arm64` | Target architecture |
| `IOS_BUNDLE_ID` | `com.sm64port.<version>` | Bundle identifier |
| `IOS_DISPLAY_NAME` | `SM64 <version>` | Home screen name |
| `IOS_SIGN_IDENTITY` | `-` (ad-hoc) | Codesigning identity |
| `IOS_SDL2_PATH` | `ios/SDL2` | Where the static SDL2 lives |

The iOS build compiles with `-O3 -flto` (link-time optimization) by default;
the game is far below what modern iPhone hardware can render, so it runs at a
locked 30 fps (the game's native logic rate), frame-paced by the Metal
presentation queue (or vsync under the GLES fallback).

`PEDANTIC=1` is scoped to the modern platform code on purpose: the decompiled
1996 game code relies on GNU C extensions and idioms that predate these
warnings, so compiling it with `-Wpedantic` would bury real findings in
thousands of historical ones. The platform layer compiles clean under it.
