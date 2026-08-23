# Port status

Last verified: 2026-08-22. This document covers the maintained native port
surface (`TARGET_N64=0`), especially iOS, `src/pc`, and `tools/porttest`.

## Current iOS implementation

- Native arm64 iPhone and iPad targets for US and JP ROMs.
- Shared Xcode schemes, simulator and device SDK selection, automatic signing,
  a real source-debuggable Debug configuration, and dSYM generation.
- Metal renderer by default with an OpenGL ES 2 fallback.
- UIKit single-scene lifecycle through the pinned SDL 2.30.7 patch.
- All four interface orientations and iPad multitasking/Stage Manager sizing.
- Full-bleed landscape rendering with safe-area-aware HUD and touch controls.
- A complete 4:3 portrait game panel above a compact control deck.
- Independent, editable portrait and landscape touch layouts, haptics, and
  automatic hiding when a hardware controller is connected.
- SDL GameController support with persistent N64 button remapping.
- Frame interpolation up to 120 rendered fps, adaptive load backoff, optional
  frame caps, and an authentic 30 fps Retro Mode.
- Ambient CoreAudio session policy, lifecycle queue pausing, and route/media
  reset flushing without competing AVAudioSession ownership.
- Atomic save/config/layout writes in the file-sharing-visible Documents
  directory, including migration from the former SDL preference path.
- Legacy launch icons plus a compiled modern AppIcon containing the 1024 px
  marketing source.
- TestFlight-ready Release archives with optimized line tables, matching dSYMs,
  export-compliance metadata, and a no-tracking/no-collection privacy manifest.
  Distribution is intentionally restricted to private internal TestFlight use;
  ROM-derived builds are not for external testing or general App Store release.
- An iOS 15 deployment baseline across Xcode, GNU Make, and SDL builds, matching
  App Store Connect's announced Spring 2027 minimum-version requirement.
- A main-menu Debug Features button exposing Level Select and Debug Info.

The iOS executable contains ROM-derived assets. Build it only from a ROM you
own and do not distribute the resulting app.

## Verification

The ROM-free test surface includes:

```sh
make -C tools/porttest check
make -C tools/porttest check-sanitize
make -C tools/porttest check-shaders
python3 tools/porttest/check-ios-project.py
```

Coverage includes durable writes, iOS storage migration, frame pacing,
viewport safe areas, touch layouts and ownership, controller mappings,
configuration parsing, graphics pools and malformed commands, texture-cache
recycling, every generated shader configuration, and source-level reachability
of the Debug Features submenu.

GitHub Actions runs the portable suite, ASan/UBSan, a structured display-list
fuzz smoke pass, sampled OpenGL/GLES compiler checks, the Windows atomic-save
test, shell syntax checks, and Apple project metadata validation. Metal and HLSL
compiler checks are reported as skipped when their optional toolchains are
unavailable.

The iOS bundle validator checks the scene manifest, ProMotion opt-in, all
orientations, iPhone/iPad support, multitasking, file-sharing metadata, legacy
icons, compiled AppIcon catalog, export compliance, privacy declarations, and
Debug Features linkage.

## Remaining external validation

No physical iOS device was connected during this work. The following require
real hardware rather than more simulator code:

- 120 Hz ProMotion presentation and adaptive backoff under thermal load.
- Dynamic Island and home-indicator appearance in all orientations.
- Silent switch, Bluetooth/headphone route changes, interruptions, and media
  service resets.
- Touch haptics and controller connect/disconnect behavior.
- Background/foreground restoration after prolonged suspension.

The JP target also requires a legal `baserom.jp.z64` and has not been launched
in this environment.

## Toolchain notes

- `ios/build-sdl2.sh` authenticates the SDL archive and the maintained patch is
  version-specific. Add a new checksum and patch together when upgrading SDL.
- Xcode installs the Metal command stub even when the optional Metal Toolchain
  component is absent. `check-shaders.sh` preflights the compiler and reports a
  skip instead of turning that environment state into false shader failures.
- HLSL remains structurally tested on every host. A real HLSL compile requires
  `dxc`; the shader checker uses it automatically when available.
- Historical decompilation and vendored sources intentionally are not subjected
  to modern style rewrites. Pedantic warnings target the maintained platform
  layer, where they remain actionable.
