# KiCad Unstable API

A research prototype for a private, cross-platform native extension runtime for stock KiCad installations.

The goal is to extend KiCad's IPC surface with bindings to existing native KiCad and wxWidgets behavior that plugins need but cannot currently call: GAL overlays, native tool/move actions, wxWindow/dialog creation and ownership, menu insertion, AUI docking, notebook-page insertion, property-grid operations, selection and document/object access. Plugins such as Blocks, JLCPCB Interface and Clippy keep their own UI, workflows, data models and business logic; KUA exposes native calls only.

## Proposed bootstrap

Proxy the editor KIFACE modules that KiCad already loads:

- Eeschema: the platform-specific _eeschema KIFACE module.
- PCB Editor: the platform-specific _pcbnew KIFACE module.

KiCad's existing KIWAY loader loads the editor module, resolves KIFACE_1, calls OnKifaceStart(), and later calls CreateKiWindow(). A proxy can load the preserved original module, return a forwarding KIFACE object, and load our independent native extension modules at the editor lifecycle boundary.

This requires no custom KiCad executable, no special command-line arguments, no LD_PRELOAD, and no runtime process injection. Users start KiCad normally. Installation does modify the installed KIFACE modules and is therefore experimental and reversible.

## Important limits

This is not a stable KiCad C++ ABI. KIFACE's virtual interface and C++ object layouts can change. Public headers are not proof that all implementations are exported from KiCad's shared libraries. The prototype currently uses a synthetic KIFACE test double for loader tests; production editor adapters must be built against the exact KiCad headers and binary ABI.

The cross-platform test target remains a synthetic KIFACE host and original module. A separate Linux CI job configures the pinned KiCad source tree, builds its shared GAL library, and compiles both production proxy faces with the real Eeschema/PCB Editor compile flags and generated headers from KiCad's `compile_commands.json`. The proxies link `libkigal` explicitly and carry a build-tree-relative RUNPATH. This is required because KiCad loads the proxy with eager symbol resolution before the preserved original KIFACE is loaded; the editor launcher itself does not load the GAL shared library.

Both matching Eeschema and PCB Editor executables are built and run under Xvfb in the pinned Linux CI job. The latest run (GitHub Actions run [45190113894](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45190113894), commit `17a06b86`) passed for both editors: each initialized the extension, supplied non-null frame/canvas/view handles, and successfully created and exercised a GAL overlay using style, line, circle and rectangle calls. The runner then restored the original Eeschema and PCB Editor KIFACE modules. This proves live API invocation in the pinned build, not pixel-level visual correctness or mouse-event pass-through; those remain separate gates.

## Repository map

- docs/research.md — KiCad loader, exported symbols, and C++ API boundaries.
- docs/use-case-matrix.md — requirements derived from kicad-blocks, kicad-jlcpcb-interface and kicad-clippy.
- docs/api-capability-catalog.md — native IPC binding catalogue for windows, menus, AUI docking, notebooks, Properties grids, objects and tools; plugin behavior stays in plugins.
- docs/platform-loader.md — platform mechanics, installation, recovery and validation plan.
- tests/native/synthetic_kiface.hpp — synthetic KIFACE ABI test double; not a KiCad header projection.
- include/kicad_unstable_api/extension_api.h — C ABI for privately maintained native extension modules.
- src/kiface_proxy.cpp — cross-platform proxy KIFACE implementation.
- tools/modloader.py — reversible install/uninstall prototype.
- tools/build_real_kicad_proxy.py — production proxy compilation using an exact KiCad build's compilation database.
- tools/smoke_real_kicad.py — reversible Linux Eeschema/PCB Editor and GAL API smoke test.
- docs/implementation-plan.md — capability API, project acceptance tests, platform matrix and delivery gates.
- tests/ and .github/workflows/native-loader.yml — synthetic host tests on Windows/macOS/Linux and a pinned real-header Linux compile job.

## Build and test

Requires CMake 3.20+ and a C++17 compiler.

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Installer and proof-tool tests:

    python -m unittest discover -s tests -p "test_*.py" -v

## Production KiCad build and runtime smoke test

Configure the exact KiCad source checkout used to build the target editor binaries with CMake/Ninja and export its compilation database. The production proxy builder requires the source tree and that build directory:

    cmake -S /path/to/kicad -B /path/to/kicad-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DKICAD_USE_PCH=OFF
    cmake --build /path/to/kicad-build --target gal
    python tools/build_real_kicad_proxy.py \
      --kicad-source /path/to/kicad \
      --kicad-build /path/to/kicad-build \
      --output-dir build/real-kicad

Build the probe extension:

    cmake -S . -B build
    cmake --build build --target kua_real_smoke_extension

On a disposable Linux KiCad build tree, the Eeschema and PCB Editor KIFACE modules and executables are in separate directories. Run the reversible smoke test under X11 (or `xvfb-run`):

    xvfb-run -a python tools/smoke_real_kicad.py \
      --eeschema-install-dir /path/to/kicad-build/eeschema \
      --pcbnew-install-dir /path/to/kicad-build/pcbnew \
      --proxy-dir build/real-kicad \
      --smoke-extension build/kua_real_smoke_extension.so \
      --eeschema /path/to/kicad-build/eeschema/eeschema \
      --pcbnew /path/to/kicad-build/pcbnew/pcbnew

The smoke runner refuses an existing managed install or an existing extension directory unless explicitly allowed. It backs up both original KIFACE modules and restores them after the run. Do not run it against a production installation.

## Status

Prototype stage. The synthetic loader/installer matrix passes on Windows, macOS and Linux. The pinned Linux production build compiles both real KIFACE proxies against KiCad headers and `libkigal`; the live Xvfb smoke passes in Eeschema and PCB Editor (run [45190113894](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45190113894), tested commit `17a06b86`). Each editor initialized the extension, exposed its frame/canvas/view, and accepted GAL overlay API operations; the original KIFACE modules were restored. Pixel-level rendering, input pass-through, Windows production compilation/signature policy and macOS production builds/Gatekeeper remain separate integration gates. The proxy disables queued host-to-extension GUI callbacks before invoking extension shutdown; native extension modules remain mapped until process exit to avoid dangling vtables and RTTI references. Do not install into a production KiCad installation.
