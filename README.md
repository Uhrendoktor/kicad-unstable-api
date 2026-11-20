# KiCad Unstable API

A research prototype for a private, cross-platform native extension runtime for stock KiCad installations.

The target is to let independently maintained plugins execute inside Eeschema and PCB Editor, use KiCad's in-process C++ APIs, and provide native capabilities that the IPC add-on API does not currently expose: transient GAL overlays, cursor-attached placement, editor-owned dockable panels, shared property tabs, and tool/event integration.

## Proposed bootstrap

Proxy the editor KIFACE modules that KiCad already loads:

- Eeschema: the platform-specific _eeschema KIFACE module.
- PCB Editor: the platform-specific _pcbnew KIFACE module.

KiCad's existing KIWAY loader loads the editor module, resolves KIFACE_1, calls OnKifaceStart(), and later calls CreateKiWindow(). A proxy can load the preserved original module, return a forwarding KIFACE object, and load our independent native extension modules at the editor lifecycle boundary.

This requires no custom KiCad executable, no special command-line arguments, no LD_PRELOAD, and no runtime process injection. Users start KiCad normally. Installation does modify the installed KIFACE modules and is therefore experimental and reversible.

## Important limits

This is not a stable KiCad C++ ABI. KIFACE's virtual interface and C++ object layouts can change. Public headers are not proof that all implementations are exported from KiCad's shared libraries. The prototype currently uses a synthetic KIFACE test double for loader tests; production editor adapters must be built against the exact KiCad headers and binary ABI.

The current cross-platform test target is a synthetic KIFACE host and original module. Passing CI proves dynamic module loading and forwarding on Windows, macOS and Linux; it does not prove compatibility with official KiCad binaries or successful GAL access in the real editors. The synthetic header is deliberately only a test double; the production ABI projection and exact-build gate remain to be implemented.

## Repository map

- docs/research.md — KiCad loader, exported symbols, and C++ API boundaries.
- docs/use-case-matrix.md — requirements derived from kicad-blocks, kicad-jlcpcb-interface and kicad-clippy.
- docs/platform-loader.md — platform mechanics, installation, recovery and validation plan.
- tests/native/synthetic_kiface.hpp — synthetic KIFACE ABI test double; not a KiCad header projection.
- include/kicad_unstable_api/extension_api.h — C ABI for privately maintained native extension modules.
- src/kiface_proxy.cpp — cross-platform proxy KIFACE implementation.
- tools/modloader.py — reversible install/uninstall prototype.
- docs/implementation-plan.md — capability API, project acceptance tests, platform matrix and delivery gates.\n- tests/ and .github/workflows/native-loader.yml — synthetic host tests on Windows, macOS and Linux.

## Build and test

Requires CMake 3.20+ and a C++17 compiler.

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Installer tests:

    python -m unittest discover -s tests -p "test_*.py" -v

## Status

Prototype stage. Do not install into a production KiCad installation without a complete backup and a way to restore the original KIFACE modules. The next acceptance gates are (1) a passing synthetic loader/installer matrix on all three operating systems, then (2) real KiCad 11+ editor smoke tests and a version-specific native adapter on each operating system.
