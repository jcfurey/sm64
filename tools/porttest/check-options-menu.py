#!/usr/bin/env python3
"""Static checks on the options menu table.

Verifies that the Debug Features submenu stays reachable, and that every
option's declared choice count matches the array it indexes. opt_cycle
wraps on numChoices and opt_set indexes its value array with the result,
so a count larger than its array reads out of bounds and stores garbage
into the config -- which is exactly what happened when the 90 fps choice
was removed and the count was left at 5.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/game/options_menu.c"


def fail(message: str) -> None:
    raise SystemExit(f"options menu check failed: {message}")


def initializer(source: str, declaration: str) -> str:
    match = re.search(rf"{re.escape(declaration)}\s*=\s*\{{(.*?)\n\}};", source, re.DOTALL)
    if match is None:
        fail(f"could not find {declaration}")
    return match.group(1)


def array_lengths(source: str) -> dict:
    """Lengths of the file-scope choice/value arrays, by name."""
    lengths = {}
    for match in re.finditer(
        r"static const (?:char \*|unsigned int |f32 )\s*(\w+)\[\]\s*=\s*\{(.*?)\};",
        source,
        re.DOTALL,
    ):
        name, body = match.group(1), match.group(2)
        body = re.sub(r"//[^\n]*", "", body)
        items = [part for part in body.split(",") if part.strip()]
        lengths[name] = len(items)
    return lengths


def check_choice_counts(source: str) -> None:
    lengths = array_lengths(source)
    table = initializer(source, "static const struct OptionDef sOptions[OPT_COUNT]")
    checked = 0

    for option, choices, count in re.findall(
        r"\[(\w+)\]\s*=\s*\{[^}]*?,\s*(\w+)\s*,\s*([A-Za-z0-9_]+\(?\w*)\s*\)?\s*\}", table
    ):
        if choices == "NULL":
            continue
        if choices not in lengths:
            continue
        if count == f"ARRAY_COUNT({choices}":
            checked += 1  # derived from the array it indexes; cannot drift
            continue
        if not count.isdigit():
            continue  # e.g. GAMEPAD_INPUT_COUNT, resolved by the compiler
        if int(count) != lengths[choices]:
            fail(
                f"{option} declares {count} choices but {choices} has "
                f"{lengths[choices]}; opt_cycle would index past the end"
            )
        checked += 1

    # The frame cap row indexes a second array in opt_set, which has to agree
    frame_caps = re.search(r"frameCaps\[\]\s*=\s*\{(.*?)\}", source, re.DOTALL)
    if frame_caps is not None:
        values = [v for v in frame_caps.group(1).split(",") if v.strip()]
        if "sChoicesFrameCap" in lengths and len(values) != lengths["sChoicesFrameCap"]:
            fail(
                f"frameCaps has {len(values)} values but sChoicesFrameCap has "
                f"{lengths['sChoicesFrameCap']}"
            )

    print(f"options menu: {checked} choice counts match their arrays")


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    check_choice_counts(source)
    main_options = initializer(source, "static const enum OptionId sMainOptions[]")
    debug_options = initializer(source, "static const enum OptionId sDebugOptions[]")

    if "OPT_DEBUG_FEATURES" not in main_options:
        fail("DEBUG FEATURES is not present on the main options page")
    for option in ("OPT_LEVEL_SELECT", "OPT_DEBUG_INFO"):
        if option not in debug_options:
            fail(f"{option} is absent from the Debug Features page")

    route = re.search(
        r"selected\s*==\s*OPT_DEBUG_FEATURES.*?"
        r"sMenuPage\s*=\s*OPT_PAGE_DEBUG.*?sMenuSel\s*=\s*0",
        source,
        re.DOTALL,
    )
    if route is None:
        fail("pressing A on DEBUG FEATURES does not route to the debug page")

    print("options menu: Debug Features is visible, opens with A, and exposes both controls")


if __name__ == "__main__":
    main()
