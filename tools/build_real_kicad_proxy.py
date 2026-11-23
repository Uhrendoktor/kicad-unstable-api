#!/usr/bin/env python3
"""Build production KIFACE proxies with flags from a configured KiCad build.

KiCad does not publish a stable C++ ABI. This tool reuses the actual editor
translation-unit compile flags from the matching KiCad compile_commands.json
and compiles the proxy against those same source, generated and wxWidgets
headers. It intentionally builds Linux ELF modules only; other platforms need
their native build toolchain and packaging rules.
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any

DEFAULT_KICAD_SOURCE_SHA = "b7066f886375b186ca8fa469de3a6382f6bba400"
EDITOR_SOURCES = {
    "eeschema": "eeschema/eeschema.cpp",
    "pcbnew": "pcbnew/pcbnew.cpp",
}


def resolve_entry(entries: list[dict[str, Any]], relative_source: str) -> dict[str, Any]:
    suffix = Path(relative_source).as_posix()
    matches = [
        item for item in entries
        if Path(item["file"]).as_posix().endswith("/" + suffix)
        or Path(item["file"]).as_posix() == suffix
    ]
    if len(matches) != 1:
        raise ValueError(
            f"Expected exactly one compile command for {relative_source}; found {len(matches)}."
        )
    return matches[0]


def command_arguments(entry: dict[str, Any]) -> list[str]:
    if "arguments" in entry:
        return list(entry["arguments"])
    if "command" in entry:
        return shlex.split(entry["command"], posix=(os.name != "nt"))
    raise ValueError("compile_commands entry has neither 'arguments' nor 'command'.")


def compiler_and_flags(entry: dict[str, Any]) -> tuple[str, list[str]]:
    args = command_arguments(entry)
    compiler_names = {"c++", "g++", "clang++", "icpx", "icpc"}
    compiler_index = next(
        (i for i, arg in enumerate(args) if Path(arg).name in compiler_names),
        None,
    )
    if compiler_index is None:
        raise ValueError(f"Could not identify a C++ compiler in compile command: {args[:5]}")
    compiler = args[compiler_index]
    directory = Path(entry["directory"])
    raw_source = Path(entry["file"])
    source = (raw_source if raw_source.is_absolute() else directory / raw_source).resolve()
    flags: list[str] = []
    skip_next = False
    discard_with_value = {"-o", "-MF", "-MT", "-MQ", "-MJ"}
    discard_single = {"-c", "-MD", "-MMD", "-MP"}

    for arg in args[compiler_index + 1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in discard_with_value:
            skip_next = True
            continue
        if arg in discard_single:
            continue
        if arg.startswith(("-o", "-MF", "-MT", "-MQ", "-MJ")) and arg != "-ObjC":
            continue
        try:
            candidate = Path(arg)
            candidate = candidate if candidate.is_absolute() else directory / candidate
            if not arg.startswith("-") and candidate.resolve() == source:
                continue
        except (OSError, ValueError):
            pass
        flags.append(arg)

    return compiler, flags


def parse_nm_symbol_names(output: str) -> set[str]:
    names: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            names.add(fields[-1].split("@", 1)[0])
    return names


def missing_gal_symbols(proxy_undefined: str, gal_defined: str) -> list[str]:
    undefined = parse_nm_symbol_names(proxy_undefined)
    defined = parse_nm_symbol_names(gal_defined)
    required = {name for name in undefined if name.startswith("_ZN5KIGFX")}
    return sorted(required - defined)


def make_link_command(
    entry: dict[str, Any],
    source_file: Path,
    output_file: Path,
    editor: str,
    abi_tag: str,
    gal_library: Path,
) -> list[str]:
    compiler, flags = compiler_and_flags(entry)
    project_include = Path(__file__).resolve().parents[1] / "include"
    flags.extend(["-I", str(project_include)])
    if not any(flag.startswith("-std=") for flag in flags):
        flags.append("-std=c++20")
    if "-fPIC" not in flags:
        flags.append("-fPIC")

    flags.extend([
        f'-DKUA_KICAD_ABI_TAG="{abi_tag}"',
        "-DKUA_BUILD_EDITOR_PCB" if editor == "pcbnew" else "-DKUA_BUILD_EDITOR_SCHEMATIC",
    ])
    # KiCad loads this module eagerly before it loads the original KIFACE.
    # Therefore every KiCad symbol referenced directly by the proxy must be
    # resolvable at dlopen(RTLD_NOW) time. GAL's VIEW/VIEW_OVERLAY methods live
    # in libkigal, which the editor launcher itself does not load.
    return [
        compiler, *flags, "-shared", str(source_file),
        str(gal_library),
        "-Wl,-rpath,$ORIGIN/../common/gal",
        "-ldl", "-o", str(output_file),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kicad-source", type=Path, required=True)
    parser.add_argument("--kicad-build", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--expected-source-sha",
        default=DEFAULT_KICAD_SOURCE_SHA,
        help="Required KiCad source commit (default: the repository's pinned baseline).",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    try:
        source_root = args.kicad_source.resolve()
        build_root = args.kicad_build.resolve()
        proxy_source = Path(__file__).resolve().parents[1] / "src" / "kiface_proxy.cpp"
        compile_db = build_root / "compile_commands.json"
        header = source_root / "include" / "kiway.h"

        if not header.is_file() or not proxy_source.is_file():
            raise ValueError("KiCad include/kiway.h or this repository's src/kiface_proxy.cpp is missing.")
        if not compile_db.is_file():
            raise ValueError(
                f"{compile_db} is missing. Configure KiCad with Ninja or Makefiles first."
            )

        head = subprocess.run(
            ["git", "-C", str(source_root), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if head != args.expected_source_sha:
            raise ValueError(
                f"KiCad source commit mismatch: expected {args.expected_source_sha}, found {head}."
            )

        subprocess.run(
            [
                sys.executable,
                str(Path(__file__).resolve().with_name("verify_kiface_abi.py")),
                "--kiway-header", str(header),
            ],
            check=True,
        )

        entries = json.loads(compile_db.read_text(encoding="utf-8"))
        gal_candidates = list((build_root / "common" / "gal").glob("libkigal.so"))
        if not gal_candidates:
            gal_candidates = list((build_root / "common" / "gal").glob("libkigal.so.*"))
        if not gal_candidates:
            raise ValueError(
                "KiCad libkigal was not built. Build the 'gal' target first: "
                "cmake --build <kicad-build> --target gal"
            )
        gal_library = gal_candidates[0].resolve()
        if not gal_library.is_file():
            raise ValueError(f"KiCad GAL library is not a regular file: {gal_library}")

        output_dir = args.output_dir.resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        for editor, relative_source in EDITOR_SOURCES.items():
            entry = resolve_entry(entries, relative_source)
            output = output_dir / f"_{editor}.kiface"
            command = make_link_command(entry, proxy_source, output, editor, head, gal_library)
            print(f"[{editor}] compiler command from {relative_source}")
            if args.dry_run:
                print(shlex.join(command))
                continue

            subprocess.run(command, cwd=entry["directory"], check=True)
            symbols = subprocess.run(
                ["nm", "-D", "--defined-only", str(output)],
                check=True, capture_output=True, text=True,
            ).stdout
            if not any(line.rstrip().endswith(" KIFACE_1") for line in symbols.splitlines()):
                raise ValueError(f"{output} does not export KIFACE_1.")

            dynamic = subprocess.run(
                ["readelf", "-d", str(output)],
                check=True, capture_output=True, text=True,
            ).stdout
            if "libkigal.so" not in dynamic:
                raise ValueError(f"{output} does not declare a DT_NEEDED dependency on KiCad libkigal.")
            if "$ORIGIN/../common/gal" not in dynamic:
                raise ValueError(f"{output} is missing its relative KiCad build-tree GAL RUNPATH.")

            undefined = subprocess.run(
                ["nm", "-D", "--undefined-only", str(output)],
                check=True, capture_output=True, text=True,
            ).stdout
            gal_defined = subprocess.run(
                ["nm", "-D", "--defined-only", str(gal_library)],
                check=True, capture_output=True, text=True,
            ).stdout
            missing_gal = missing_gal_symbols(undefined, gal_defined)
            if missing_gal:
                raise ValueError(
                    f"{output} references GAL symbols not exported by {gal_library}: "
                    + ", ".join(missing_gal)
                )
            print(
                f"PASS: {output} exports KIFACE_1, links libkigal, and resolves "
                "all "
                f"direct GAL references (KiCad {head})."
            )

    except (OSError, subprocess.CalledProcessError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"real KiCad proxy build failed: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
