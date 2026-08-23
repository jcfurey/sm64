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
    if not (IOS / "patches/SDL2-2.30.7-uiscene.patch").is_file():
        fail("the SDL 2.30.7 UIScene patch is missing")

    print("iOS project: metadata, orientations, multitasking, AppIcon, SDL checksum, and UIScene patch passed")


if __name__ == "__main__":
    main()
