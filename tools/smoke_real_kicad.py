#!/usr/bin/env python3
"""Reversible live-editor smoke test for a matching Linux KiCad installation.

The script can use one shared plugin directory or separate Eeschema/PCB Editor
build directories. It temporarily installs the proxy, places the probe
extension beside each editor KIFACE, checks real frame/canvas/view handles and
GAL calls, and restores all original modules in a finally block.
Use only on a disposable KiCad installation or integration runner.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("kua_smoke_modloader", ROOT / "tools" / "modloader.py")
if not SPEC or not SPEC.loader:
    raise RuntimeError("Could not load tools/modloader.py")
modloader = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = modloader
SPEC.loader.exec_module(modloader)


def stop_process(process: subprocess.Popen, grace_seconds: float = 10.0) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=grace_seconds)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def run_editor(
    name: str,
    editor_kind: int,
    executable: Path,
    arguments: list[str],
    log_file: Path,
    output_log: Path,
    timeout_seconds: float,
) -> None:
    environment = os.environ.copy()
    environment["KUA_SMOKE_LOG"] = str(log_file)
    with output_log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(
            [str(executable), *arguments],
            cwd=executable.parent,
            env=environment,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        deadline = time.monotonic() + timeout_seconds
        last_content = ""
        required_events = (
            f"initialize|editor={editor_kind}|",
            f"window|editor={editor_kind}|",
            f"overlay-ready|editor={editor_kind}|",
        )
        try:
            while time.monotonic() < deadline:
                last_content = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
                missing = [event for event in required_events if event not in last_content]
                if not missing:
                    print(f"PASS: {name} initialized, created a real editor frame, and exercised GAL overlays.")
                    return
                if process.poll() is not None:
                    raise RuntimeError(
                        f"{name} exited with code {process.returncode} before events {missing}.\n"
                        f"Extension log:\n{last_content}\nProcess log:\n{output_log.read_text(errors='replace')}"
                    )
                time.sleep(0.25)

            raise TimeoutError(
                f"{name} timed out waiting for {missing}.\n"
                f"Extension log:\n{last_content}\nProcess log:\n{output_log.read_text(errors='replace')}"
            )
        finally:
            stop_process(process)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install-dir", type=Path,
        help="Shared directory containing both original KIFACE modules (legacy mode).",
    )
    parser.add_argument(
        "--eeschema-install-dir", type=Path,
        help="Directory containing the original _eeschema.kiface.",
    )
    parser.add_argument(
        "--pcbnew-install-dir", type=Path,
        help="Directory containing the original _pcbnew.kiface.",
    )
    parser.add_argument("--proxy-dir", type=Path, required=True,
                        help="Directory containing production _eeschema.kiface and _pcbnew.kiface.")
    parser.add_argument("--smoke-extension", type=Path, required=True,
                        help="Build of kua_real_smoke_extension.")
    parser.add_argument("--eeschema", type=Path, required=True, help="Matching Eeschema executable.")
    parser.add_argument("--pcbnew", type=Path, required=True, help="Matching PCB Editor executable.")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--allow-existing-extensions", action="store_true",
                        help="Allow other shared libraries in the KUA extension directories.")
    parser.add_argument("--eeschema-arg", action="append", default=[])
    parser.add_argument("--pcbnew-arg", action="append", default=[])
    args = parser.parse_args()

    if not sys.platform.startswith("linux"):
        parser.error("The live smoke runner currently supports Linux only.")
    if not os.environ.get("DISPLAY"):
        parser.error("A live X11 display is required; use xvfb-run on a headless runner.")
    if args.timeout <= 0:
        parser.error("--timeout must be positive.")

    if args.install_dir and (args.eeschema_install_dir or args.pcbnew_install_dir):
        parser.error("Use either --install-dir or the two editor-specific install directories, not both.")
    if args.install_dir:
        eeschema_install_dir = pcbnew_install_dir = args.install_dir.resolve()
    elif args.eeschema_install_dir and args.pcbnew_install_dir:
        eeschema_install_dir = args.eeschema_install_dir.resolve()
        pcbnew_install_dir = args.pcbnew_install_dir.resolve()
    else:
        parser.error("Supply --install-dir or both --eeschema-install-dir and --pcbnew-install-dir.")

    proxy_dir = args.proxy_dir.resolve()
    extension = args.smoke_extension.resolve()
    eeschema = args.eeschema.resolve()
    pcbnew = args.pcbnew.resolve()
    install_specs = [
        ("_eeschema", eeschema_install_dir, eeschema, 1, args.eeschema_arg, "eeschema"),
        ("_pcbnew", pcbnew_install_dir, pcbnew, 2, args.pcbnew_arg, "pcbnew"),
    ]

    for module, install_root, executable, _, _, _ in install_specs:
        required = (
            install_root / f"{module}.kiface",
            proxy_dir / f"{module}.kiface",
            executable,
        )
        for path in required:
            if not path.is_file():
                parser.error(f"Required file does not exist: {path}")
        manifest = install_root / modloader.MANIFEST_NAME
        if manifest.exists():
            parser.error(f"Refusing to run over an existing managed install: {manifest}")

    if not extension.is_file():
        parser.error(f"Smoke extension does not exist: {extension}")

    extension_dirs = {
        install_root / "kicad-unstable-api" / "extensions"
        for _, install_root, _, _, _, _ in install_specs
    }
    for extension_dir in extension_dirs:
        existing = [p for p in extension_dir.glob("*") if p.is_file()] if extension_dir.exists() else []
        if existing and not args.allow_existing_extensions:
            parser.error(
                f"{extension_dir} already contains extensions. Use a clean test installation, "
                "or pass --allow-existing-extensions after reviewing those modules."
            )

    test_extension_name = f"kua_real_smoke_{uuid.uuid4().hex}.so"
    test_extensions: list[Path] = []
    created_extension_dirs: list[Path] = []
    installed_dirs: list[Path] = []
    temp_root = Path(tempfile.mkdtemp(prefix="kua-real-smoke-"))
    succeeded = False
    try:
        for extension_dir in extension_dirs:
            existed = extension_dir.exists()
            extension_dir.mkdir(parents=True, exist_ok=True)
            if not existed:
                created_extension_dirs.append(extension_dir)
            test_extension = extension_dir / test_extension_name
            shutil.copy2(extension, test_extension)
            test_extensions.append(test_extension)

        # Install one KIFACE module in each editor directory. This also supports
        # KiCad's own build tree, where Eeschema and PCB Editor have separate
        # module/executable directories.
        for module, install_root, _, _, _, _ in install_specs:
            result = modloader.install(
                install_root, proxy_dir, modules=[module], suffix=".kiface"
            )
            if result.get("state") == "installed":
                installed_dirs.append(install_root)

        for module, _, executable, editor_kind, arguments, label in install_specs:
            run_editor(
                label, editor_kind, executable, arguments,
                temp_root / f"{label}-extension.log",
                temp_root / f"{label}-process.log",
                args.timeout,
            )

        succeeded = True
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError, modloader.ModloaderError) as error:
        succeeded = False
        print(f"REAL KICAD SMOKE TEST FAILED: {error}", file=sys.stderr)
        print(f"Diagnostic logs retained at: {temp_root}", file=sys.stderr)
    finally:
        # Restore each editor's original module independently; never discard a
        # backup if restoration fails.
        for install_root in reversed(installed_dirs):
            try:
                restore = modloader.uninstall(install_root)
                print(f"Restored original KiCad modules: {restore['restored']}")
            except Exception as error:
                succeeded = False
                print(
                    f"CRITICAL: proxy uninstall failed in {install_root}; "
                    f"backups and manifest were retained: {error}",
                    file=sys.stderr,
                )

        for test_extension in test_extensions:
            test_extension.unlink(missing_ok=True)
        for extension_dir in reversed(created_extension_dirs):
            try:
                extension_dir.rmdir()
                extension_dir.parent.rmdir()
            except OSError:
                pass

    if succeeded:
        shutil.rmtree(temp_root, ignore_errors=True)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
