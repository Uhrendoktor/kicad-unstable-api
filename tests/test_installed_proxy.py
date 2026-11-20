from __future__ import annotations

import argparse
import hashlib
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).parents[1]
SPEC = importlib.util.spec_from_file_location("kua_modloader", ROOT / "tools" / "modloader.py")
assert SPEC and SPEC.loader
modloader = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = modloader
SPEC.loader.exec_module(modloader)


class InstalledProxyWorkflowTests(unittest.TestCase):
    proxy: Path
    original: Path
    extension: Path
    test_host: Path
    suffix: str

    def setUp(self) -> None:
        required = ("proxy", "original", "extension", "test_host", "suffix")
        if not all(getattr(type(self), name, None) for name in required):
            self.skipTest("native artifacts were not provided by the CMake test harness")
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.install_dir = self.root / "fake-kicad" / "PlugIns"
        self.proxy_dir = self.root / "proxy-payload"
        extension_dir = self.install_dir / "clippy-native" / "extensions"
        self.install_dir.mkdir(parents=True)
        self.proxy_dir.mkdir()
        extension_dir.mkdir(parents=True)
        shutil.copy2(self.original, self.install_dir / f"_eeschema{self.suffix}")
        shutil.copy2(self.proxy, self.proxy_dir / f"_eeschema{self.suffix}")
        shutil.copy2(self.extension, extension_dir / self.extension.name)
        self.installed_proxy = self.install_dir / f"_eeschema{self.suffix}"
        self.installed_extension = extension_dir / self.extension.name

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_python_installer_then_real_dynamic_proxy_load_then_restore(self) -> None:
        original_hash = hashlib.sha256(self.original.read_bytes()).hexdigest()
        result = modloader.install(
            self.install_dir, self.proxy_dir, modules=["_eeschema"], suffix=self.suffix
        )
        self.assertEqual(result["state"], "installed")
        self.assertTrue((self.install_dir / f"_eeschema.original{self.suffix}").is_file())

        completed = subprocess.run(
            [str(self.test_host), str(self.installed_proxy), str(self.installed_extension)],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

        modloader.uninstall(self.install_dir)
        restored_hash = hashlib.sha256(self.installed_proxy.read_bytes()).hexdigest()
        self.assertEqual(restored_hash, original_hash)
        self.assertFalse((self.install_dir / f"_eeschema.original{self.suffix}").exists())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--proxy", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--test-host", type=Path, required=True)
    parser.add_argument("--suffix", required=True)
    args = parser.parse_args()

    InstalledProxyWorkflowTests.proxy = args.proxy
    InstalledProxyWorkflowTests.original = args.original
    InstalledProxyWorkflowTests.extension = args.extension
    InstalledProxyWorkflowTests.test_host = args.test_host
    InstalledProxyWorkflowTests.suffix = args.suffix
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(InstalledProxyWorkflowTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
