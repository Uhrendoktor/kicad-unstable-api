#!/usr/bin/env python3
"""Check KIFACE virtual-method order against a pinned KiCad source header.

This is a vtable-order guard, not a substitute for compiling the production
proxy against the exact KiCad headers and matching toolchain.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
KICAD_SOURCE_SHA = "b7066f886375b186ca8fa469de3a6382f6bba400"
KICAD_HEADER_URL = (
    "https://raw.githubusercontent.com/KiCad/kicad-source-mirror/"
    f"{KICAD_SOURCE_SHA}/include/kiway.h"
)


def strip_comments_and_literals(source: str) -> str:
    source = re.sub(r"/\*[\s\S]*?\*/", " ", source)
    source = re.sub(r"//[^\r\n]*", " ", source)
    source = re.sub(r'"(?:\\.|[^"\\])*"', '""', source)
    return re.sub(r"'(?:\\.|[^'\\])*'", "''", source)


def extract_block(source: str, marker: str) -> str:
    source = strip_comments_and_literals(source)
    start = source.find(marker)
    if start < 0:
        raise ValueError(f"Could not find {marker!r}")
    opening = source.find("{", start)
    if opening < 0:
        raise ValueError(f"Could not find opening brace for {marker!r}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]

    raise ValueError(f"Unterminated declaration block for {marker!r}")


def virtual_method_names(source: str) -> list[str]:
    block = extract_block(source, "struct KIFACE")
    matches = re.findall(r"\bvirtual\s+([^;{}]*?)\s*\(", block, flags=re.DOTALL)
    names = [declaration.strip().split()[-1] for declaration in matches]
    if not names:
        raise ValueError("No virtual KIFACE methods found")
    return names


def proxy_override_names(source: str) -> list[str]:
    block = extract_block(source, "class KIFACE_PROXY")
    return re.findall(
        r"\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?override\b",
        block,
        flags=re.DOTALL,
    )


def tested_dispatch_names(source: str) -> set[str]:
    return set(re.findall(r"\biface->([A-Za-z_]\w*)\s*\(", source))


def verify(kicad_header: str) -> str:
    upstream = virtual_method_names(kicad_header)
    synthetic = virtual_method_names(
        (ROOT / "tests/native/synthetic_kiface.hpp").read_text(encoding="utf-8")
    )

    if synthetic != upstream:
        raise ValueError(
            "Synthetic KIFACE virtual-method sequence differs from pinned KiCad "
            f"source.\nKiCad:    {upstream}\nSynthetic: {synthetic}"
        )

    required = [name for name in upstream if name != "~KIFACE"]
    overrides = proxy_override_names(
        (ROOT / "src/kiface_proxy.cpp").read_text(encoding="utf-8")
    )
    missing_overrides = sorted(set(required) - set(overrides))
    unexpected_overrides = sorted(set(overrides) - set(required))
    if missing_overrides or unexpected_overrides:
        raise ValueError(
            f"Proxy KIFACE overrides mismatch; missing={missing_overrides}, "
            f"unexpected={unexpected_overrides}"
        )

    tested = tested_dispatch_names(
        (ROOT / "tests/native/test_host.cpp").read_text(encoding="utf-8")
    )
    missing_dispatch = sorted(set(required) - tested)
    if missing_dispatch:
        raise ValueError(f"Synthetic host does not dispatch every KIFACE method: {missing_dispatch}")

    return (
        f"PASS: {len(upstream)} KIFACE virtual entries (including destructor); "
        f"{len(required)} proxy overrides and test dispatches match KiCad "
        f"{KICAD_SOURCE_SHA}."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--kiway-header",
        type=Path,
        help="Use a local include/kiway.h instead of fetching the pinned KiCad revision.",
    )
    args = parser.parse_args()

    try:
        if args.kiway_header:
            header = args.kiway_header.read_text(encoding="utf-8")
        else:
            with urlopen(KICAD_HEADER_URL, timeout=30) as response:
                header = response.read().decode("utf-8")
        print(verify(header))
    except (OSError, UnicodeError, ValueError) as error:
        print(f"KIFACE ABI guard failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
