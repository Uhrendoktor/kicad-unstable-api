# Native extension implementation plan

This document turns the use-case matrix into an implementation and verification plan. It is intentionally explicit about what the current prototype proves and what still requires a real KiCad GUI.

## Design rule

KUA extends KiCad's IPC surface with bindings to existing native KiCad and wxWidgets behavior. The per-build C++ adapter maps IPC calls to native methods/events. Plugins retain their own UI composition, workflows, data models, metadata, persistence and business logic. KUA provides transport, conversion, safe handles, GUI-thread dispatch and version-specific native invocation.

```text
KiCad starts normally
  -> KIWAY loads _eeschema / _pcbnew
    -> KIFACE proxy loads the preserved original module
    -> proxy forwards the complete KIFACE interface
    -> native host discovers extensions and attaches to editor windows
       -> per-build KiCad adapter (C++ / wxWidgets / GAL / tools)
       -> stable KUA C ABI (extension-facing)
       -> extensions: Blocks | JLCPCB | Clippy
          -> existing Python/Rust application services over IPC or local RPC
```

Do not make every extension depend directly on KiCad's C++ ABI. Only the per-build adapter should do that. Expose capability-oriented operations and opaque handles through KUA.

## Native KiCad behavior required by the three projects

| Native behavior | Binding required in KUA | Blocks caller | JLCPCB caller | Clippy caller |
|---|---|---|---|---|
| Editor lifecycle | Start/end/reset, frame-created/closed, active document and project change | Required | Required | Required |
| Document identity | Editor kind, project/document ID, schematic sheet path or board UUID | Hierarchical block instance identity | Resolve symbol instances across sheets | Diagnostic scope and stable targets |
| Selection | Observe/query/select by KiCad UUID plus sheet path | Select block instances and interface pins | Select linked components | Select finding targets and related items |
| Navigation | Focus editor, sheet/board, zoom-to-object, cross-probe | Open implementation/sheet | Find catalogue-linked component | Navigate finding and related geometry |
| GAL overlays | Transient view-owned graphics, z-order, visibility, update/remove | Block boundaries, pin/interface previews | Optional component/catalogue hints | Primary diagnostic rendering |
| Input/tool routing | Passive observation, hover, selective event consumption, keyboard focus | Placement affordances | Optional inline add/replace affordances | Hover explanations and clickable fix markers |
| Native placement | Create object, start interactive move, snap, commit/cancel | Place facade and PCB layout | Add part at cursor | Proposed move/geometry fixes |
| Transactions | Undoable schematic/board commits and rollback | Instance changes and placement | Add/update catalogue fields | Apply fixes as one transaction |
| Docking | wxAuiManager GetManager/AddPane/DetachPane/Update/perspective | Plugin-owned panes | Plugin-owned panes | Plugin-owned panes |
| Native menus | wxMenuBar/wxMenu insertion and command events | Plugin menu entries | Plugin menu entries | Plugin menu entries |
| Properties | wxPropertyGrid rows and wxNotebook pages in existing dialogs | Plugin-owned rows/pages | Plugin-owned rows/pages | Plugin-owned rows/pages |
| Windows | wxWindow/wxDialog/wxFrame create and parent | Plugin-owned windows | Plugin-owned windows | Plugin-owned windows |
| Preview | Reusable symbol/footprint/3D preview surfaces | Facade and implementation preview | Symbol, footprint, STEP/3D | Geometry and fix preview |
| Project settings | Project-local configuration and metadata persistence | Block catalogs and profiles | Supplier configuration | Rule profile and component intent |
| Background work | Thread-safe dispatch to Python/Rust; GUI calls return to main thread | Library sync/build | Pricing/stock/catalogue queries | Lint/ERC/DRC and fix planning |

## Proposed extension-facing API groups

The C ABI should expose these groups using versioned structs, fixed-width scalars, UTF-8 strings, opaque handles and function pointers. Never pass KiCad or wxWidgets C++ objects by value across the boundary.

### 1. Host and editor context
- Host build fingerprint: KiCad version, source commit/build ID if available, platform, architecture, compiler/runtime ABI, wxWidgets version, KUA ABI.
- Editor context: schematic or PCB, frame handle, document handle, active sheet path / board ID, project path.
- Lifecycle events: editor attached/detached, document changed, selection changed, reset, shutdown.
- GUI dispatch: post callback to the wx main thread; no extension may call wxWidgets or GAL from its worker thread.

### 2. Object model and selection
- Enumerate/query supported objects with typed handles and stable KiCad UUIDs.
- Schematic identity is (document, sheet-instance path, item UUID), not UUID alone.
- PCB identity is (board/document, item UUID); footprint reference and pad number are additional semantic selectors, not primary identity.
- Selection snapshots include object kind, reference/value, position, orientation, bounding box and parent/related handles where available.
- Subscribe to selection changes and request select/focus/zoom operations.

### 3. Transient overlay service
- Create/update/remove overlay groups that belong to the editor view, never the schematic/board document.
- Primitives: point, line, polyline, rectangle, rounded rectangle, circle, arc, polygon, text and object outline.
- Coordinate spaces: schematic/board millimetres and view/screen coordinates; conversions must be performed by the active KiCad view.
- Attributes: severity/style, z-order, visibility, document scope, expiry and owning extension ID.
- Modes: display-only/pass-through; passive with independent hover testing; interactive hit targets.
- Overlay update is batched and invalidated on zoom/pan/selection/document change.
- The extension host owns VIEW_OVERLAY lifetimes and calls VIEW::Remove before releasing them. MakeOverlay already adds the overlay to the view; do not add it twice.
- Eeschema and PCB need separate layer/display-only verification.

### 3a. Implemented GAL overlay slice
- The C ABI appends opaque overlay create/destroy/clear/style/line/circle/rectangle functions; consumers check `struct_size` before accessing appended fields.
- Geometry uses KiCad world coordinates (internal units), not pixels. Unit conversion helpers are a future capability.
- All overlay operations are GUI-thread-only; extensions marshal calls through `post_to_gui`.
- The host owns `VIEW_OVERLAY` shared pointers, calls `VIEW::Remove` before releasing them, and refreshes the canvas after mutations.
- The host removes per-frame overlays on the primary frame close event and remaining overlays before KIFACE shutdown. Synthetic tests validate ABI presence and invalid-handle behavior only; they do not exercise GAL rendering or close ordering.

### 4. Interaction and tool service
- Register native tool actions and event observers per editor.
- Passive observers must not consume pointer or keyboard events.
- Hover mode performs hit testing against KUA geometry, shows a native tooltip, and lets the normal KiCad event continue.
- Interactive mode consumes only events whose hit target belongs to the active extension; it can request keyboard focus and route Enter/Escape.
- Expose cursor position, current grid/snap state, selection and current tool state.
- Never use global OS hooks or synthesize mouse/keyboard events.

### 5. Native placement and transaction service
- Schematic: create symbol in a SCH_COMMIT, set selection/reference point, hand the commit to SCH_ACTIONS::move using TOOL_MANAGER::PostAPIAction. Existing move tool owns cursor, grid/magnetic snapping, confirm, Escape and undo.
- PCB: use the editor's native interactive-move path (the IPC Board.interactive_move operation is a reference implementation); wrap model edits in BOARD_COMMIT.
- Expose begin/preview/commit/cancel, with one undo item and explicit failure results.
- Fix proposals must include document fingerprint, object UUIDs, precondition hashes and expected post-edit validation. Re-resolve UUIDs immediately before applying.

### 6. Native wxWidgets bindings: windows, menus, notebooks and AUI
- Bind wxWindow/wxDialog/wxFrame create, parent/owner, child traversal, visibility, focus, geometry, events and wxSizer composition.
- Bind wxMenuBar/wxMenu Append/Insert/Remove/Enable/Check and native event callbacks.
- Bind wxAuiManager::GetManager/AddPane/InsertPane/DetachPane/Update and perspective methods; each plugin creates/configures its own panes.
- Bind wxNotebook AddPage/InsertPage/RemovePage/DeletePage and child class inspection.
- The modal Symbol Properties and Footprint Properties dialogs already contain wxNotebook controls; plugins can append their own pages.
- Bind EDA_DRAW_FRAME::GetPropertiesPanel, PROPERTIES_PANEL::GetPropertyGrid and wxPropertyGrid methods for the persistent side panel.
- KUA does not create a shared workspace, page registry, menu manager, property-provider registry or common inspector. Plugins own their UI controls and state.

### 7. Native property/object bindings
- Expose existing KiCad symbol/footprint field accessors, wxPropertyGrid operations and native notebook methods.
- Expose native SCH_COMMIT/BOARD_COMMIT operations and object updates.
- Preserve unrelated user fields and library identity through existing native update APIs.
- Blocks/JLCPCB/Clippy define their own metadata schemas, interpretation, storage and conflict policy.

## Project-specific acceptance scenarios

### kicad-blocks
1. Open the Functional Block Library as a registered workspace page.
2. Search/select a block and show facade plus implementation preview.
3. Place a facade using the native Eeschema move tool; verify cursor-following, grid/magnetic snap, confirm, Escape rollback and one-step undo.
4. Change instance profile/variant/typed parameters through a contextual provider; verify a single undoable update.
5. Open the implementation sheet and navigate/select the corresponding instance.
6. Apply a PCB layout through the PCB editor's native move/transaction path.
7. Verify the page survives selection changes, project switches and multiple open editor frames.

### kicad-jlcpcb-interface
1. Show project-wide component catalogue/stock/cost overview in one workspace page.
2. Select a schematic symbol and display its supplier/MPN/LCSC contextual page.
3. Update catalogue fields without changing library identity, graphics, pin map or unrelated fields.
4. Add a component and hand it to native cursor-attached placement; verify snapping, Escape and undo.
5. Preview symbol, footprint and STEP/3D data in tabs; preview must not mutate the design.
6. Select a catalogue row and navigate to the exact symbol instance including sheet path.

### kicad-clippy
1. Render transient diagnostics for point, line, box, polygon, arc and object UUID targets; confirm no design file change.
2. Pan/zoom and confirm overlay geometry remains registered to document coordinates.
3. Passive overlay: click/select underlying KiCad objects normally.
4. Hover overlay: show explanation without consuming normal selection.
5. Interactive marker: click opens the corresponding finding/fix page; keyboard focus and Escape work.
6. Navigate to primary and related objects across hierarchical sheets and PCB.
7. Edit component intent in the contextual property page and persist metadata.
8. Preview a fix, apply through SCH_COMMIT/BOARD_COMMIT, undo it, and rerun lint/ERC/DRC to verify.
9. Close/reopen the project and verify overlays are reconstructed from diagnostics, not serialized into the schematic or board.

## Loader and install verification

### Current automated harness
- Synthetic original KIFACE and proxy for both Eeschema and PCB Editor, plus a shared extension module.
- OS-native module loading and symbol resolution.
- Full synthetic KIFACE virtual-method forwarding.
- Extension initialize/window/reset/shutdown callbacks.
- Python install/uninstall of both editor proxies against a temporary fake KiCad plugin directory, including real dynamic loads from the installed paths and byte-identical restore.
- Deferred install waits for host exit.

The harness is useful for loader mechanics only. It does not prove that a real KiCad frame can be safely inspected.

### Production-header compile and live-editor probe

- The `real-kicad-linux` CI job configures the pinned KiCad source revision in a Debian 13 (Trixie) environment using the upstream build dependency set. It does not build all of KiCad.
- `tools/build_real_kicad_proxy.py` selects the `eeschema/eeschema.cpp` and `pcbnew/pcbnew.cpp` entries from the exact build's `compile_commands.json`, reuses their C++20 compiler flags, generated-header paths, wxWidgets settings and target include/definition sets, and compiles `src/kiface_proxy.cpp` as two Linux KIFACE modules.
- The builder first requires KiCad's `gal` target and links the resulting `common/gal/libkigal.so` into each proxy, with a build-tree-relative `$ORIGIN/../common/gal` RUNPATH. This is required because the KiCad launcher itself links `kicommon` and wxWidgets but not `gal`, and it loads the proxy with eager resolution before the proxy can open the preserved original KIFACE. The previous proxy had unresolved `KIGFX::VIEW` / `VIEW_OVERLAY` references and no `DT_NEEDED` on `libkigal`, so its entry point could not run.
- The builder verifies `KIFACE_1` export and the `libkigal` `DT_NEEDED`/RUNPATH entries with `nm` and `readelf`.
- The module's `host_build_tag` is set to the KiCad source commit used for compilation. This is provenance metadata, not an independently verified runtime compatibility gate.
- `tests/native/test_real_smoke_extension.cpp` is a probe extension for a live editor. It records initialization, requires non-null editor frame/canvas/view handles, creates a GAL overlay, sets style, and submits line, circle and rectangle geometry.
- `tools/smoke_real_kicad.py` can run that probe against matching Linux Eeschema and PCB Editor binaries under X11/Xvfb. It installs both proxies reversibly, verifies the callbacks for each editor, terminates the test processes, and restores the original KIFACE modules.
- The Linux CI job builds the matching `eeschema` and `pcbnew` CMake targets from the same pinned KiCad source/build tree and runs the reversible smoke runner under Xvfb. The first attempt built both applications and proxies but timed out before the extension initialization callback in Eeschema; the runner restored both original KIFACE modules.
- The runner now records proxy entry, preserved-original loading, extension search path, wxWidgets trace output, and a `strace` file-open trace. Diagnostics are retained as a CI artifact after failures. The runtime gate remains unresolved until both editors emit initialize/window/overlay-ready events.
- Once it passes, the smoke proves non-null frame/canvas/view handles and successful GAL overlay create/style/line/circle/rectangle calls. It does not capture pixels or synthesize clicks, so visual rendering and mouse pass-through remain unverified.


### Required platform test matrix

| Test | Windows | macOS | Linux |
|---|---|---|---|
| Build proxy with official KiCad headers/toolchain | Manual/integration runner | Required (arm64 + x86_64 if shipped) | CI (pinned source/header compile) |
| Load proxy in synthetic host | CI | CI | CI |
| Forward original KIFACE in synthetic host | CI | CI | CI |
| Install/restore hash-verified fake modules | CI | CI | CI |
| Real KiCad Eeschema launch and close | Manual/integration runner | Manual/integration runner | CI attempted (runtime gate failing) |
| Real KiCad PCB Editor launch and close | Manual/integration runner | Manual/integration runner | CI attempted (pending after Eeschema) |
| GAL create/style/primitive API calls | GUI test | GUI test | CI attempted (no callback yet) |
| Pixel-level GAL rendering and mouse pass-through | GUI test | GUI test | GUI test |
| Native placement/snap/undo/cancel | GUI test | GUI test | GUI test |
| AUI workspace + provider switching | GUI test | GUI test | GUI test |
| Package permissions and recovery | Program Files + user install | /Applications bundle | deb/rpm + AppImage/Flatpak |
| Signature/loader policy | Verify actual vendor build | Gatekeeper, quarantine, ad-hoc signature | ELF/RPATH/package policy |

### Installer safeguards before real use
- Detect the exact KiCad installation, version, architecture and editor module filenames; do not accept a caller-supplied directory without validating it.
- Refuse unknown versions/build fingerprints and existing third-party proxy modifications.
- Stage both editor replacements and backups before replacing either file.
- Keep the original KIFACE in the same directory as the proxy to preserve relative dependency lookup.
- Wait for all KiCad processes using the installation to exit, not merely the IPC add-on process.
- Verify proxy and backup hashes after every operation; provide an explicit recovery command and retain backups until a verified successful startup.
- Elevation must be explicit and platform-native. The current helper does not implement privilege elevation.
- macOS: ad-hoc signing is not notarization; validate actual bundle/runtime library-loading policy. The current CI only tests Mach-O dynamic loading outside KiCad.app.
- Windows: inspect the actual vendor build's KIFACE signature-verification setting; do not infer it from upstream defaults.
- Linux: detect immutable AppImage/Flatpak and package-managed locations; do not patch immutable bundles.

## Current gaps in the prototype

1. The synthetic KIFACE header remains a test double. Linux CI compiles the production proxy against headers and compile flags generated by the pinned KiCad source build and links the real `libkigal` dependency; Windows and macOS production-header compiles remain outstanding.
2. Linux CI also builds the pinned Eeschema and PCB Editor executables and runs the live API smoke. The proxy is compiled using the exact KiCad editor compile flags and links only the shared GAL library it directly calls; it does not link the editor implementation. The smoke's RUNPATH is specific to the KiCad build-tree directory layout; distribution/package-specific runtime paths still need platform integration work.
3. The ABI tag is reported to extensions but is not independently verified against the running KiCad executable; do not treat it as a runtime compatibility gate.
4. The current KUA API includes initial GAL overlay operations; native window/control, menu, notebook, AUI, property-grid, object, selection and tool bindings remain to be exposed.
5. The host scans a shared extension directory but has no manifest, dependency ordering, capability negotiation or per-extension enable/disable settings yet.
6. The frame callback filters to the primary `FRAME_SCH` / `FRAME_PCB_EDITOR` window and now supplies borrowed opaque frame, canvas and GAL view handles. Canvas/view discovery uses `EDA_DRAW_FRAME::GetCanvas()` and `EDA_DRAW_PANEL_GAL::GetView()` from the exact headers; document and selection services remain unimplemented.
7. The installer has no platform privilege-elevation flow and no integration with the running IPC add-on. It must schedule after *all* host processes exit and confirm executable identity/PID to avoid PID-reuse races.
8. CI tests both synthetic editor faces and the joint install/restore workflow, compiles both production proxy faces against pinned KiCad headers and `libkigal` on Linux, and builds both pinned editor executables. Run [45190113894](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45190113894) passed the live Xvfb smoke in both editors: extension initialization, non-null frame/canvas/view handles, and successful GAL overlay API calls were recorded; the original KIFACE modules were restored.
9. The deferred-helper zombie-process wait bug is fixed; the current CI matrix exercises the helper on Windows, macOS and Linux.
10. The real-editor smoke runner is Linux-only and passes in CI against the pinned source build. It proves live initialization and GAL API calls, but not pixel-level overlay rendering or mouse pass-through. Windows production builds/signature policy and macOS production builds/Gatekeeper tests remain unvalidated.

## Delivery order

P0 — loader correctness:
1. Fix and pass the three-platform synthetic harness.
2. Test both Eeschema and PCB proxy faces.
3. Generate/check the KIFACE ABI projection against the exact KiCad header and reject unknown fingerprints.
4. Add real-install discovery, all-process shutdown wait, backup/restore and startup recovery.

P1 — first real native editor integration:
5. Build real Eeschema and PCB Editor proxies against an exact KiCad build. **Completed for the pinned Linux build.**
6. Discover frame/canvas/view and exercise transient GAL overlay operations. **Completed in both editors in the pinned Linux smoke test; pixel-level rendering is not yet verified.**
7. Prove passive pass-through, hover and one interactive tool event.
8. Bind wxWindow, wxMenuBar/wxMenu, wxAuiManager, wxNotebook and wxPropertyGrid so plugins can compose their own UI.

P2 — core workflows:
9. Implement schematic native move handoff and verify snap/undo/cancel.
10. Bind native selection/document/object access and existing Properties panel/grid methods.
11. Verify the three plugins can invoke the exposed native calls; do not port their UI/features into KUA.

P3 — production hardening:
12. Expose native commit and validation/report calls; keep fix planning and post-edit policy in Clippy.
13. Expand version/build matrix and automated real-GUI smoke tests.
14. Add platform installer UX, diagnostics, rollback and recovery documentation.
