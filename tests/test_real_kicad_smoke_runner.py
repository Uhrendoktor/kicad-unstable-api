from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]
SCRIPT = ROOT / "tools" / "smoke_real_kicad.py"


class RealKiCadSmokeRunnerTests(unittest.TestCase):
    def test_help_is_available_without_editor_installation(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--help"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--eeschema-install-dir", result.stdout)
        self.assertIn("--pcbnew-install-dir", result.stdout)
        self.assertIn("--proxy-dir", result.stdout)
        self.assertIn("--diagnostics-dir", result.stdout)


if __name__ == "__main__":
    unittest.main()
