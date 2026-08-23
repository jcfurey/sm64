#!/usr/bin/env python3
"""Validate an Xcode archive before TestFlight export or upload."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from fnmatch import fnmatchcase
import plistlib
import re
import subprocess
import sys
from pathlib import Path


UUID_PATTERN = re.compile(r"^UUID: ([0-9A-F-]+) \(([^)]+)\)", re.MULTILINE)


def fail(message: str) -> None:
    raise SystemExit(f"iOS archive check failed: {message}")


def load_plist(path: Path) -> dict[str, object]:
    if not path.is_file():
        fail(f"{path} does not exist")
    with path.open("rb") as source:
        value = plistlib.load(source)
    if not isinstance(value, dict):
        fail(f"{path} is not a property-list dictionary")
    return value


def run(command: list[str], description: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        fail(f"{description}: {details}")
    return result


def uuids(path: Path) -> dict[str, str]:
    output = run(["dwarfdump", "--uuid", str(path)], f"could not inspect UUIDs in {path}").stdout
    values = {architecture: uuid for uuid, architecture in UUID_PATTERN.findall(output)}
    if not values:
        fail(f"dwarfdump reported no UUIDs for {path}")
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="path to an .xcarchive")
    parser.add_argument(
        "--allow-unsigned",
        action="store_true",
        help="validate structure and symbols without requiring an Apple signature",
    )
    arguments = parser.parse_args()

    archive = arguments.archive.resolve()
    archive_info = load_plist(archive / "Info.plist")
    if archive_info.get("ArchiveVersion") != 2:
        fail("ArchiveVersion must be 2")

    properties = archive_info.get("ApplicationProperties")
    if not isinstance(properties, dict):
        fail("ApplicationProperties is missing from the archive Info.plist")

    application_path = properties.get("ApplicationPath")
    if not isinstance(application_path, str) or Path(application_path).is_absolute():
        fail("ApplicationPath is missing or is not a relative path")
    app = archive / "Products" / application_path
    if not app.is_dir() or app.suffix != ".app":
        fail(f"archived app {app} does not exist")

    bundle_checker = Path(__file__).with_name("check-ios-bundle.py")
    run([sys.executable, str(bundle_checker), str(app)], "app bundle validation failed")

    app_info = load_plist(app / "Info.plist")
    executable_name = app_info.get("CFBundleExecutable")
    if not isinstance(executable_name, str):
        fail("CFBundleExecutable is missing from the archived app")
    executable = app / executable_name
    if not executable.is_file():
        fail(f"archived executable {executable} does not exist")

    dsym = archive / "dSYMs" / f"{app.name}.dSYM"
    dwarf = dsym / "Contents" / "Resources" / "DWARF" / executable_name
    if not dwarf.is_file():
        fail(f"matching dSYM binary {dwarf} does not exist")

    executable_uuids = uuids(executable)
    dsym_uuids = uuids(dsym)
    if executable_uuids != dsym_uuids:
        fail(f"executable and dSYM UUIDs differ ({executable_uuids} != {dsym_uuids})")
    run(
        ["dwarfdump", "--verify", "--quiet", str(dsym)],
        "dSYM DWARF verification failed",
    )

    expected_architectures = properties.get("Architectures")
    if not isinstance(expected_architectures, list) or set(expected_architectures) != set(executable_uuids):
        fail(
            "archive architectures do not match the executable "
            f"({expected_architectures} != {sorted(executable_uuids)})"
        )

    team = properties.get("Team")
    identity = properties.get("SigningIdentity")
    profile_name = ""
    if not arguments.allow_unsigned:
        if not isinstance(team, str) or not team:
            fail("the archive has no signing team")
        if not isinstance(identity, str) or not identity:
            fail("the archive has no signing identity")
        run(
            ["codesign", "--verify", "--deep", "--strict", str(app)],
            "code-signature verification failed",
        )

        profile_path = app / "embedded.mobileprovision"
        if not profile_path.is_file():
            fail("the signed app has no embedded provisioning profile")
        profile_output = run(
            ["security", "cms", "-D", "-i", str(profile_path)],
            "could not decode the embedded provisioning profile",
        ).stdout.encode()
        profile = plistlib.loads(profile_output)
        if not isinstance(profile, dict):
            fail("the embedded provisioning profile is malformed")
        profile_teams = profile.get("TeamIdentifier")
        if not isinstance(profile_teams, list) or team not in profile_teams:
            fail(f"the provisioning profile does not belong to archive team {team}")
        profile_entitlements = profile.get("Entitlements")
        expected_app_id = f"{team}.{app_info.get('CFBundleIdentifier')}"
        profile_app_id = (
            profile_entitlements.get("application-identifier")
            if isinstance(profile_entitlements, dict)
            else None
        )
        if not isinstance(profile_app_id, str) or not fnmatchcase(expected_app_id, profile_app_id):
            fail(f"the provisioning profile does not authorize {expected_app_id}")
        expiration = profile.get("ExpirationDate")
        now = datetime.now(timezone.utc).replace(tzinfo=None)
        if not isinstance(expiration, datetime) or expiration <= now:
            fail("the embedded provisioning profile is expired or has no valid expiration date")
        raw_profile_name = profile.get("Name")
        profile_name = raw_profile_name if isinstance(raw_profile_name, str) else "unnamed profile"

    bundle_identifier = app_info.get("CFBundleIdentifier", "unknown")
    version = app_info.get("CFBundleShortVersionString", "unknown")
    build = app_info.get("CFBundleVersion", "unknown")
    signature = (
        "unsigned structure"
        if arguments.allow_unsigned
        else f"signed by {identity} ({team}), profile {profile_name}"
    )
    uuid_summary = ", ".join(
        f"{architecture} {uuid}" for architecture, uuid in sorted(executable_uuids.items())
    )
    print(
        f"iOS archive: {bundle_identifier} {version} ({build}), {signature}; "
        f"bundle, privacy metadata, dSYM, and UUIDs passed [{uuid_summary}]"
    )


if __name__ == "__main__":
    main()
