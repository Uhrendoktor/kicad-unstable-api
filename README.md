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

The cross-platform test target remains a synthetic KIFACE host and original module. A separate Linux CI job now configures the pinned KiCad source tree and compiles both production proxy faces with the real Eeschema/PCB Editor compile flags, generated headers and wxWidgets configuration from KiCad's `compile_commands.json`. This catches production-header signature and include errors, but it does not build or launch KiCad itself.

The `tools/smoke_real_kicad.py` runner can be used on a disposable Linux KiCad installation built from the same source/toolchain. It temporarily installs the proxies, launches both editor binaries, checks that the extension receives non-null frame/canvas/view handles, exercises GAL overlay create/style/line/circle/rectangle operations, and restores the original modules. The automated workflow does not run this GUI smoke test; the running binary must be the exact build corresponding to the compile database, and pixel-level rendering, pass-through input, macOS Gatekeeper and Windows signature policy still require platform integration testing.

## Repository map

- docs/research.md — KiCad loader, exported symbols, and C++ API boundaries.
- docs/use-case-matrix.md — requirements derived from kicad-blocks, kicad-jlcpcb-interface and kicad-clippy.
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
    python tools/build_real_kicad_proxy.py \
      --kicad-source /path/to/kicad \
      --kicad-build /path/to/kicad-build \
      --output-dir build/real-kicad

Build the probe extension:

    cmake -S . -B build
    cmake --build build --target kua_real_smoke_extension

On a disposable Linux KiCad installation built from the same source commit and toolchain, run the reversible live smoke test under X11 (or `xvfb-run`):

    xvfb-run -a python tools/smoke_real_kicad.py \
      --install-dir /path/to/kicad/bin \
      --proxy-dir build/real-kicad \
      --smoke-extension build/kua_real_smoke_extension.so \
      --eeschema /path/to/eeschema \
      --pcbnew /path/to/pcbnew

The smoke runner refuses an existing managed install or an existing extension directory unless explicitly allowed. It backs up both original KIFACE modules and restores them after the run. Do not run it against a production installation.

## Status

Prototype stage. The synthetic three-platform loader/installer matrix and the pinned Linux production-header compile are CI checked. The live-editor runner is available but still requires execution against the exact built KiCad binaries on a disposable GUI-capable installation. The proxy disables queued host-to-extension GUI callbacks before invoking extension shutdown; native extension modules remain mapped until process exit to avoid dangling vtables and RTTI references. Do not install into a production KiCad installation.
