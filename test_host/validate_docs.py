#!/usr/bin/env python3
"""Validate repository documentation without requiring external services."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail(f"missing documentation file: {relative}")
    return path.read_text(encoding="utf-8")


def check_fences(relative: str, text: str) -> None:
    fences = len(re.findall(r"^```", text, flags=re.MULTILINE))
    if fences % 2:
        fail(f"unbalanced fenced code blocks in {relative}")


def check_local_links(relative: str, text: str) -> None:
    source = ROOT / relative
    for link in re.findall(r"\[[^\]]+\]\(([^)]+)\)", text):
        if link.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = (source.parent / link.split("#", 1)[0]).resolve()
        if not target.is_file():
            fail(f"broken local link in {relative}: {link}")


def require_all(relative: str, text: str, phrases: tuple[str, ...]) -> None:
    for phrase in phrases:
        if phrase not in text:
            fail(f"{relative} is missing required content: {phrase}")


def main() -> int:
    architecture = read("docs/system-architecture.md")
    manual = read("docs/user-manual.md")
    readme = read("README.md")

    check_fences("docs/system-architecture.md", architecture)
    check_fences("docs/user-manual.md", manual)
    check_local_links("docs/system-architecture.md", architecture)
    check_local_links("docs/user-manual.md", manual)
    check_local_links("README.md", readme)

    require_all(
        "docs/system-architecture.md",
        architecture,
        (
            "## 3. Hardware abstraction and example profile",
            "## 5. Startup sequence",
            "## 6. Runtime data flows",
            "## 8. Build, partitioning, and release model",
            "## 10. Production hardening checklist",
            "b2_modem.c",
            "b2_relay.c",
            "ci.yml",
            "release.yml",
        ),
    )
    require_all(
        "docs/user-manual.md",
        manual,
        (
            "## 4. Hardware preparation",
            "## 5. Install ESP-IDF and build the firmware",
            "## 6. Connect the board and flash the firmware",
            "## 7. First-boot commissioning",
            "## 8. Local console commands",
            "## 9. SIM7600 installation and SMS control",
            "## 11. Troubleshooting",
            "idf.py build",
            "idf.py -p /dev/ttyACM0 flash monitor",
            "RELAY1 ON",
            "RELAY2 TOGGLE",
        ),
    )
    require_all(
        "README.md",
        readme,
        ("docs/system-architecture.md", "docs/user-manual.md"),
    )

    print("Documentation validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
