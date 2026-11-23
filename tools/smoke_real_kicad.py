#!/usr/bin/env python3
"""Reversible live-editor smoke test for a matching Linux KiCad installation.

The script temporarily installs both proxies, places a single probe extension
beside them, starts Eeschema and PCB Editor, verifies real frame/canvas/view and
GAL-overlay callbacks, and restores the original modules in a finally block.
Use only on a disposable test installation or integration runner.
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
    parser.add_argument("--install-dir", type=Path, required=True,
                        help="Directory containing _eeschema.kiface and _pcbnew.kiface.")
    parser.add_argument("--proxy-dir", type=Path, required=True,
                        help="Directory containing production _eeschema.kiface and _pcbnew.kiface.")
    parser.add_argument("--smoke-extension", type=Path, required=True,
                        help="Build of kua_real_smoke_extension.")
    parser.add_argument("--eeschema", type=Path, required=True, help="Matching Eeschema executable.")
    parser.add_argument("--pcbnew", type=Path, required=True, help="Matching PCB Editor executable.")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--allow-existing-extensions", action="store_true",
                        help="Allow other shared libraries in kicad-unstable-api/extensions.")
    parser.add_argument("--eeschema-arg", action="append", default=[])
    parser.add_argument("--pcbnew-arg", action="append", default=[])
    args = parser.parse_args()

    if not sys.platform.startswith("linux"):
        parser.error("The live smoke runner currently supports Linux only.")
    if not os.environ.get("DISPLAY"):
        parser.error("A live X11 display is required; use xvfb-run on a headless runner.")
    if args.timeout <= 0:
        parser.error("--timeout must be positive.")

    install_dir = args.install_dir.resolve()
    proxy_dir = args.proxy_dir.resolve()
    extension = args.smoke_extension.resolve()
    eeschema = args.eeschema.resolve()
    pcbnew = args.pcbnew.resolve()
    for required in (
        install_dir / "_eeschema.kiface", install_dir / "_pcbnew.kiface",
        proxy_dir / "_eeschema.kiface", proxy_dir / "_pcbnew.kiface",
        extension, eeschema, pcbnew,
    ):
        if not required.is_file():
            parser.error(f"Required file does not exist: {required}")

    manifest = install_dir / modloader.MANIFEST_NAME
    if manifest.exists():
        parser.error(f"Refusing to run over an existing managed install: {manifest}")

    extension_dir = install_dir / "kicad-unstable-api" / "extensions"
    existing = [p for p in extension_dir.glob("*") if p.is_file()] if extension_dir.exists() else []
    if existing and not args.allow_existing_extensions:
        parser.error(
            f"{extension_dir} already contains extensions. Use a clean test installation, "
            "or pass --allow-existing-extensions after reviewing those modules."
        )

    test_extension = extension_dir / f"kua_real_smoke_{uuid.uuid4().hex}.so"
    extension_dir.mkdir(parents=True, exist_ok=True)
    temp_root = Path(tempfile.mkdtemp(prefix="kua-real-smoke-"))
    installed = False
    succeeded = False
    try:
        shutil.copy2(extension, test_extension)
        result = modloader.install(install_dir, proxy_dir, suffix=".kiface")
        installed = result.get("state") == "installed"

        run_editor(
            "Eeschema", 1, eeschema, args.eeschema_arg,
            temp_root / "eeschema-extension.log", temp_root / "eeschema-process.log",
            args.timeout,
        )
        run_editor(
            "PCB Editor", 2, pcbnew, args.pcbnew_arg,
            temp_root / "pcbnew-extension.log", temp_root / "pcbnew-process.log",
            args.timeout,
        )
        succeeded = True
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError, modloader.ModloaderError) as error:
        print(f"REAL KICAD SMOKE TEST FAILED: {error}", file=sys.stderr)
        print(f"Diagnostic logs retained at: {temp_root}", file=sys.stderr)
    finally:
        if installed:
            try:
                restore = modloader.uninstall(install_dir)
                print(f"Restored original KiCad modules: {restore['restored']}")
            except Exception as error:
                succeeded = False
                print(
                    f"CRITICAL: proxy uninstall failed; backups and manifest were retained: {error}",
                    file=sys.stderr,
                )
        test_extension.unlink(missing_ok=True)
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
