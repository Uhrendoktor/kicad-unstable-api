# Native extension implementation plan

This document turns the use-case matrix into an implementation and verification plan. It is intentionally explicit about what the current prototype proves and what still requires a real KiCad GUI.

## Design rule

Use one in-process native host per editor face, with independently versioned extension modules and a stable C ABI between the host and those modules. The KiCad-specific C++ adapter is compiled against one exact KiCad source/build fingerprint. Rust/Python application logic stays outside the native ABI and communicates through the extension's own service boundary.

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

## Shared native services required by the three projects

| Service | Shared host responsibility | Blocks | JLCPCB | Clippy |
|---|---|---|---|---|
| Editor lifecycle | Start/end/reset, frame-created/closed, active document and project change | Required | Required | Required |
| Document identity | Editor kind, project/document ID, schematic sheet path or board UUID | Hierarchical block instance identity | Resolve symbol instances across sheets | Diagnostic scope and stable targets |
| Selection | Observe/query/select by KiCad UUID plus sheet path | Select block instances and interface pins | Select linked components | Select finding targets and related items |
| Navigation | Focus editor, sheet/board, zoom-to-object, cross-probe | Open implementation/sheet | Find catalogue-linked component | Navigate finding and related geometry |
| GAL overlays | Transient view-owned graphics, z-order, visibility, update/remove | Block boundaries, pin/interface previews | Optional component/catalogue hints | Primary diagnostic rendering |
| Input/tool routing | Passive observation, hover, selective event consumption, keyboard focus | Placement affordances | Optional inline add/replace affordances | Hover explanations and clickable fix markers |
| Native placement | Create object, start interactive move, snap, commit/cancel | Place facade and PCB layout | Add part at cursor | Proposed move/geometry fixes |
| Transactions | Undoable schematic/board commits and rollback | Instance changes and placement | Add/update catalogue fields | Apply fixes as one transaction |
| Docking | One shared AUI workspace per editor | Library/instance/layout tabs | Catalogue/preview tabs | Findings/explanation/fix tabs |
| Context properties | Provider registry keyed by selection type | Block role/profile/variant/parameters | LCSC/MPN/manufacturer/package | Component intent and lint metadata |
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

### 6. Shared workspace and property-provider registry
- One host-owned wxAui pane per editor, implemented as a Clippy Workspace/Inspector.
- Host owns docking, tab strip, selection context, theme, visibility, focus and lifecycle.
- Extensions register pages with: extension ID, page ID, title, supported object kinds, selection predicate, create/update/destroy callbacks, preferred order and optional icon.
- A page receives a typed selection context and opaque host services; the extension owns only its page content.
- Initial tabs:
  - Blocks: Library, Facade/Preview, Instance, Layout, Publish.
  - JLCPCB: Overview, Catalogue, Symbol, Footprint, 3D.
  - Clippy: Findings, Explanation, Fix Preview, Component Intent.
- Context providers are separate from static workspace pages. For a selected symbol, all applicable providers may contribute fields or tabs.
- Do not promise insertion into KiCad's built-in Symbol Properties dialog. Its current property panel is a single property-grid implementation, not an extensible third-party tab registry. A sibling/tabified AUI pane is the first realistic target.

### 7. Property metadata and persistence
- Keep plugin metadata namespaced by extension and versioned.
- Prefer KiCad custom properties or project-local metadata keyed by schematic sheet path + symbol UUID; avoid using reference designators as stable IDs.
- Blocks page: block UUID, implementation, variant, profile and typed parameters.
- JLCPCB page: LCSC ID, MPN, manufacturer, supplier/package mapping and cached catalogue status.
- Clippy page: component kind/role/function/rationale, target IC/pin, capacitor target/return terminals, return net, cluster and distance limits.
- Property writes must use native commits and preserve unrelated fields/library identity.

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
- Synthetic KIFACE original + proxy + extension module.
- OS-native module loading and symbol resolution.
- Full synthetic KIFACE virtual-method forwarding.
- Extension initialize/window/reset/shutdown callbacks.
- Python install/uninstall against a temporary fake KiCad plugin directory.
- Deferred install waits for host exit.

The harness is useful for loader mechanics only. It does not compile the production proxy against KiCad headers or prove that a real KiCad frame can be safely inspected.

### Required platform test matrix

| Test | Windows | macOS | Linux |
|---|---|---|---|
| Build proxy with official KiCad headers/toolchain | Required | Required (arm64 + x86_64 if shipped) | Required |
| Load proxy in synthetic host | CI | CI | CI |
| Forward original KIFACE in synthetic host | CI | CI | CI |
| Install/restore hash-verified fake modules | CI | CI | CI |
| Real KiCad Eeschema launch and close | Manual/integration runner | Manual/integration runner | Manual/integration runner |
| Real KiCad PCB Editor launch and close | Manual/integration runner | Manual/integration runner | Manual/integration runner |
| Real GAL overlay and mouse pass-through | GUI test | GUI test | GUI test |
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

1. The synthetic KIFACE header is a test double, not a KiCad ABI projection. The README currently refers to a production projection path that is not in the tree; correct that before claiming real ABI coverage.
2. The real-proxy CMake option is only a starting point. It does not yet discover the full KiCad build's generated headers, wxWidgets configuration, compile definitions, imported targets, runtime dependencies or exact module output layout.
3. No build fingerprint gate exists yet.
4. The host API currently exposes logging, GUI dispatch and lifecycle callbacks only. GAL, object model, tools, placement, docking, selection and property-provider services are not implemented.
5. The extension directory and callbacks are currently hard-coded for Clippy; the host should become extension-neutral and discover Blocks/JLCPCB/Clippy modules by manifest.
6. The frame callback reports a raw wxWindow pointer but does not identify the main editor frame, canvas, view, document or selection. It must be upgraded to an editor adapter callback after window construction.
7. The installer has no platform privilege-elevation flow and no integration with the running IPC add-on. It must schedule after *all* host processes exit and confirm executable identity/PID to avoid PID-reuse races.
8. Existing CI tests only the Eeschema synthetic path; PCB proxy and both real editor faces need separate coverage.
9. The current Python deferred-helper test had a POSIX zombie-process wait bug; the helper wait now attempts to reap a child before checking process existence. Re-run the cross-platform workflow to confirm.
10. No real KiCad GUI tests or app-bundle Gatekeeper tests are in CI yet.

## Delivery order

P0 — loader correctness:
1. Fix and pass the three-platform synthetic harness.
2. Test both Eeschema and PCB proxy faces.
3. Generate/check the KIFACE ABI projection against the exact KiCad header and reject unknown fingerprints.
4. Add real-install discovery, all-process shutdown wait, backup/restore and startup recovery.

P1 — first real native editor integration:
5. Build one real Eeschema adapter against an exact KiCad build.
6. Discover frame/canvas/view and draw one transient GAL overlay.
7. Prove passive pass-through, hover and one interactive tool event.
8. Add one shared AUI pane and a two-provider tab registry.

P2 — core workflows:
9. Implement schematic native move handoff and verify snap/undo/cancel.
10. Implement shared selection/document identity and contextual property providers.
11. Port the three plugin pages and board-side overlay/placement adapter.

P3 — production hardening:
12. Add fix transaction planner and post-edit verification.
13. Expand version/build matrix and automated real-GUI smoke tests.
14. Add platform installer UX, diagnostics, rollback and recovery documentation.
