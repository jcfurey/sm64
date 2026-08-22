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

**Not** verified: anything on physical hardware — no device was connected. The
`jp` build has never been run here either; it needs `baserom.jp.z64`.

### Reproducing that

```sh
gmake TARGET_IOS=1 VERSION=us -j8                        # device .app + .ipa
gmake TARGET_IOS=1 IOS_SDK=iphonesimulator VERSION=us -j8
make -C tools/porttest check                             # 4 suites, no ROM needed
make -C tools/porttest check-shaders                     # real compilers, macOS
xcrun simctl install "iPhone 17 Pro" build/us_iossim/SM64-us.app
xcrun simctl launch --console-pty "iPhone 17 Pro" com.sm64port.us
```

One-time: `./ios/build-sdl2.sh` and `IOS_SDK=iphonesimulator ./ios/build-sdl2.sh`.

---

## Lane 2 — bundle and input plumbing

Small, self-contained, and each item is a defect a user would notice.

### 2.1 The app has no icon

`build/us_ios/SM64-us.app` contains exactly `Info.plist`, `_CodeSignature` and
`sm64`. Nothing else. It shows a blank tile on the home screen.

The `$(IOS_APP)` rule in the Makefile writes the plist and copies the
executable; there is no `actool` step, so an asset catalog would mean adding
one. The lighter route is the legacy path — PNGs copied into the bundle and
named by `CFBundleIconFiles` — which is enough for a sideloaded app and adds no
new tool dependency. Decide which before starting; the asset-catalog route is
the one Apple documents, the legacy route is the one that fits this Makefile.

### 2.2 `UIApplicationSupportsIndirectInputEvents` is missing

The app reports this itself on every launch:

```
sm64[78787:1660933] You need UIApplicationSupportsIndirectInputEvents
in your Info.plist for mouse support
```

Without it, trackpad and indirect pointer input are ignored — which matters on
iPad and in the simulator. One boolean key in `ios/Info.plist.in`.

### 2.3 Saves cannot leave the device

`ios/Info.plist.in` sets neither `UIFileSharingEnabled` nor
`LSSupportsOpeningDocumentsInPlace`, so the save file is sealed in the app
container. For an app sideloaded with a free Apple ID — re-signed every seven
days, and lost entirely if that lapses — there is no way to back a file up or
move it to another device.

`src/pc/fs.c` already writes saves atomically (`fs_atomic_test` covers the
abandoned-write case), so exposing the directory does not introduce a
torn-write risk.

### Testing lane 2

These are bundle properties, not runtime behaviour, so the natural check runs
against the built `.app` and needs no device: parse the generated `Info.plist`,
assert the required keys are present and correctly typed, and confirm every
file named by `CFBundleIconFiles` exists at its declared pixel size. That turns
"the icon silently stopped being copied" into a failure instead of a surprise
on someone's home screen.

It belongs beside `check` rather than inside it — `check` deliberately needs no
ROM and no Xcode, and this needs a built bundle.

---

## Lane 3 — haptics and controller remapping

### 3.1 Haptics on the touch controls

`CoreHaptics` is linked in `PLATFORM_LDFLAGS`, but nothing under `src/`
references it — it is there for SDL's MFi controller rumble. On-screen buttons
give no physical feedback at all.

The press and release transitions already exist in
`src/pc/controller/controller_touch.c`; that is where a tap would fire.
`UIImpactFeedbackGenerator` (UIKit) is markedly simpler than CoreHaptics for
this and is the right default; either way it needs an Objective-C shim, the way
`gfx_metal.mm` is one.

**Make it testable by construction.** Put the feedback behind a function
pointer the platform layer fills in. `touch_layout_test` already drives press,
drag and edit-mode transitions through the real hit-testing code, so it can
then assert the obvious rules — a tap fires once, a drag that moves a button
fires nothing, edit mode does not buzz on every frame — with a counter standing
in for the device.

### 3.2 Controller bindings are compile-time constants

`src/pc/controller/controller_sdl.c` hardcodes the mapping:

```c
if (SDL_GameControllerGetButton(sdl_cntrl, SDL_CONTROLLER_BUTTON_A)) pad->button |= A_BUTTON;
```

and `src/pc/configfile.h` stores only keyboard scancodes — `configKeyA` through
`configKeyStickRight`. There is no gamepad binding in the config at all, so a
controller whose layout the player dislikes cannot be changed.

Work: config entries for gamepad bindings, a remap path through
`controller_sdl.c`, and menu UI to drive it.

**The mapping is a pure function** — SDL button in, N64 button mask out — which
makes it the easy half to pin. A `controller_map_test` in `tools/porttest`
covers the defaults, a round-trip through `configfile`, and what happens to
out-of-range or unknown values, in the same shape `touch_layout_test` already
uses for the layout. Write the mapping as a table the test can read rather than
a chain of `if`s, and the test stays honest as bindings are added.

---

## Lane 4 — the audio session

**Flagged as the weakest of the four, and the reason is worth keeping.**

Lifecycle is already handled. `src/pc/gfx/gfx_sdl2.c` responds to
`SDL_APP_WILLENTERBACKGROUND` with `save_state_before_suspend()` and to
`SDL_APP_DIDENTERBACKGROUND` with `wait_for_foreground()`, and
`framerate_reset()` discards pacing history on resume, so measurements taken
across a suspension do not distort the backoff.

What is *not* handled: nothing in `src/` mentions `AVAudioSession`,
`setCategory`, or any interruption notification. The session category is
therefore whatever the default is, which leaves all of these unverified —

- the ringer/silent switch,
- an incoming call or other interruption, and whether audio resumes after,
- another app already playing audio,
- headphones unplugged, or any route change mid-game.

Deciding the category is a product question, not a technical one: `Ambient`
lets other audio keep playing and respects the silent switch, `Playback` takes
over and ignores it. Pick deliberately.

**Why this is hard to test.** The simulator reproduces route changes and
backgrounding but not a phone call, and the interesting cases are exactly the
ones that need real hardware. The honest structure is to make the *policy* a
pure function — event and current state in, desired state out — in a
translation unit `porttest` can link, test that exhaustively, and keep the
`AVAudioSession` binding underneath it thin enough to verify by hand. That way
the untestable part stays small and obvious instead of being spread through the
audio backend.

---

## Smaller debt

- **`.gitignore`'s porttest list is per-binary and has now bitten twice.**
  Lines 77–83 name each test executable individually, so adding a test without
  adding its line silently tracks a binary. It caught out `shader_gen_test` in
  `a081e14` — the same mistake `3176de6` made with the `sm64tools` binaries.
  A pattern, or a list generated from the Makefile's `PROGRAMS`, would end it.

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

- **The simulator runs the app sideways.** The app is landscape-only
  (`UISupportedInterfaceOrientations`) and the simulator boots portrait.
  Rotating it is `Cmd+←`, done by hand — driving the Simulator UI from a script
  needs Accessibility permission for the terminal.

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
