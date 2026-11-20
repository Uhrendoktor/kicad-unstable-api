from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).parents[1] / "tools" / "modloader.py"
SPEC = importlib.util.spec_from_file_location("kua_modloader", MODULE_PATH)
assert SPEC and SPEC.loader
modloader = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = modloader
SPEC.loader.exec_module(modloader)


class ModloaderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.install_dir = self.root / "kicad" / "PlugIns"
        self.proxy_dir = self.root / "proxy"
        self.install_dir.mkdir(parents=True)
        self.proxy_dir.mkdir()
        for name in ("_eeschema", "_pcbnew"):
            (self.install_dir / f"{name}.kiface").write_bytes(f"original:{name}".encode())
            (self.proxy_dir / f"{name}.kiface").write_bytes(f"proxy:{name}".encode())

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_install_preserves_and_replaces_both_modules(self) -> None:
        result = modloader.install(self.install_dir, self.proxy_dir)
        self.assertEqual(result["state"], "installed")
        for name in ("_eeschema", "_pcbnew"):
            self.assertEqual((self.install_dir / f"{name}.kiface").read_bytes(), f"proxy:{name}".encode())
            self.assertEqual((self.install_dir / f"{name}.original.kiface").read_bytes(), f"original:{name}".encode())

    def test_uninstall_restores_originals(self) -> None:
        modloader.install(self.install_dir, self.proxy_dir)
        result = modloader.uninstall(self.install_dir)
        self.assertEqual(result["state"], "uninstalled")
        for name in ("_eeschema", "_pcbnew"):
            self.assertEqual((self.install_dir / f"{name}.kiface").read_bytes(), f"original:{name}".encode())
            self.assertFalse((self.install_dir / f"{name}.original.kiface").exists())

    def test_refuses_to_replace_existing_backup(self) -> None:
        (self.install_dir / "_eeschema.original.kiface").write_text("user data")
        with self.assertRaises(modloader.ModloaderError):
            modloader.install(self.install_dir, self.proxy_dir)

    def test_uninstall_refuses_modified_proxy(self) -> None:
        modloader.install(self.install_dir, self.proxy_dir)
        (self.install_dir / "_pcbnew.kiface").write_text("third-party replacement")
        with self.assertRaises(modloader.ModloaderError):
            modloader.uninstall(self.install_dir)
        self.assertEqual((self.install_dir / "_eeschema.original.kiface").read_bytes(), b"original:_eeschema")

    def test_install_is_idempotent_when_proxy_hashes_match(self) -> None:
        first = modloader.install(self.install_dir, self.proxy_dir)
        second = modloader.install(self.install_dir, self.proxy_dir)
        self.assertEqual(first, second)

    def test_rejects_unknown_module_names(self) -> None:
        with self.assertRaises(modloader.ModloaderError):
            modloader.install(self.install_dir, self.proxy_dir, modules=["_cvpcb"])

    def test_deferred_install_waits_for_host_exit(self) -> None:
        host = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.35)"])
        log_path = self.root / "helper.log"
        helper_pid = modloader.schedule_install(
            self.install_dir, self.proxy_dir, host.pid, log_path=log_path
        )
        host.wait(timeout=5)
        modloader.wait_for_process_exit(helper_pid)
        self.assertEqual(
            (self.install_dir / "_eeschema.kiface").read_bytes(),
            b"proxy:_eeschema",
        )
        self.assertTrue(log_path.is_file())


if __name__ == "__main__":
    unittest.main()
