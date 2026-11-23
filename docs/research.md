# KiCad native extension research

Research baseline: KiCad source mirror commit b7066f886375b186ca8fa469de3a6382f6bba400 (verified 2026-11-23). The complete `struct KIFACE` declaration block is byte-for-byte identical to the earlier baseline 2313f118a23a3e3bf4fd04e3625f190f4531a02d: the destructor and all 16 virtual methods remain in the same order. The CI ABI-order guard fetches this immutable header revision and verifies the repository's synthetic projection, proxy overrides, and test dispatch coverage. Re-check against the exact KiCad build before distributing a proxy.

Research baseline: KiCad source mirror commit 2313f118a23a3e3bf4fd04e3625f190f4531a02d. Re-check all details against the exact KiCad build before distributing a proxy.

## 1. The existing editor-module loader is the bootstrap

KiCad does not need a new plugin loader to enter the editor process. The project/editor launcher already loads a KIFACE module.

The relevant flow in common/kiway.cpp is:

1. KIWAY::dso_search_path() chooses the editor module path. The names are built from KIFACE_PREFIX plus eeschema or pcbnew and KIFACE_SUFFIX.
2. KIWAY::KiFACE() opens that module with wxDynamicLibrary using wxDL_VERBATIM, wxDL_NOW and wxDL_GLOBAL.
3. It resolves the exported symbol KIFACE_1.
4. It calls the getter with the KIFACE version output, KIWAY version and PGM_BASE pointer.
5. It calls KIFACE::OnKifaceStart().
6. It retains the KIFACE pointer for later calls.
7. KIWAY::Player() later calls KIFACE::CreateKiWindow() to create an editor window.

Source:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/common/kiway.cpp
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/include/kiway.h

This is a better bootstrap than the 3D plugin manager. It occurs as part of normal editor initialization and does not require opening the 3D viewer.

## 2. Both editors implement the same KIFACE entry contract

Eeschema and PCB Editor each export KIFACE_1 from their KIFACE module. Their KIFACE implementation initializes process-level editor state in OnKifaceStart() and constructs the editor frame in CreateKiWindow().

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/eeschema/eeschema.cpp
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/pcbnew/pcbnew.cpp

This gives two interception points:
- OnKifaceStart: process-level bootstrap, before the editor window is created.
- CreateKiWindow: observe each window creation and attach editor-specific services after the original editor has created the window.

OnKifaceEnd is the matching teardown point. Reset, document methods, library handlers and job handlers must also be forwarded.

## 3. KIFACE forwarding must preserve the complete vtable contract

KIFACE documents that virtual function order establishes the vtable sequence. A proxy must preserve the exact order and signatures for the supported KiCad build.

The forwarding wrapper must delegate every virtual method, not only the pure virtual methods. In particular, dropping the defaults for HandleApiOpenDocument, HandleApiCloseDocument, HandleJob, HandleJobConfig, PreloadLibraries, CancelPreload, ProjectChanged, RegisterLibraryHandlers or LoadAllLibraries would silently break services added by the real editor implementation.

The proxy must:
- load the preserved original KIFACE module by absolute path;
- resolve the original KIFACE_1 from that module handle, not from the process-global symbol table;
- call the original getter and preserve its reported KIFACE version;
- return a forwarding KIFACE object;
- retain the original module handle for the entire editor lifetime;
- call the original implementation for every interface method;
- call extension teardown before the original KIFACE is shut down.

The ABI projection in this repository deliberately contains the KIFACE method declarations and forward declarations for referenced types. It is only a test contract. Production builds must be generated or checked against the exact matching KiCad include/kiway.h, and the virtual order must be diff-checked whenever KiCad changes.

## 4. What "access to public KiCad C++" actually means

Loading code inside KiCad's address space is necessary, but it does not make every C++ symbol available.

### Explicitly exported shared-library APIs

The GAL target is built as shared library kigal. Its public rendering types use GAL_API. In particular:
- KIGFX::VIEW is declared with GAL_API.
- KIGFX::VIEW_OVERLAY is declared with GAL_API.
- VIEW exposes Add(), Remove(), MakeOverlay(), SetLayerDisplayOnly(), and view update methods.
- VIEW_OVERLAY exposes line, circle, polygon and text drawing primitives.

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/common/gal/CMakeLists.txt
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/include/view/view.h
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/include/view/view_overlay.h

### Hidden symbols and inlined accessors

KiCad sets CXX_VISIBILITY_PRESET to hidden and enables hidden inline visibility. kicommon also explicitly uses hidden visibility. Consequently, many public-looking C++ declarations are not dynamically linkable from an external module.

Source:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/CMakeLists.txt
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/common/CMakeLists.txt

A module can still use:
- explicitly exported APIs such as GAL_API and KICOMMON_API;
- inline accessors compiled into the extension against matching headers;
- documented KIFACE service pointers returned by IfaceOrAddress();
- runtime-registered tool/action interfaces where the editor exposes them.

It cannot safely assume that a hidden editor method becomes linkable merely because its header is installed.

### Editor canvas access

EDA_DRAW_FRAME has an inline GetCanvas() accessor. EDA_DRAW_PANEL_GAL has an inline GetView() accessor. The editor-specific canvas classes override GetView() to return SCH_VIEW or PCB_VIEW.

A KIFACE wrapper observes the wxWindow returned by CreateKiWindow(). An adapter can identify the main editor frame using the editor face and class ID, then use the matching headers and inline accessors to obtain its canvas/view. This requires exact-build testing: editor frame classes and some canvas APIs are not a stable exported plugin ABI.

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/include/eda_draw_frame.h
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/include/class_draw_panel_gal.h
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/eeschema/sch_draw_panel.h
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/pcbnew/pcb_draw_panel_gal.h

## 5. Native UI and interaction surfaces

### Transient GAL overlays

Use VIEW::MakeOverlay() and VIEW_OVERLAY primitives for diagnostic paint. Keep overlay instances in the editor process, never in the document object graph. A view layer can be configured display-only so normal canvas hit testing does not select the overlay. Hover and interactive hit testing should be an independent tool/input layer.

### Tool dispatch and native placement

TOOL_MANAGER exposes PostAPIAction() and GetView(). Eeschema already has SCH_ACTIONS::move and the native schematic move tool. PCB Editor already exposes Board.interactive_move() through IPC.

The native Clippy adapter should hand a newly created symbol and its active commit to the existing schematic move tool. This keeps grid snapping, magnetic snapping, cursor ownership, confirm/cancel and undo semantics inside KiCad. Do not emulate placement with OS mouse events.

The exact action lookup, commit handoff and editor busy/transaction semantics must be verified against the target KiCad build. kicad-blocks has a detailed proposal for the missing Eeschema IPC analogue:
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/upstream/kicad-schematic-interactive-move-proposal.md

### Dockable panels

EDA_BASE_FRAME owns the editor's wxAuiManager. PCB's edit frame exposes GetAuiManager(); the base frame has an AUI manager member. wxAuiManager can also be queried from managed windows. A native integration can host a single Clippy workspace pane and register pages from Blocks, JLCPCB and Clippy.

This is not the same as adding a tab inside KiCad's existing Symbol Properties dialog. No general third-party property-page registration API was found. The practical first implementation is a shared dockable inspector/workspace with tab providers, plus an object-selection context passed to each provider.

### Contextual property tabs

Implement a provider registry:
- one shared native pane/tab strip;
- pages registered by plugin ID;
- selected-item context containing editor kind, document identity, item UUID, sheet path or board reference;
- provider lifecycle callbacks when the selection changes;
- plugin-owned controls and validation, but shared layout, tab navigation, theme, docking and focus behavior.

This accommodates component intent, supplier metadata, block instance configuration and lint remediation without each plugin building a separate top-level window.

## 6. Platform-specific loader facts

The KIFACE path is platform-specific but selected by KiCad itself:
- Windows uses the KIFACE DLL suffix.
- Linux uses the KIFACE module suffix and loads through the ELF dynamic loader.
- macOS places KIFACE modules in KiCad.app/Contents/PlugIns.

The Windows KIFACE loader has an optional KICAD_WIN32_VERIFY_CODESIGN build flag. Current upstream CMake declares it OFF by default. This must be checked against the actual distributed build; do not assume every vendor build uses the default.

macOS bundle modification invalidates the existing application signature. A local user can ad-hoc re-sign the modified app bundle, but this does not restore Developer ID notarization. Gatekeeper/quarantine behavior must be documented and tested. Do not claim that an ad-hoc signature is equivalent to notarization.

Linux package formats can be read-only or managed (Flatpak, AppImage, system package). The installer should detect unwritable KIFACE directories and refuse rather than mutate an immutable package in place.

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/common/kiway.cpp
- https://github.com/KiCad/kicad-source-mirror/blob/2313f118a23a3e3bf4fd04e3625f190f4531a02d/CMakeLists.txt

## 7. Proof boundary

The CI fixture in this repository validates:
- dynamic loading of a proxy module on Windows, macOS and Linux;
- dynamic loading of the preserved original module from beside the proxy;
- resolution of the original KIFACE_1 symbol from the original module handle;
- preservation of the version output and forwarding of the full KIFACE virtual interface;
- native extension discovery and lifecycle callbacks;
- reversible installer backup and restore using synthetic installation directories.

It does not yet validate:
- ABI compatibility with an official KiCad 11 binary;
- actual editor frame discovery;
- actual GAL overlay rendering;
- live mouse/keyboard event routing;
- actual dock panes or property tabs;
- actual Eeschema cursor-attached move/commit behavior;
- macOS Gatekeeper behavior of a modified KiCad.app.

Those are explicit next-stage integration tests, not implied by the synthetic host suite.
