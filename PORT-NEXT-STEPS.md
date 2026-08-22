# Next steps for the port

Scope is the port (`TARGET_N64=0`) rather than the decompilation: the iOS app,
the rendering backends, and `tools/porttest`. Every claim below cites the file
or the observation it rests on, so none of it has to be taken on trust.

## Where things stand

Five commits on `claude/fork-review-s1g6fe`, in order:

| Commit | What |
|---|---|
| `7619602` | Untracked four build-output binaries; ignored the local SDL2 build |
| `7c13609` | Guarded the WUP adapter driver on `__linux__`, as its own headers already were |
| `adf5bcf` | Stopped the Metal combiner generator emitting `float4` where `float3` was required |
| `e183120` | Added an iOS Simulator target |
| `a081e14` | Tested the shader source every backend generates |

Verified: device and simulator builds compile; the app installs, launches, and
renders correctly in the iPhone 17 Pro simulator at a steady 60 fps (30 Hz
logic × 2 sub-frames); `tools/porttest` is green; 1,995 generated shaders pass
their real compilers.

The follow-up work after this handoff completed Lane 2: both iOS bundles now
carry a full opaque icon set, indirect input is declared, and saves live in the
file-sharing-visible `Documents` directory. An actual simulator launch showed
the old config migrate there and no longer emitted SDL's indirect-input warning.
`ios/SM64.xcodeproj` now provides shared US and JP schemes as well, using the
existing Makefile for compilation while Xcode handles SDK selection, bundling,
automatic signing, installation, and launch.

The app now declares all four interface orientations and the SDL UIKit patch
reports the window's live safe-area geometry to the touch layer. Independent
portrait and landscape layouts remain inside the usable rectangle across
notched iPhones and iPads, cancel stale fingers on rotation, preserve old
landscape-only layout files, and cap physical control size on large tablets.
`touch_layout_test` covers compact phone, Dynamic Island phone, and 13-inch
iPad geometries in both aspect directions.

Portrait now presents the complete 4:3 game frame below the top safe area and
uses the remaining height as a control deck instead of center-cropping the
scene. Touch buttons provide optional light haptics and can hide automatically
while a controller is connected. A dedicated controller screen remaps every
gameplay button and persists validated bindings. The audio layer now has an
explicit Ambient AVAudioSession policy with interruption, route-change, media
reset, and background handling. All of those decisions have ROM-free tests,
which also run in GitHub Actions.

**Not** verified: anything on physical hardware — no device was connected. The
`jp` build has never been run here either; it needs `baserom.jp.z64`.

### Reproducing that

```sh
gmake TARGET_IOS=1 VERSION=us -j8                        # device .app + .ipa
gmake TARGET_IOS=1 IOS_SDK=iphonesimulator VERSION=us -j8
make -C tools/porttest check                             # portable suites, no ROM needed
make -C tools/porttest check-shaders                     # real compilers, macOS
xcrun simctl install "iPhone 17 Pro" build/us_iossim/SM64-us.app
xcrun simctl launch --console-pty "iPhone 17 Pro" com.sm64port.us
```

One-time: `./ios/build-sdl2.sh` and `IOS_SDK=iphonesimulator ./ios/build-sdl2.sh`.

---

## Lane 2 — bundle and input plumbing (completed)

The Makefile takes the lightweight bundle-resource route: eleven opaque PNGs
under `ios/icons/` cover iPhone and iPad home-screen, Spotlight and Settings
sizes, are copied into the app, and are named explicitly by `CFBundleIcons` and
`CFBundleIcons~ipad`. The 1024 px source is kept beside them for future edits.

`ios/Info.plist.in` now sets `UIApplicationSupportsIndirectInputEvents`,
`UIFileSharingEnabled`, and `LSSupportsOpeningDocumentsInPlace` to real boolean
values. The first removes SDL's launch warning and enables pointer input on the
pre-iOS-17 versions the app still supports; the latter two expose `Documents`
through Finder file sharing and the Files app.

That last part needed more than plist keys. SDL's `SDL_GetPrefPath` resolves to
`Library/Application Support` on iOS, which file sharing does not expose.
`src/pc/fs_ios_storage.c` now selects `<HOME>/Documents` and atomically moves an
older save, config, or touch layout there on first use. If the move fails, the
caller stays on the legacy path rather than making an existing save disappear.
`fs_ios_storage_test` covers migration, destination precedence, path bounds and
the missing-home fallback.

`gmake TARGET_IOS=1 [IOS_SDK=iphonesimulator] check-ios-bundle` builds the app
and runs `tools/porttest/check-ios-bundle.py` against the result. It checks the
three boolean values, four declared orientations, plus existence, dimensions
and opaque RGB format for every declared icon. It also verifies the packaged
executable contains the dedicated Debug Features screen and its two user-facing
controls. This remains separate from portable `make -C tools/porttest check`,
which still needs neither a ROM nor Xcode.

The same change replaced `.gitignore`'s per-executable porttest list with
`/tools/porttest/*_test`, so new test binaries no longer become tracked files.

---

## Lane 3 — haptics and controller remapping (completed)

### 3.1 Haptics on the touch controls

On-screen buttons use a light `UIImpactFeedbackGenerator` behind a callback
installed by `ios_support.mm`. The touch layer owns the transition decision,
so `touch_layout_test` verifies that a press fires once while layout dragging
does not fire at all. The option is user-configurable.

### 3.2 Controller bindings

`controller_gamepad.c` converts a platform-neutral snapshot into N64 input by
table, including trigger and right-stick directions. The Controller options
screen edits those bindings, config loading rejects out-of-range values, and
`controller_map_test` covers defaults, remapping, persistence, and controller
presence. SDL now only translates its API into that neutral snapshot.

---

## Lane 4 — the audio session (completed in software)

The software policy is complete; real interruptions and route changes still
belong in the physical-device verification pass.

Lifecycle is already handled. `src/pc/gfx/gfx_sdl2.c` responds to
`SDL_APP_WILLENTERBACKGROUND` with `save_state_before_suspend()` and to
`SDL_APP_DIDENTERBACKGROUND` with `wait_for_foreground()`, and
`framerate_reset()` discards pacing history on resume, so measurements taken
across a suspension do not distort the backoff.

`ios_support.mm` deliberately selects `Ambient`, so the game respects the
silent switch and mixes with existing audio. It observes interruptions, route
changes, and media-service resets; the existing SDL lifecycle path reports
background and foreground transitions into the same state machine. The pure C
policy is covered by `audio_session_policy_test`, while the Objective-C binding
is kept to category setup, notifications, and applying the policy decision.

---

## Smaller debt

- **HLSL has no real-compiler coverage.** `check-shaders.sh` reports `d3d` as
  `SKIPPED` because no Homebrew formula provides `dxc` and Apple ships no HLSL
  compiler. It is rule-checked by `shader_gen_test` only. Closing this means
  either a Windows CI leg running `fxc`/`dxc`, or building DirectXShaderCompiler
  from source. Until then the gap is real and the script says so out loud.

- **`sprintf` throughout the shader generators.** Apple's SDK deprecates it, so
  the warning is macOS-only and is currently suppressed in the porttest
  `CFLAGS`. Converting to `snprintf` against the existing `char tmp[256]` would
  be a small, strict improvement; it was left out of `a081e14` to keep that
  commit a pure move.

- **Simulator rotation automation still needs Accessibility permission.** The
  app itself supports portrait and landscape and relays SDL resize events, but
  `simctl` has no public rotation command in the installed Xcode. Simulator's
  `Cmd+←` / `Cmd+→` shortcuts remain the direct manual check.

---

## Deliberately not doing

**Vulkan.** There is no Vulkan backend in this repository. The Makefile knows
four graphics backends — `ENABLE_DX11`, `ENABLE_DX12`, `ENABLE_METAL`,
`ENABLE_OPENGL` — and a search for "vulkan" outside the vendored SDL2 tree
matches exactly one file, `text/de/courses.h`, where it is the German for
*volcano*. SDL2 itself has Vulkan support, which is the likely source of any
apparent hit.

Adding one would be a feature, not a test: a `gfx_vulkan.c` implementing the
`GfxRenderingAPI` contract plus a shader path emitting **SPIR-V** rather than
source text, since Vulkan will not take GLSL at runtime the way the other three
do. That means precompiling with glslang/shaderc at build time or bundling a
compiler — a genuinely different shape. On iOS it would run through MoltenVK on
top of Metal, strictly worse than the native Metal backend already here; the
case for Vulkan is Linux or Android, replacing the GL 1.10 / GLES 2.0 path.

`shader_gen_test` is a table of backend descriptors, so a fifth entry is one row
plus a width function. If Vulkan is ever added, it is covered by construction.

---

## Two traps worth not re-learning

Both of these cost real time in `a081e14`, and both looked like success.

**`xcrun metal` costs ~2.5s per invocation** re-resolving the toolchain, against
0.03s for the binary it finds. Across a few hundred shaders that is the
difference between fifteen seconds and half an hour. Resolve the compiler once,
then call it directly.

**A harness that never ran must not look like one that found nothing.**
Inlining that compiler's path into an `xargs -I` template overflows the
argument limit — it lives on a long cryptex mount — and xargs then reports
`command line cannot be assembled, too long` on stderr, compiles nothing, and
exits clean. The script reported "0 rejected" while doing no work at all.
`compile_set` now makes every file account for itself with exactly one `OK` or
one `FAIL`, and treats a mismatch as a `HARNESS ERROR` rather than a pass.
Any new checking harness should carry the same property.
