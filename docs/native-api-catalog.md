# Native API capability catalogue

This summary maps the public/in-process facilities needed by KiCad Blocks, JLCPCB Interface and kicad-clippy. The full proposed C API call catalogue, native window/window-group API, docking API, native wxWindow/menu/AUI/notebook/property-grid binding catalogue, object model and priority matrix are in [api-capability-catalog.md](api-capability-catalog.md). Native source baseline for this summary: KiCad b7066f886375b186ca8fa469de3a6382f6bba400. Exact headers, exported symbols and object layout must be checked against each supported KiCad build.

## The loader gives process access, not universal symbol access

KIFACE proxying places the extension in the editor process. It does not make hidden symbols exported automatically. KiCad globally enables hidden C++ visibility and uses explicit export macros. Distinguish:

1. **Exported library API:** an implementation symbol is present in the platform shared library export table and can be linked.
2. **Inline/header API:** implementation is compiled into our extension using exact KiCad headers. It depends on class layout, compiler ABI and build flags.
3. **Virtual interface API:** a public base-class virtual call dispatches through the live editor object's vtable. This can reach an override whose implementation symbol is hidden, provided the class layout and ABI match.
4. **KIFACE service:** an explicitly exposed editor pointer/service returned by `IfaceOrAddress(dataId)`.
5. **Private non-exported implementation:** not callable merely because a header exists. It needs a supported virtual interface, inline wrapper, exported symbol or a version-specific adapter.

A generated per-build SDK should record all five categories. Do not describe all KiCad headers as a stable public C++ ABI.

## GAL and transient overlays

The host C ABI now has an initial overlay slice: create/destroy/clear, RGBA stroke/fill style, line, circle and rectangle. Handles are opaque and host-owned. Calls are GUI-thread-only and must be dispatched with `post_to_gui`; geometry arguments use KiCad world coordinates (internal units), not screen pixels or millimetres. `VIEW::MakeOverlay()` already attaches the overlay to the view; the host retains the returned `shared_ptr`, removes the overlay from the view before releasing it, and refreshes the canvas after mutations. Synthetic tests verify function-table presence and invalid-handle behavior only; actual rendering and frame-close ordering still require a real KiCad GUI test.


Relevant interfaces:
- `EDA_DRAW_FRAME::GetCanvas()`
- `EDA_DRAW_PANEL_GAL::GetView()`
- `KIGFX::VIEW::MakeOverlay()`
- `KIGFX::VIEW::Remove()`
- `KIGFX::VIEW::SetLayerDisplayOnly()`
- `KIGFX::VIEW_OVERLAY` drawing methods: `Line`, `Circle`, `Polygon`, `BitmapText`, etc.

The current implementation of `VIEW::MakeOverlay()` creates a `VIEW_OVERLAY`, immediately calls `VIEW::Add()`, and returns shared ownership. **Do not call `VIEW::Add()` a second time.** Retain the returned `shared_ptr` and remove the object from the view before releasing it.

Eeschema's schematic draw panel configures `LAYER_GP_OVERLAY` as an overlay target and marks it display-only. KiCad documents display-only layers as rendered but excluded from hit-test queries. Use the passive layer for diagnostic paint; create independent hit-test geometry for hover/interactive markers, and route events through a native editor tool on the GUI thread.

For the required input modes:
- Passive: render on display-only layer; do not register a pointer consumer.
- Hover: keep the visual overlay passive, but add an editor tool that observes pointer movement and performs its own geometry hit-test. It should not consume the event.
- Interactive: editor tool consumes only events over the hit target; use a proper KiCad tool/action to handle keyboard focus and click actions. Never install global mouse hooks or send OS-level synthetic input.

PCB Editor's `VIEW` layer-target/display-only configuration must be verified separately. Do not assume that Eeschema's setup automatically applies to PCB.

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view.h
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view_overlay.h
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/common/view/view.cpp
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/sch_draw_panel.cpp

## Editor and document model

### Eeschema
- `SCH_EDIT_FRAME`, `SCH_BASE_FRAME`, `SCHEMATIC`, `SCH_SCREEN`, `SCH_SHEET_PATH`
- `SCH_SYMBOL`, `SCH_PIN`, `SCH_LINE`, `SCH_LABEL`, `SCH_JUNCTION`, `SCH_SHEET`
- `SCH_COMMIT` for undoable transactional edits
- `SCH_SELECTION_TOOL` and `SCH_ACTIONS` for selection and native tools

The Eeschema main editor frame is created by the Eeschema KIFACE's `CreateKiWindow(FRAME_SCH)`. The base class `EDA_DRAW_FRAME::GetCanvas()` is virtual and `EDA_DRAW_PANEL_GAL::GetView()` is virtual; an exact-build extension can use these base interfaces to reach the live schematic view. Use the correct frame class ID and guard against symbol editor, viewer and preview windows.

### PCB Editor
- `PCB_EDIT_FRAME`, `PCB_BASE_EDIT_FRAME`, `BOARD`
- `FOOTPRINT`, `PAD`, `PCB_TRACK`, `PCB_VIA`, `ZONE`
- `BOARD_COMMIT`, `PCB_SELECTION_TOOL`, `PCB_ACTIONS`

The same base canvas/view route applies. The actual board view is `KIGFX::PCB_VIEW`.

## Cursor-attached placement and snapping

Eeschema has a native move action: `SCH_ACTIONS::move`. `TOOL_MANAGER::PostAPIAction(action, COMMIT*)` transfers an API-owned commit to the tool/action system. `SCH_COMMIT` can be constructed from a `TOOL_MANAGER*` or frame.

The intended exact-symbol placement sequence is:
1. Resolve the target library symbol and active sheet.
2. Create/insert the symbol at a temporary coordinate inside a `SCH_COMMIT`.
3. Set the selection's reference point to the symbol anchor.
4. Set the selection in `SCH_SELECTION_TOOL`.
5. Post `SCH_ACTIONS::move` with the commit.
6. Let the existing schematic move tool own cursor movement, grid/magnetic snapping, confirm, cancel and undo.

This is the required native route for Blocks and JLCPCB Add mode. It is not the same as launching the ordinary Add Symbol picker. The existing `Board.interactive_move()` IPC operation is a precedent for asynchronous PCB placement; Eeschema does not currently expose the same operation through its IPC schema. A native in-process extension may be able to invoke the existing move action directly, but the full create/selection/commit handoff must be proven in a real Eeschema GUI test.

Sources:
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/tool/tool_manager.h
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/sch_commit.h
- https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/tools/sch_actions.h
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/upstream/kicad-schematic-interactive-move-proposal.md

## Native UI bindings: windows, menus, docking and properties

Plugins create and own their native windows, controls, menu entries, notebook pages and AUI panes. KUA exposes existing wxWidgets/KiCad methods; it does not create a common workspace or decide plugin UI.

- **Windows:** wxWindow/wxDialog/wxFrame creation, parenting/ownership, visibility, focus, geometry, child traversal/class inspection, events and wxSizer layout.
- **Menus:** wxFrame::GetMenuBar and wxMenuBar/wxMenu Append/Insert/Remove/Replace, submenu, enable/check/label and event binding.
- **Docking:** wxAuiManager::GetManager/GetPane/AddPane/InsertPane/DetachPane/Update/SavePerspective/LoadPerspective and wxAuiPaneInfo.
- **Notebook pages:** child traversal plus wxNotebook AddPage/InsertPage/RemovePage/DeletePage/GetPageCount/SetSelection.
- **Side Properties:** EDA_DRAW_FRAME::GetPropertiesPanel and PROPERTIES_PANEL::GetPropertyGrid; wxPropertyGrid Append/Insert/Delete/Clear/value/attribute/event operations.

The modal Eeschema Symbol Properties and PCB Footprint Properties dialogs each contain a wxNotebook child that can accept a plugin-owned page. The persistent side Properties panel is a single wxPropertyGrid, not a notebook. See [api-capability-catalog.md](api-capability-catalog.md) for detailed calls and source evidence.

## Other shared native services

| Service | Intended implementation |
|---|---|
| Editor lifecycle | KIFACE proxy callbacks for startup, frame/window creation, reset and shutdown |
| GUI thread | wxApp::CallAfter and editor tools; never call wx/GAL from worker threads |
| Selection | Editor's selection tool and KIID/UUID mapping; sheet path is part of schematic identity |
| Navigation | Native focus/cross-probe helpers, then selection |
| Tool registration | Existing TOOL_MANAGER/ACTION_MANAGER per editor |
| Docking | Native wxAuiManager methods; plugin owns pane/window |
| Properties | Native wxPropertyGrid rows and wxNotebook pages; plugin owns values/pages |
| Project configuration | KiCad project/settings APIs and project-local profile files |
| Native edit primitives | Existing SCH_COMMIT/BOARD_COMMIT calls; plugin chooses transaction and validation logic |
| DRC/ ERC | Existing native engines and IPC/CLI reports; do not duplicate KiCad's validators |

## Project-by-project native UI mapping

The following lists describe UI each plugin owns and creates itself using KUA's native wxWidgets/KiCad bindings; they are not host-owned pages or workflows.

### kicad-blocks
- Blocks Library panel: searchable reusable block/implementation list.
- Preview tab: schematic facade and interface-pin preview.
- Place: create facade, then hand it to native Eeschema move tool; native grid/snap and undo/cancel remain authoritative.
- Instance tab: variant, profile, typed parameters and optional board layout.
- PCB layout placement: reuse the native PCB interactive move flow.
- Publish/sync pages: operate on the existing Rust/Python library model.

### kicad-jlcpcb-interface
- Project Overview tab: symbols, linked LCSC IDs, repeated instances, stock coverage and estimated cost.
- Context properties tab: supplier, MPN, LCSC, package, value/footprint mapping.
- Add/Update flow: use native selected-symbol context and symbol-library import; use native move handoff for new parts.
- Preview tabs: Symbol, Footprint and 3D/STEP preview.
- Select-in-schematic: native sheet navigation/focus and selection.

### kicad-clippy
- Diagnostic overlay targets: point/segment/rect/polyline/polygon/arc/circle and object UUIDs.
- Passive/hover/interactive hit handling.
- Finding tabs: severity/rule filters, explanation, help, related objects.
- Fix tabs: preview edits, apply via native commits, verify by reparsing and rerunning ERC/DRC/rules.
- Component-intent property page: typed roles, decoupling relationship (root IC/pin, target and return terminals/nets, constraints), test point and connector roles.


## Native windows and editor grouping

The current Python/Tk companion plugins cannot reliably establish native ownership across process boundaries. The host should create wxWidgets dialogs and modeless tool windows inside KiCad and parent them to the invoking editor. This allows the platform adapter to establish normal owner/transient grouping, focus, activation and close lifetime. See the complete method catalogue in [api-capability-catalog.md](api-capability-catalog.md).

Expose existing wxDialog/wxFrame/wxWindow create/parent/show/hide/raise/focus/close/event methods and native file dialogs. A plugin creates and manages its own dialogs and child controls; for docking, the plugin adds its own window to the existing AUI manager. Do not reparent a window in another process or guess native window IDs.

## Current frame context handles

The lifecycle callback is emitted only for the primary Eeschema or PCB Editor frame. Its KUA_EditorContext includes borrowed opaque handles for the frame, GAL canvas and KIGFX::VIEW. These pointers are host-owned and must not be deleted; extensions must not retain them after the corresponding editor frame closes. The synthetic host supplies null canvas/view handles, so only the real KiCad adapter can validate their contents. The next API layer must expose capability functions for overlays and editor operations rather than requiring extensions to call KiCad C++ methods directly.
