#!/usr/bin/env python3
"""Verify that the user-facing Debug Features submenu remains reachable."""

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


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
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
