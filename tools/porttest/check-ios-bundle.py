#!/usr/bin/env python3
"""Validate the metadata and icon resources in a built iOS app bundle."""

from __future__ import annotations

import plistlib
import struct
import sys
from pathlib import Path


IPHONE_ICONS = {
    "Icon-60@2x.png": (120, 120),
    "Icon-60@3x.png": (180, 180),
    "Icon-Small-40@2x.png": (80, 80),
    "Icon-Small-40@3x.png": (120, 120),
    "Icon-Small@2x.png": (58, 58),
    "Icon-Small@3x.png": (87, 87),
}

IPAD_ICONS = {
    "Icon-76.png": (76, 76),
    "Icon-76@2x.png": (152, 152),
    "Icon-83.5@2x.png": (167, 167),
    "Icon-Small-40.png": (40, 40),
    "Icon-Small-40@2x.png": (80, 80),
    "Icon-Small.png": (29, 29),
    "Icon-Small@2x.png": (58, 58),
}

REQUIRED_DEBUG_SCREEN_TEXT = (
    "DEBUG FEATURES",
    "LEVEL SELECT",
    "DEBUG INFO",
)

ALL_ORIENTATIONS = {
    "UIInterfaceOrientationLandscapeLeft",
    "UIInterfaceOrientationLandscapeRight",
    "UIInterfaceOrientationPortrait",
    "UIInterfaceOrientationPortraitUpsideDown",
}


def fail(message: str) -> None:
    raise SystemExit(f"iOS bundle check failed: {message}")


def icon_files(info: dict[str, object], key: str) -> list[str]:
    icons = info.get(key)
    if not isinstance(icons, dict):
        fail(f"{key} is missing or is not a dictionary")
    primary = icons.get("CFBundlePrimaryIcon")
    if not isinstance(primary, dict):
        fail(f"{key}.CFBundlePrimaryIcon is missing or is not a dictionary")
    files = primary.get("CFBundleIconFiles")
    if not isinstance(files, list) or not all(isinstance(item, str) for item in files):
        fail(f"{key}.CFBundlePrimaryIcon.CFBundleIconFiles is not an array of strings")
    return files


def png_dimensions(path: Path) -> tuple[int, int]:
    contents = path.read_bytes()
    header = contents[:33]
    if len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        fail(f"{path.name} is not a valid PNG")
    width, height, bit_depth, color_type = struct.unpack(">IIBB", header[16:26])
    if bit_depth != 8 or color_type != 2:
        fail(f"{path.name} must be an opaque 8-bit RGB PNG")

    offset = 8
    while offset + 12 <= len(contents):
        chunk_size = struct.unpack(">I", contents[offset:offset + 4])[0]
        chunk_type = contents[offset + 4:offset + 8]
        offset += chunk_size + 12
        if offset > len(contents):
            fail(f"{path.name} contains a truncated PNG chunk")
        if chunk_type == b"tRNS":
            fail(f"{path.name} contains a transparency chunk")
        if chunk_type == b"IEND":
            return width, height

    fail(f"{path.name} has no PNG end marker")
    return width, height


def check_icon_set(bundle: Path, info: dict[str, object], plist_key: str,
                   expected: dict[str, tuple[int, int]]) -> None:
    declared = icon_files(info, plist_key)
    if len(declared) != len(expected) or set(declared) != set(expected):
        missing = sorted(set(expected) - set(declared))
        extra = sorted(set(declared) - set(expected))
        fail(f"{plist_key} icon list differs (missing={missing}, extra={extra})")

    for name, size in expected.items():
        path = bundle / name
        if not path.is_file():
            fail(f"declared icon {name} is absent from the bundle")
        actual = png_dimensions(path)
        if actual != size:
            fail(f"{name} is {actual[0]}x{actual[1]}, expected {size[0]}x{size[1]}")


def check_debug_screen(bundle: Path, info: dict[str, object]) -> None:
    executable_name = info.get("CFBundleExecutable")
    if not isinstance(executable_name, str):
        fail("CFBundleExecutable is missing or is not a string")
    executable = bundle / executable_name
    if not executable.is_file():
        fail(f"bundle executable {executable_name} does not exist")

    contents = executable.read_bytes()
    for text in REQUIRED_DEBUG_SCREEN_TEXT:
        if text.encode("ascii") + b"\0" not in contents:
            fail(f"user-facing debug screen text {text!r} is absent from the executable")


def check_scene_manifest(info: dict[str, object]) -> None:
    manifest = info.get("UIApplicationSceneManifest")
    if not isinstance(manifest, dict):
        fail("UIApplicationSceneManifest is missing or is not a dictionary")
    if manifest.get("UIApplicationSupportsMultipleScenes") is not False:
        fail("UIApplicationSupportsMultipleScenes must be the boolean false")

    configurations = manifest.get("UISceneConfigurations")
    if not isinstance(configurations, dict):
        fail("UIApplicationSceneManifest.UISceneConfigurations is missing")
    application_role = configurations.get("UIWindowSceneSessionRoleApplication")
    if not isinstance(application_role, list):
        fail("the application scene-role configuration is missing")

    expected = {
        "UISceneConfigurationName": "SDLSceneConfiguration",
        "UISceneDelegateClassName": "SDLUIKitSceneDelegate",
    }
    if not any(isinstance(item, dict) and all(item.get(key) == value for key, value in expected.items())
               for item in application_role):
        fail("the SDL UIKit scene configuration is missing or malformed")


def check_orientations(info: dict[str, object]) -> None:
    for key in ("UISupportedInterfaceOrientations", "UISupportedInterfaceOrientations~ipad"):
        values = info.get(key)
        if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
            fail(f"{key} is missing or is not an array of strings")
        if set(values) != ALL_ORIENTATIONS:
            fail(f"{key} must declare all four interface orientations")


def main() -> None:
    if len(sys.argv) != 2:
        fail(f"usage: {Path(sys.argv[0]).name} path/to/App.app")

    bundle = Path(sys.argv[1])
    plist_path = bundle / "Info.plist"
    if not plist_path.is_file():
        fail(f"{plist_path} does not exist")

    with plist_path.open("rb") as plist_file:
        info = plistlib.load(plist_file)

    for key in (
        "CADisableMinimumFrameDurationOnPhone",
        "UIApplicationSupportsIndirectInputEvents",
        "UIFileSharingEnabled",
        "LSSupportsOpeningDocumentsInPlace",
    ):
        if info.get(key) is not True:
            fail(f"{key} must be the boolean true")

    if info.get("UIDeviceFamily") != [1, 2]:
        fail("UIDeviceFamily must support both iPhone (1) and iPad (2)")

    check_icon_set(bundle, info, "CFBundleIcons", IPHONE_ICONS)
    check_icon_set(bundle, info, "CFBundleIcons~ipad", IPAD_ICONS)
    check_scene_manifest(info)
    check_orientations(info)
    check_debug_screen(bundle, info)

    unique_icons = len(set(IPHONE_ICONS) | set(IPAD_ICONS))
    print(
        f"iOS bundle: UIScene lifecycle, ProMotion, all orientations, 3 metadata flags, iPhone/iPad support, "
        f"{unique_icons} icons, and Debug Features screen passed"
    )


if __name__ == "__main__":
    main()
