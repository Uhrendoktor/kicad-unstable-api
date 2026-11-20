#!/usr/bin/env python3
"""Reversible KIFACE proxy installer prototype.

A live IPC add-on must not replace a KIFACE module that KiCad currently has
loaded. Use schedule_install/schedule_uninstall with a verified host PID. A
detached helper waits for KiCad to exit, then performs the atomic file swap.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable

MANIFEST_NAME = ".kicad-unstable-api-install.json"


class ModloaderError(RuntimeError):
    """Raised when installation safety checks fail."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".staged", dir=destination.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_stream:
            shutil.copyfileobj(input_stream, output)
            output.flush()
            os.fsync(output.fileno())
        shutil.copystat(source, temporary, follow_symlinks=True)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def _write_json_atomic(path: Path, value: dict) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".json", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _module_paths(
    install_dir: Path, proxy_dir: Path, modules: Iterable[str], suffix: str
) -> list[tuple[str, Path, Path, Path]]:
    result = []
    for module in modules:
        if module not in {"_eeschema", "_pcbnew"}:
            raise ModloaderError(f"Unsupported KIFACE module name: {module}")
        target = install_dir / f"{module}{suffix}"
        proxy = proxy_dir / f"{module}{suffix}"
        backup = install_dir / f"{module}.original{suffix}"
        result.append((module, target, proxy, backup))
    return result


def install(
    install_dir: Path,
    proxy_dir: Path,
    modules: Iterable[str] = ("_eeschema", "_pcbnew"),
    suffix: str = ".kiface",
) -> dict:
    """Install proxy modules with verified originals and rollback on failure."""
    install_dir = install_dir.resolve()
    proxy_dir = proxy_dir.resolve()
    manifest_path = install_dir / MANIFEST_NAME
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("state") == "installed":
            for item in manifest["modules"]:
                target = install_dir / item["target"]
                if not target.exists() or sha256(target) != item["proxy_sha256"]:
                    raise ModloaderError("An installation exists but a proxy hash no longer matches.")
            return manifest

    paths = _module_paths(install_dir, proxy_dir, modules, suffix)
    for module, target, proxy, backup in paths:
        if not target.is_file():
            raise ModloaderError(f"Original KiCad module does not exist: {target}")
        if not proxy.is_file():
            raise ModloaderError(f"Clippy proxy module does not exist: {proxy}")
        if backup.exists():
            raise ModloaderError(f"Refusing to overwrite existing backup: {backup}")

    records = []
    staged: list[tuple[Path, Path]] = []
    try:
        # Preserve all originals before replacing any target.
        for module, target, proxy, backup in paths:
            _atomic_copy(target, backup)
            records.append(
                {
                    "module": module,
                    "target": target.name,
                    "backup": backup.name,
                    "proxy_source": str(proxy),
                    "original_sha256": sha256(backup),
                    "proxy_sha256": sha256(proxy),
                }
            )

        # Stage every replacement before the first atomic rename.
        for (_, target, proxy, _) in paths:
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=f".{target.name}.", suffix=".proxy", dir=install_dir
            )
            os.close(descriptor)
            temporary = Path(temporary_name)
            shutil.copy2(proxy, temporary)
            staged.append((temporary, target))

        for temporary, target in staged:
            os.replace(temporary, target)

        for record in records:
            target = install_dir / record["target"]
            if sha256(target) != record["proxy_sha256"]:
                raise ModloaderError(f"Installed proxy verification failed: {target}")

        manifest = {
            "schema": 1,
            "state": "installed",
            "suffix": suffix,
            "modules": records,
        }
        _write_json_atomic(manifest_path, manifest)
        return manifest
    except Exception:
        for _, target, _, backup in paths:
            if backup.exists():
                _atomic_copy(backup, target)
        raise
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)


def uninstall(install_dir: Path) -> dict:
    """Restore verified originals; refuse to overwrite unexpected user changes."""
    install_dir = install_dir.resolve()
    manifest_path = install_dir / MANIFEST_NAME
    if not manifest_path.is_file():
        raise ModloaderError(f"No Clippy native installation manifest at {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("state") != "installed":
        raise ModloaderError("Installation manifest is not in the installed state.")

    for item in manifest.get("modules", []):
        target = install_dir / item["target"]
        backup = install_dir / item["backup"]
        if not target.is_file() or sha256(target) != item["proxy_sha256"]:
            raise ModloaderError(f"Refusing to overwrite a modified or missing proxy: {target}")
        if not backup.is_file() or sha256(backup) != item["original_sha256"]:
            raise ModloaderError(f"Original backup is missing or corrupt: {backup}")

    restored = []
    try:
        for item in manifest["modules"]:
            target = install_dir / item["target"]
            backup = install_dir / item["backup"]
            _atomic_copy(backup, target)
            if sha256(target) != item["original_sha256"]:
                raise ModloaderError(f"Restored original failed hash verification: {target}")
            restored.append(target)

        # Removing the manifest is the uninstall commit point. Keep backups if
        # cleanup subsequently fails; the installed files are already restored.
        manifest_path.unlink()
    except Exception:
        for item in manifest["modules"]:
            proxy = Path(item["proxy_source"])
            target = install_dir / item["target"]
            if proxy.is_file():
                _atomic_copy(proxy, target)
        raise

    leftovers = []
    for item in manifest["modules"]:
        backup = install_dir / item["backup"]
        try:
            backup.unlink(missing_ok=True)
        except OSError:
            leftovers.append(str(backup))

    return {
        "state": "uninstalled",
        "restored": [str(path) for path in restored],
        "backup_cleanup_pending": leftovers,
    }


def wait_for_process_exit(pid: int, poll_seconds: float = 0.25) -> None:
    """Wait for a host process to exit without injecting into or suspending it."""
    if pid <= 0:
        raise ModloaderError("Host PID must be a positive integer.")

    if os.name == "nt":
        synchronize = 0x00100000
        infinite = 0xFFFFFFFF
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel32.OpenProcess.restype = ctypes.c_void_p
        kernel32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel32.WaitForSingleObject.restype = ctypes.c_uint32
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel32.OpenProcess(synchronize, 0, pid)
        if not handle:
            error = ctypes.get_last_error()
            if error == 87:  # ERROR_INVALID_PARAMETER: PID no longer exists.
                return
            raise OSError(error, "OpenProcess(SYNCHRONIZE) failed")
        try:
            result = kernel32.WaitForSingleObject(handle, infinite)
            if result != 0:
                raise OSError(ctypes.get_last_error(), "WaitForSingleObject failed")
        finally:
            kernel32.CloseHandle(handle)
        return

    while True:
        # On POSIX, a detached helper spawned by this process can become a
        # zombie after exiting. kill(pid, 0) still succeeds for a zombie, so
        # reap our own child before falling back to the generic PID check.
        try:
            waited_pid, _ = os.waitpid(pid, os.WNOHANG)
            if waited_pid == pid:
                return
        except ChildProcessError:
            pass  # Not our child; use the portable existence check below.

        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return
        except PermissionError:
            pass  # Process exists but is owned by another user.
        time.sleep(poll_seconds)


def _spawn_deferred(command: list[str], host_pid: int, log_path: Path | None = None) -> int:
    command.extend(["--wait-for-pid", str(host_pid)])
    log_path = log_path or (Path.home() / ".kicad-unstable-api-modloader.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_stream = log_path.open("a", encoding="utf-8")
    options = {
        "stdin": subprocess.DEVNULL,
        "stdout": log_stream,
        "stderr": subprocess.STDOUT,
        "close_fds": True,
    }
    if os.name == "nt":
        options["creationflags"] = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        options["start_new_session"] = True

    try:
        process = subprocess.Popen(command, **options)
    finally:
        log_stream.close()
    return process.pid


def schedule_install(
    install_dir: Path,
    proxy_dir: Path,
    host_pid: int,
    modules: Iterable[str] = ("_eeschema", "_pcbnew"),
    suffix: str = ".kiface",
    log_path: Path | None = None,
) -> int:
    """Schedule install after the caller-verified KiCad host process exits."""
    command = [
        sys.executable, str(Path(__file__).resolve()), "install",
        "--install-dir", str(install_dir.resolve()),
        "--proxy-dir", str(proxy_dir.resolve()),
        "--suffix", suffix,
        "--modules", *modules,
    ]
    return _spawn_deferred(command, host_pid, log_path)


def schedule_uninstall(install_dir: Path, host_pid: int, log_path: Path | None = None) -> int:
    """Schedule restore after the caller-verified KiCad host process exits."""
    command = [
        sys.executable, str(Path(__file__).resolve()), "uninstall",
        "--install-dir", str(install_dir.resolve()),
    ]
    return _spawn_deferred(command, host_pid, log_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    install_parser = subparsers.add_parser("install")
    install_parser.add_argument("--install-dir", type=Path, required=True)
    install_parser.add_argument("--proxy-dir", type=Path, required=True)
    install_parser.add_argument("--suffix", default=".kiface")
    install_parser.add_argument("--modules", nargs="+", default=["_eeschema", "_pcbnew"])
    install_parser.add_argument("--wait-for-pid", type=int)
    uninstall_parser = subparsers.add_parser("uninstall")
    uninstall_parser.add_argument("--install-dir", type=Path, required=True)
    uninstall_parser.add_argument("--wait-for-pid", type=int)
    args = parser.parse_args()

    try:
        if args.wait_for_pid:
            wait_for_process_exit(args.wait_for_pid)
        if args.command == "install":
            result = install(args.install_dir, args.proxy_dir, args.modules, args.suffix)
        else:
            result = uninstall(args.install_dir)
    except (ModloaderError, OSError, ValueError, KeyError) as exc:
        print(f"modloader: {exc}", file=sys.stderr)
        return 2

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
