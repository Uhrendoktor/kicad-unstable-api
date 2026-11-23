# KIFACE proxy installation and cross-platform mechanics

## Mechanism

This prototype replaces the editor KIFACE module at the path KiCad already loads. The original module is preserved next to it with the filename:

- Windows: `_eeschema.original.dll`, `_pcbnew.original.dll`
- Linux: `_eeschema.original.kiface`, `_pcbnew.original.kiface`
- macOS: `_eeschema.original.kiface`, `_pcbnew.original.kiface`

The proxy module exports the same C entry point, `KIFACE_1`. It opens the preserved original module by absolute path, resolves that module's own `KIFACE_1`, calls the original getter and returns a forwarding KIFACE implementation to KIWAY. Every KIFACE virtual method is forwarded. The original module handle is kept alive for the process lifetime. Extension window callbacks are filtered to the primary `FRAME_SCH` or `FRAME_PCB_EDITOR`; symbol editors, viewers, choosers and dialogs are still forwarded to the original KIFACE but are not exposed as the main editor frame.

The real KiCad suffix is configured in source:
- Windows uses `CMAKE_SHARED_MODULE_SUFFIX` (normally `.dll`).
- Linux and macOS use `.kiface`.

The original module must remain in the same directory as the proxy because its dependent libraries are resolved relative to KiCad's installed layout.

## Install and uninstall from a running IPC plugin

A KIFACE module is already loaded by KiCad while the IPC Python plugin is running. Do not replace an in-use module in place.

The intended end-user flow is:
1. The Python add-on asks the native modloader to prepare an install or uninstall request.
2. A detached helper waits for the KiCad process to exit.
3. The helper atomically applies the staged replacements/restoration.
4. The user starts KiCad normally; no special launcher or startup arguments are required.

The Python helper in `tools/modloader.py` implements the closed-process atomic install/uninstall step, hash verification, rollback, and a detached process-waiting helper (`schedule_install` / `schedule_uninstall`). It is not yet wired into the live IPC plugin, and it does not verify the identity of the supplied PID or provide privilege-elevation UX. The caller must verify the PID belongs to the KiCad process and ensure every process using the installation has exited. Do not swap a loaded KIFACE module in place.

The install directory is commonly protected:
- Windows may require elevation for Program Files installations.
- Linux package-managed paths may require root and AppImage/Flatpak paths may be immutable.
- macOS /Applications may require administrator authorization.

The installer currently requires write permission. It refuses to overwrite an existing backup and verifies hashes on installation and removal.

## macOS

The proxy changes files inside `KiCad.app/Contents/PlugIns`, invalidating the original application signature. This project explicitly does not require Developer ID signing or notarization. The local installer must still deal with Gatekeeper/quarantine and may need to ad-hoc re-sign the modified bundle using `codesign --force --deep --sign -`. Ad-hoc signing is not notarization and does not restore the original trust identity. This behavior has not yet been tested against official KiCad builds.

## Windows

The official source has an optional `KICAD_WIN32_VERIFY_CODESIGN` build flag; current upstream CMake defaults it OFF. A vendor build enabling it may reject the unsigned proxy before loading it. The installer must inspect the actual build or surface the load failure and restore the original.

## Linux

The proxy is loaded with `dlopen(..., RTLD_NOW | RTLD_GLOBAL)`. The preserved original is also loaded using an absolute path and the same global visibility mode. This needs testing across distro packages, AppImage and Flatpak. Immutable package images are not supported by the in-place installer prototype.

## ABI compatibility

The forwarding class must be compiled against the exact KiCad `include/kiway.h` and matching generated headers. The KIFACE header explicitly states that virtual function order establishes the vtable sequence. A change in method order/signature or compiler ABI invalidates the proxy.

The proxy source overrides all KIFACE slots in the inspected source revision. The real-proxy CMake target includes the supplied source tree's `kiway.h`, so the compiler checks override signatures against that header. On Windows the proxy exports its own `KIFACE_1` declaration; it deliberately does not define `BUILD_KIWAY_DLL`, which would make `kiway.h` emit a second import/export-decorated declaration. This is not a generic ABI-stable loader and must be rebuilt for each supported KiCad build. A verified source/build fingerprint gate and runtime compatibility check are still required before production use.

## What CI proves

The synthetic test harness builds fake original KIFACE modules and proxies for both Eeschema and PCB Editor with the same entry point and method ordering. On Windows, macOS and Linux it tests:
- loading the proxy using the OS native dynamic loader;
- resolving and calling the original KIFACE entry point;
- forwarding every KIFACE virtual slot;
- loading a third-party extension module from the extension directory;
- extension initialize, editor-window, reset and shutdown callbacks;
- rejection of new GUI dispatch requests after extension shutdown;
- Python install/restore of both editor proxies against a temporary fake KiCad plugin directory;
- loading both installed proxies and restoring byte-identical originals.

A separate Linux CI job additionally configures the pinned KiCad source/build, compiles both production KIFACE proxies against real generated editor compile flags, builds the matching Eeschema and PCB Editor executables, and runs the reversible Xvfb smoke. The first live attempt built the applications but timed out before the Eeschema extension initialization callback. The current runner emits opt-in proxy-entry/original-module/extension-directory diagnostics and captures `strace` file-open evidence; failure artifacts are retained. Therefore real editor loading and GAL API success are not yet proven until a diagnostic run passes.

The live smoke does not provide pixel-level GAL rendering or event pass-through proof. macOS Gatekeeper and Windows vendor signature-verification builds remain untested.
