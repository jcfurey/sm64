# Super Mario 64 for iOS

This directory contains the iOS packaging for the port. The build produces a
native arm64 app (`.app` bundle and sideloadable `.ipa`) with touch controls,
for the `us` and `jp` versions of the game. iPhone and iPad can run in portrait,
upside-down portrait where UIKit permits it, or either landscape direction.

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
- An iPhone or iPad running iOS 15 or newer
- `python3` (ships with macOS) and GNU make from Homebrew (`brew install make`)
- An original Super Mario 64 ROM for each version you want to build
  (`us` and/or `jp`)

## Building and running with Xcode

1. Build both SDL2 slices (one-time setup):

   ```
   ./ios/build-sdl2.sh
   IOS_SDK=iphonesimulator ./ios/build-sdl2.sh
   ```

   The build script automatically applies the repository's SDL2 `UIScene`
   lifecycle patch before compiling each slice. The SDL archive is verified
   against its pinned SHA-256 before extraction. Rebuild both slices after
   pulling changes to that patch.

2. Place your ROM(s) in the repository root, named `baserom.us.z64` /
   `baserom.jp.z64`, for asset extraction.

3. Open the project:

   ```
   open ios/SM64.xcodeproj
   ```

4. Select the **SM64 US** or **SM64 JP** scheme, choose an iOS Simulator or
   connected device, and press **Run**. The scheme reports a clear error if its
   matching ROM or SDL2 slice is missing.

For a physical device, select the corresponding target, open **Signing &
Capabilities**, and choose your development team. Xcode then manages the app's
development signature and provisioning. Simulator builds do not need a team.

The Xcode project is a native app wrapper around the existing build: GNU Make
still compiles and links the game executable, while Xcode creates the final app
bundle, selects the SDK, installs it, and launches it. The user-defined
`SM64_ENABLE_OPENGL` build setting can be changed to `YES` to use the GLES
fallback. The wrapper keeps the decompilation-wide legacy warning sweep out of
Xcode's Issue navigator; command-line builds retain that full warning audit,
while real Xcode compile errors use absolute, clickable source paths.
The generated app adopts UIKit's single-scene lifecycle on iOS 13 and newer;
SDL startup is deferred until the window scene is connected.

The Xcode **Debug** configuration is intentionally different from command-line
release builds: it uses `-Og -g3`, keeps assertions and frame pointers, disables
LTO, and emits a matching dSYM. Source breakpoints, local variables, and crash
symbolication therefore work normally. Use **Release** when measuring frame
pacing or device performance.

## Creating a TestFlight archive

TestFlight requires an active Apple Developer Program membership, a globally
unique bundle identifier registered to your team, and a matching app record in
App Store Connect. The **SM64 US** target is configured for team `7YGTR289AX`
with the permanent identifier `com.jcfurey.sm64.us`. Keep that identifier
unchanged after creating the App Store Connect record. The JP target retains
its local-build default until it needs its own App Store Connect record.

This archive is intended **only for private internal TestFlight use on personal
devices**. It contains ROM-derived Nintendo assets and must not be submitted to
the general App Store, offered to external TestFlight testers, or otherwise
distributed.

For personal devices on your own App Store Connect account, use internal
TestFlight testing:

1. Select **SM64 US**, **Any iOS Device (arm64)**, and the **Release**
   configuration, then choose **Product > Archive**.
2. In Organizer, run **Validate App**. Then choose **Distribute App > TestFlight
   Internal Only** and upload.
3. In App Store Connect, add your Apple ID to an internal testing group and
   install the build from the TestFlight app.

The Release archive retains optimized line tables and includes a matching dSYM
for crash symbolication. It also contains `PrivacyInfo.xcprivacy`, the modern
AppIcon catalog, and `ITSAppUsesNonExemptEncryption = NO`. The privacy manifest
declares no tracking or collected data and records the file-metadata and elapsed
timer APIs used by the app and SDL.

A command-line archive can be created without uploading:

```
xcodebuild -project ios/SM64.xcodeproj -scheme "SM64 US" \
  -configuration Release -destination "generic/platform=iOS" \
  -archivePath "$PWD/build/archives/SM64-us-1.0-2.xcarchive" \
  CURRENT_PROJECT_VERSION=2 -allowProvisioningUpdates archive

tools/porttest/check-ios-archive.py \
  "$PWD/build/archives/SM64-us-1.0-2.xcarchive"
```

The archive check verifies the bundled metadata and privacy declarations, the
Apple code signature, and that the optimized executable has a valid matching
dSYM. Pass `--allow-unsigned` only when checking archive structure before a
team and permanent bundle identifier have been selected.

Every subsequent upload of version 1.0 must use a larger
`CURRENT_PROJECT_VERSION`. `ios/ExportOptions-TestFlight.plist` is provided for
automation and deliberately marks uploads as internal-TestFlight-only. Running
the following command **uploads the archive to App Store Connect**:

```
xcodebuild -exportArchive \
  -archivePath "$PWD/build/archives/SM64-us-1.0-2.xcarchive" \
  -exportOptionsPlist ios/ExportOptions-TestFlight.plist \
  -exportPath "$PWD/build/testflight-upload" -allowProvisioningUpdates
```

Keep `testFlightInternalTestingOnly` enabled for this project. Do not invite
external testers or submit this build for general App Store review.

## Building from the command line

After building the SDL2 slice for the desired SDK and adding the ROM, invoke
Homebrew's `gmake` (the make shipped with macOS is too old):

```
gmake TARGET_IOS=1 VERSION=us -j$(sysctl -n hw.ncpu)
gmake TARGET_IOS=1 VERSION=jp -j$(sysctl -n hw.ncpu)
```

Outputs land in `build/<version>_ios/`:

- `SM64-<version>.app` — the app bundle
- `sm64.<version>.ipa` — the same bundle packaged for sideloading

To verify the generated plist and every bundled icon without launching the
app, run:

```
gmake TARGET_IOS=1 VERSION=us check-ios-bundle
```

The two versions use different bundle identifiers (`com.sm64port.us` /
`com.sm64port.jp`), so both can be installed at the same time.

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

In portrait, the complete 4:3 game picture is fitted directly below the top
safe area, leaving the lower portion of the display as a dedicated control
deck. This avoids the severe center crop a 4:3 scene would otherwise receive
on a tall phone. Landscape remains full-screen.

The overlay and edge-anchored game HUD follow the live UIKit safe area, so UI stays clear of the
Dynamic Island/notch, rounded corners, and home indicator as the device rotates.
It uses separate portrait and landscape arrangements whose positions adapt to
the available rectangle rather than matching a hard-coded list of device
models. Control size is calculated in UIKit points and capped on large tablets,
so the same build remains usable from compact iPhones through 13-inch iPads.
Modern iPhones without a Home button may ignore upside-down portrait even
though the app declares it; that decision belongs to UIKit.
On iPad, the app also participates in multitasking and Stage Manager; resize
events select the appropriate layout profile without requiring device-specific
screen tables.

The layout is not fixed. The **Touch Controls** options screen provides
**Touch Size** and **Touch Alpha** (Hidden removes the overlay while leaving
touch input working), light impact **Touch Haptics**, and **Hide for Pad**,
which automatically removes the overlay while a hardware controller is
connected. **Move Buttons** starts a drag-to-place mode: drag any button where
you want it, then tap an empty spot to finish. The result is saved to
`sm64_touch_layout.txt` next to your save file — delete that file to go back to
the default arrangement. Portrait and landscape edits are saved independently;
an older landscape-only layout file is imported automatically.

The **Controller** options screen remaps N64 A, B, Start, Z, R, and each C
direction to face buttons, shoulders, triggers, D-pad directions, or right-stick
directions. The physical D-pad always continues to navigate menus, and **Reset
Mapping** restores the defaults. Bindings are persisted in `sm64config.txt`.

## In-game options menu

Pause the game and press **R** to open the options menu (stick or D-pad to
navigate, A or left/right to change a value, and R or Start to close). In the
Debug Features screen, B returns to the main options screen. Settings persist
in the app's config file:

- **Frame Rate** — Auto (match the display: 120 on ProMotion, 60 otherwise),
  or a fixed 30/60/90/120 cap. Game logic always runs at its native 30 Hz;
  higher rates render interpolated frames between logic frames. The row
  shows the rate actually being rendered when it differs from the one
  selected — a cap that does not divide the display's refresh rate is
  rounded down so pacing stays even, and a device that cannot sustain the
  rate is stepped down automatically (see below).
  iPhone builds opt into Core Animation's full ProMotion range; the system
  can still lower the physical display rate for Low Power Mode, thermal
  pressure, or accessibility settings. Xcode 26.5's iPhone 17 Pro simulator
  reports a 60 Hz maximum through UIKit, so Auto resolves to 60 there despite
  the simulated model name; verify 120 Hz on physical ProMotion hardware.
  On a physical Metal device, the in-game FPS counter counts only drawables
  Core Animation confirms were displayed; frames dropped after submission no
  longer make the counter look healthy. The simulator retains elapsed
  submission-rate measurement because its Metal SDK does not expose that
  presentation feedback.
- **View** — Normal, Wireframe (Metal renderer), or Collision, which draws
  the collision mesh around Mario as translucent triangles: floors green,
  ceilings red, walls blue.
- **Retro Mode** — the authentic look: centered 4:3 picture with black
  pillarbox bars, 240-line rendering scaled up (Metal), and a 30 fps lock.
- **Show FPS** — rendered-frames-per-second counter.
- **HUD** — hide the heads-up display for clean screenshots.
- **Debug Features** — opens a dedicated screen for the game's user-facing
  debug tools:
  - **Level Select** — the debug level select the game has always contained
    but never exposed. With it on, exiting a course from the pause menu leads
    to the level select rather than back to the castle.
  - **Debug Info** — the working built-in debug text overlay. The separate
    profiler stays disabled because it times itself with `osGetTime`, which
    this port stubs out, so every profiler bar would otherwise read empty.

Save files and settings are stored in the app's `Documents` directory and
survive app updates (but not uninstalling). They are visible through Finder
file sharing and the Files app, so they can be backed up before a sideloaded
app expires or is removed. Builds made before this directory was exposed kept
the same files under `Library/Application Support`; each file is moved into
`Documents` automatically the first time it is used.

Writes are atomic — they go to a temporary file that is flushed to storage and
then renamed into place — so being killed by the system mid-write cannot leave
a truncated save behind. Settings are also flushed when the app is sent to the
background, since iOS terminates suspended apps without running any exit
handlers.

The app asks SDL's CoreAudio backend to use an `Ambient` AVAudioSession: it
respects the silent switch and allows music or podcasts from another app to
continue. SDL remains the single owner of session activation and interruption
handling; the app pauses or flushes its queued audio on background, route, and
media-service transitions so stale samples are not replayed on resume.

## Frame pacing under load

On iOS, SDL's `CADisplayLink` calls the port at the fastest cadence currently
available from the variable-rate display. A monotonic 30 Hz deadline inside
that callback controls game logic and audio, so ProMotion switching among
120, 80, 60, or lower system-selected rates cannot change game speed.
Interpolated Metal frames use absolute host presentation times; Core Animation
can drop one it cannot show without chaining the delay into the next logic
tick.

Sub-frames are still generated as one batch for each logic frame. If producing
that batch itself blocks the callback, the port measures the resulting logic
lateness and gives up a sub-frame after sustained overload — 120 fps steps
down to 60, and 60 to 30 — earning it back after about ten seconds of clean
frames. Brief hitches such as level loads are ignored rather than counted
against the frame rate.

## Build options

| Variable | Default | Meaning |
|----------|---------|---------|
| `VERSION` | `us` | Game version: `us` or `jp` (`eu` also builds but is less tested on the port) |
| `ENABLE_OPENGL` | off | Use the OpenGL ES 2.0 renderer instead of Metal |
| `PEDANTIC` | `0` | Compile the platform layer (`src/pc`) with `-Wall -Wextra -Wpedantic` |
| `IOS_MIN_VERSION` | `15.0` | Minimum iOS version |
| `IOS_ARCH` | `arm64` | Target architecture |
| `IOS_BUNDLE_ID` | `com.sm64port.<version>` | Bundle identifier |
| `IOS_DISPLAY_NAME` | `SM64 <version>` | Home screen name |
| `IOS_MARKETING_VERSION` | `1.0` | User-facing version |
| `IOS_BUILD_VERSION` | `1` | Incrementing bundle build number |
| `IOS_SIGN_IDENTITY` | `-` (ad-hoc) | Codesigning identity |
| `IOS_SDL2_PATH` | `ios/SDL2` | Where the static SDL2 lives |

Command-line and Xcode Release builds compile with `-O3 -flto -DNDEBUG` by default. `NDEBUG`
disables the display list interpreter's assertions, which would otherwise
abort a shipped app on a command it does not recognize; the bounds they
guarded are enforced at runtime instead, and an unsupported texture format
or a shader that fails to compile now renders as magenta rather than
terminating the process.

Game logic always runs at the native 30 Hz; with `HIGH_FPS=1` (the default)
each logic frame is rendered up to 4 times with everything interpolated
between the previous and current game state, reaching a real 120 rendered
frames per second on ProMotion displays. `HIGH_FPS=0` builds the vanilla
30 fps port. Wireframe view and the 240p half of retro mode are Metal
features; the GLES fallback ignores them (collision view and 4:3 work
everywhere).

## ROM-free verification

The maintained platform layer can be checked without copyrighted assets:

```
make -C tools/porttest check
make -C tools/porttest check-sanitize
make -C tools/porttest check-shaders
python3 tools/porttest/check-ios-project.py
```

The sanitizer target includes a structured display-list fuzz smoke pass. A
libFuzzer entry point is also available as `gfx_command_fuzz` when the selected
Clang installation includes the libFuzzer runtime.

Rendering a sub-frame re-runs the display list interpreter, which sounds
expensive but measures at roughly 0.1 ms for a scene of a few thousand
triangles — about 1% of the 33 ms logic frame even at 120 fps. The cost of
a higher frame rate is on the GPU (four times the draw calls and fill), not
in the interpreter, so the adaptive backoff above is what protects the game
from it rather than any CPU-side caching.

`PEDANTIC=1` is scoped to the modern platform code on purpose: the decompiled
1996 game code relies on GNU C extensions and idioms that predate these
warnings, so compiling it with `-Wpedantic` would bury real findings in
thousands of historical ones. The platform layer compiles clean under it.
