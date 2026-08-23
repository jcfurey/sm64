#!/usr/bin/env python3
"""ROM-free validation of the maintained iOS project inputs."""

from __future__ import annotations

import json
import plistlib
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
IOS = ROOT / "ios"
EXPECTED_SDL_SHA256 = "2508c80438cd5ff3bbeb8fe36b8f3ce7805018ff30303010b61b03bb83ab9694"
EXPECTED_PRIVACY_REASONS = {
    "NSPrivacyAccessedAPICategoryFileTimestamp": {"C617.1"},
    "NSPrivacyAccessedAPICategorySystemBootTime": {"35F9.1"},
}
EXPECTED_US_BUNDLE_ID = "com.jcfurey.sm64.us"
EXPECTED_US_TEAM = "7YGTR289AX"
EXPECTED_US_BUILD = 3
EXPECTED_MINIMUM_IOS = "15.0"


def fail(message: str) -> None:
    raise SystemExit(f"iOS project check failed: {message}")


def png_dimensions(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:26]
    if len(header) != 26 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        fail(f"{path} is not a PNG")
    return struct.unpack(">II", header[16:24])


def main() -> None:
    with (IOS / "Info.plist.in").open("rb") as source:
        info = plistlib.load(source)
    for key in ("CFBundleIcons", "CFBundleIcons~ipad"):
        icons = info.get(key)
        primary = icons.get("CFBundlePrimaryIcon") if isinstance(icons, dict) else None
        if not isinstance(primary, dict) or primary.get("CFBundleIconName") != "AppIcon":
            fail(f"Info.plist.in does not select the modern AppIcon asset for {key}")
    if info.get("UIRequiresFullScreen") is True:
        fail("the project disables iPad multitasking")
    if info.get("ITSAppUsesNonExemptEncryption") is not False:
        fail("Info.plist.in does not declare the app's export-compliance status")
    if info.get("CFBundleShortVersionString") != "$(MARKETING_VERSION)" \
            or info.get("CFBundleVersion") != "$(CURRENT_PROJECT_VERSION)":
        fail("Info.plist.in does not use Xcode's marketing and build versions")
    if set(info.get("UISupportedInterfaceOrientations", [])) != {
        "UIInterfaceOrientationLandscapeLeft",
        "UIInterfaceOrientationLandscapeRight",
        "UIInterfaceOrientationPortrait",
        "UIInterfaceOrientationPortraitUpsideDown",
    }:
        fail("the iPhone orientation list is incomplete")

    marketing_icon = IOS / "icons/AppIcon-1024.png"
    if png_dimensions(marketing_icon) != (1024, 1024):
        fail("AppIcon-1024.png is not 1024x1024")
    catalog = json.loads((IOS / "AppIconContents.json").read_text(encoding="utf-8"))
    images = catalog.get("images", [])
    if not any(image.get("filename") == marketing_icon.name and image.get("size") == "1024x1024"
               for image in images if isinstance(image, dict)):
        fail("the asset catalog template does not contain the marketing icon")

    build_script = (IOS / "build-sdl2.sh").read_text(encoding="utf-8")
    recorded = re.search(r'2\.30\.7\) SDL2_SHA256="([0-9a-f]{64})"', build_script)
    if recorded is None or recorded.group(1) != EXPECTED_SDL_SHA256:
        fail("the authenticated SDL 2.30.7 checksum is missing or changed")
    sdl_patch_path = IOS / "patches/SDL2-2.30.7-uiscene.patch"
    if not sdl_patch_path.is_file():
        fail("the SDL 2.30.7 UIScene patch is missing")
    sdl_patch = sdl_patch_path.read_text(encoding="utf-8")
    for required in (
        "sceneDidBecomeActive:(UIScene *)scene",
        "sceneWillResignActive:(UIScene *)scene",
        "sceneDidEnterBackground:(UIScene *)scene",
        "sceneWillEnterForeground:(UIScene *)scene",
        "preferredFrameRateRange = CAFrameRateRangeMake(minimum, maximum, maximum)",
        "mouse_scroll_direction = SDL_MOUSEWHEEL_FLIPPED",
    ):
        if required not in sdl_patch:
            fail(f"the SDL UIScene patch is missing runtime behavior: {required}")

    window_backend = (ROOT / "src/pc/gfx/gfx_sdl2.c").read_text(encoding="utf-8")
    if "SDL_WaitEvent" in window_backend:
        fail("the iOS display callback can still enter SDL_WaitEvent")
    if "ios_platform_is_app_active()" not in window_backend:
        fail("the iOS display callback does not return while its scene is inactive")

    metal_backend = (ROOT / "src/pc/gfx/gfx_metal.mm").read_text(encoding="utf-8")
    if "dispatch_semaphore_wait(mtl.frame_semaphore, DISPATCH_TIME_NOW)" not in metal_backend:
        fail("the Metal display callback can still wait indefinitely for an in-flight slot")
    if "presentDrawable:mtl.drawable atTime:" in metal_backend \
            or "presentDrawable:mtl.drawable afterMinimumDuration:" in metal_backend:
        fail("Metal still queues future presentations from a display callback")

    main_loop = (ROOT / "src/pc/pc_main.c").read_text(encoding="utf-8")
    if "const s32 max_logic_catchup = 4;" not in main_loop \
            or "produce_audio_for_logic_tick();" not in main_loop:
        fail("the iOS loop has lost its bounded independent logic/audio clock")

    project = (IOS / "SM64.xcodeproj/project.pbxproj").read_text(encoding="utf-8")
    if project.count(f"PRODUCT_BUNDLE_IDENTIFIER = {EXPECTED_US_BUNDLE_ID};") != 2:
        fail("the US target does not consistently use its permanent TestFlight bundle identifier")
    if project.count(f"DEVELOPMENT_TEAM = {EXPECTED_US_TEAM};") != 2:
        fail("the US target does not consistently use its TestFlight development team")
    us_build_versions: list[int] = []
    for configuration_id in ("A00000000000000000000030", "A00000000000000000000031"):
        match = re.search(
            rf"{configuration_id}.*?CURRENT_PROJECT_VERSION = ([0-9]+);",
            project,
            re.DOTALL,
        )
        if match is None:
            fail("a US target configuration has no numeric build number")
        us_build_versions.append(int(match.group(1)))
    if len(set(us_build_versions)) != 1 or us_build_versions[0] != EXPECTED_US_BUILD:
        fail(f"the US target build numbers are inconsistent or stale: {us_build_versions}")
    if project.count(f"IPHONEOS_DEPLOYMENT_TARGET = {EXPECTED_MINIMUM_IOS};") != 2:
        fail("the Xcode project does not consistently require iOS 15")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    xcode_bridge = (IOS / "xcode-build.sh").read_text(encoding="utf-8")
    if f"IOS_MIN_VERSION ?= {EXPECTED_MINIMUM_IOS}" not in makefile \
            or f"IPHONEOS_DEPLOYMENT_TARGET:-{EXPECTED_MINIMUM_IOS}" not in xcode_bridge \
            or f'IOS_MIN_VERSION:-{EXPECTED_MINIMUM_IOS}' not in build_script:
        fail("the Make, Xcode bridge, and SDL deployment baselines are inconsistent")

    with (IOS / "PrivacyInfo.xcprivacy").open("rb") as source:
        privacy = plistlib.load(source)
    if privacy.get("NSPrivacyTracking") is not False \
            or privacy.get("NSPrivacyTrackingDomains") != [] \
            or privacy.get("NSPrivacyCollectedDataTypes") != []:
        fail("the privacy manifest's no-tracking/no-collection declarations are malformed")
    actual_reasons = {
        entry.get("NSPrivacyAccessedAPIType"): set(entry.get("NSPrivacyAccessedAPITypeReasons", []))
        for entry in privacy.get("NSPrivacyAccessedAPITypes", [])
        if isinstance(entry, dict)
    }
    if actual_reasons != EXPECTED_PRIVACY_REASONS:
        fail(f"the privacy required-reason declarations differ: {actual_reasons}")

    with (IOS / "ExportOptions-TestFlight.plist").open("rb") as source:
        export = plistlib.load(source)
    if export.get("method") != "app-store-connect" or export.get("destination") != "upload" \
            or export.get("signingStyle") != "automatic" \
            or export.get("testFlightInternalTestingOnly") is not True \
            or export.get("uploadSymbols") is not True:
        fail("the internal TestFlight export options are missing or malformed")

    print("iOS project: distribution metadata, privacy, AppIcon, SDL lifecycle, and pacing checks passed")


if __name__ == "__main__":
    main()
