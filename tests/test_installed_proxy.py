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
    pcb_proxy: Path
    pcb_original: Path
    extension: Path
    test_host: Path
    suffix: str

    def setUp(self) -> None:
        required = ("proxy", "original", "pcb_proxy", "pcb_original", "extension", "test_host", "suffix")
        if not all(getattr(type(self), name, None) for name in required):
            self.skipTest("native artifacts were not provided by the CMake test harness")
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.install_dir = self.root / "fake-kicad" / "PlugIns"
        self.proxy_dir = self.root / "proxy-payload"
        extension_dir = self.install_dir / "kicad-unstable-api" / "extensions"
        self.install_dir.mkdir(parents=True)
        self.proxy_dir.mkdir()
        extension_dir.mkdir(parents=True)
        shutil.copy2(self.original, self.install_dir / f"_eeschema{self.suffix}")
        shutil.copy2(self.pcb_original, self.install_dir / f"_pcbnew{self.suffix}")
        shutil.copy2(self.proxy, self.proxy_dir / f"_eeschema{self.suffix}")
        shutil.copy2(self.pcb_proxy, self.proxy_dir / f"_pcbnew{self.suffix}")
        shutil.copy2(self.extension, extension_dir / self.extension.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _run_host(self, module: str, expected_editor_kind: int) -> None:
        installed_proxy = self.install_dir / f"{module}{self.suffix}"
        installed_extension = self.install_dir / "kicad-unstable-api" / "extensions" / self.extension.name
        completed = subprocess.run(
            [str(self.test_host), str(installed_proxy), str(installed_extension), str(expected_editor_kind)],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_install_both_editor_proxies_load_forward_and_restore(self) -> None:
        original_hashes = {
            "_eeschema": hashlib.sha256(self.original.read_bytes()).hexdigest(),
            "_pcbnew": hashlib.sha256(self.pcb_original.read_bytes()).hexdigest(),
        }
        result = modloader.install(
            self.install_dir, self.proxy_dir, modules=["_eeschema", "_pcbnew"], suffix=self.suffix
        )
        self.assertEqual(result["state"], "installed")
        for module in ("_eeschema", "_pcbnew"):
            self.assertTrue((self.install_dir / f"{module}.original{self.suffix}").is_file())

        self._run_host("_eeschema", 1)
        self._run_host("_pcbnew", 2)

        modloader.uninstall(self.install_dir)
        for module, expected_hash in original_hashes.items():
            restored = self.install_dir / f"{module}{self.suffix}"
            self.assertEqual(hashlib.sha256(restored.read_bytes()).hexdigest(), expected_hash)
            self.assertFalse((self.install_dir / f"{module}.original{self.suffix}").exists())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--proxy", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--pcb-proxy", type=Path, required=True)
    parser.add_argument("--pcb-original", type=Path, required=True)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--test-host", type=Path, required=True)
    parser.add_argument("--suffix", required=True)
    args = parser.parse_args()

    InstalledProxyWorkflowTests.proxy = args.proxy
    InstalledProxyWorkflowTests.original = args.original
    InstalledProxyWorkflowTests.pcb_proxy = args.pcb_proxy
    InstalledProxyWorkflowTests.pcb_original = args.pcb_original
    InstalledProxyWorkflowTests.extension = args.extension
    InstalledProxyWorkflowTests.test_host = args.test_host
    InstalledProxyWorkflowTests.suffix = args.suffix
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(InstalledProxyWorkflowTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
