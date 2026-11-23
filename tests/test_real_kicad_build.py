from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]
SPEC = importlib.util.spec_from_file_location(
    "kua_build_real_kicad_proxy", ROOT / "tools" / "build_real_kicad_proxy.py"
)
assert SPEC and SPEC.loader
build_proxy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_proxy)


class RealKiCadBuildToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.build_dir = self.root / "build"
        self.source_dir = self.root / "kicad"
        self.build_dir.mkdir()
        self.source_dir.mkdir()
        self.source_file = self.source_dir / "eeschema" / "eeschema.cpp"
        self.source_file.parent.mkdir()
        self.source_file.write_text("// compile database fixture\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_extracts_real_compile_flags_without_object_outputs(self) -> None:
        entry = {
            "directory": str(self.build_dir),
            "file": str(self.source_file),
            "arguments": [
                "/usr/bin/c++",
                "-std=gnu++20",
                "-I/source/include",
                "-I/generated/include",
                "-DKICAD_TEST_FLAG=1",
                "-c",
                str(self.source_file),
                "-o",
                "CMakeFiles/eeschema.dir/eeschema.cpp.o",
                "-MD",
                "-MF",
                "CMakeFiles/eeschema.dir/eeschema.cpp.o.d",
                "-MT",
                "CMakeFiles/eeschema.dir/eeschema.cpp.o",
            ],
        }
        compiler, flags = build_proxy.compiler_and_flags(entry)

        self.assertEqual(compiler, "/usr/bin/c++")
        self.assertIn("-std=gnu++20", flags)
        self.assertIn("-I/source/include", flags)
        self.assertIn("-I/generated/include", flags)
        self.assertIn("-DKICAD_TEST_FLAG=1", flags)
        self.assertNotIn("-c", flags)
        self.assertNotIn(str(self.source_file), flags)
        self.assertFalse(any(flag.startswith("-o") for flag in flags))
        self.assertFalse(any(flag.startswith("-MF") for flag in flags))
        self.assertFalse(any(flag.startswith("-MT") for flag in flags))

    def test_build_command_targets_each_editor_and_embeds_source_tag(self) -> None:
        entry = {
            "directory": str(self.build_dir),
            "file": str(self.source_file),
            "arguments": ["/usr/bin/c++", "-std=c++20", "-I/kicad/include",
                          "-c", str(self.source_file), "-o", "eeschema.o"],
        }
        proxy_source = ROOT / "src" / "kiface_proxy.cpp"
        output = self.root / "_pcbnew.kiface"
        command = build_proxy.make_link_command(
            entry, proxy_source, output, "pcbnew", "b7066f886375b186ca8fa469de3a6382f6bba400",
            self.root / "libkigal.so.9.0.0",
        )

        self.assertEqual(command[0], "/usr/bin/c++")
        self.assertIn("-std=c++20", command)
        self.assertIn("-fPIC", command)
        self.assertIn('-DKUA_KICAD_ABI_TAG="b7066f886375b186ca8fa469de3a6382f6bba400"', command)
        self.assertIn("-DKUA_BUILD_EDITOR_PCB", command)
        self.assertIn("-shared", command)
        self.assertIn(str(proxy_source), command)
        self.assertIn(str(self.root / "libkigal.so.9.0.0"), command)
        self.assertIn("-Wl,-rpath,$ORIGIN/../common/gal", command)
        self.assertIn("-ldl", command)
        self.assertEqual(command[-1], str(output))

    def test_resolves_one_compile_command_for_source_file(self) -> None:
        entry = {"file": str(self.source_file), "directory": str(self.build_dir)}
        result = build_proxy.resolve_entry([entry], "eeschema/eeschema.cpp")
        self.assertEqual(result, entry)

    def test_rejects_ambiguous_compile_database_entries(self) -> None:
        entry = {"file": str(self.source_file), "directory": str(self.build_dir)}
        with self.assertRaisesRegex(ValueError, "found 2"):
            build_proxy.resolve_entry([entry, entry], "eeschema/eeschema.cpp")


if __name__ == "__main__":
    unittest.main()
