# Native extension API capability and call catalogue

This document consolidates the API requirements derived from [kicad-blocks](https://github.com/Uhrendoktor/kicad-blocks), [kicad-jlcpcb-interface](https://github.com/Uhrendoktor/kicad-jlcpcb-interface) and [kicad-clippy](https://github.com/Uhrendoktor/kicad-clippy). It is a proposed API specification, not a claim that every call below is implemented.

**KiCad source baseline inspected:** b7066f886375b186ca8fa469de3a6382f6bba400. The C++ adapter must be rebuilt and checked against each exact KiCad source/build/toolchain. KIFACE, wxWidgets and KiCad object layouts are not stable ABIs.

## Status vocabulary

| Status | Meaning |
|---|---|
| **Implemented** | Present in this prototype and exercised by tests or the pinned Linux live-editor smoke. |
| **Native primitive** | KiCad has a native class/method for it, but KUA does not yet expose a safe wrapper. |
| **Wrapper needed** | Can be implemented over an inspected KiCad/IPC facility; KUA lifecycle and validation still needed. |
| **KiCad API gap** | Needs an upstream extension hook or narrowly scoped version-specific bridge. |
| **Unverified** | Needs real-editor and/or platform acceptance tests. |

## 1. ABI and object model principles

- Stable extension surface: C ABI, fixed-width scalars, UTF-8, versioned structs, callbacks and opaque handles. Never pass wxWidgets or KiCad C++ objects by value.
- Opaque, generation-checked handles for editor, document, object, selection, transaction, overlay, window, pane and task.
- Schematic identity is (project/document, sheet-instance path, item UUID/KIID). UUID alone is insufficient for repeated hierarchical sheet instances.
- PCB identity is (board/document, item UUID/KIID). Reference and pad number are semantic selectors, not primary identities.
- All GUI, GAL, selection, tool and document mutations run on the owning editor's GUI thread.
- Background tasks return copied/serialized results and marshal UI changes back to the GUI thread.
- Do not expose native pointers to extensions. The existing frame/canvas/view pointers are borrowed callback context only; they expire at frame close.
- Use native selection, tools, snapping, commits, undo and cancel. No OS-level mouse/keyboard injection.
- Keep rendering overlays separate from document items. Diagnostic paint must never modify the design.
- Capability-detect functions and validate struct_size. A plugin must not infer support from a KiCad version string alone.

## 2. Existing KUA API

The current header already provides:

| Current field/call | Purpose | Status |
|---|---|---|
| `abi_version`, `struct_size` | Extension ABI negotiation | Implemented |
| `host_build_tag`, `editor_kind` | Host build/editor identity | Implemented |
| `log(level, message)` | Host logging | Implemented |
| `post_to_gui(callback, user_data)` | GUI-thread dispatch | Implemented |
| `overlay_create/destroy/clear` | Create and manage a host-owned overlay | Implemented |
| `overlay_set_style` | RGBA stroke/fill, width and enable flags | Implemented |
| `overlay_line/circle/rectangle` | Initial GAL geometry calls | Implemented |
| `KUA_EditorContext.frame/canvas/view` | Borrowed handles for primary editor frame and GAL view | Implemented |
| `initialize/on_editor_window_created/on_editor_reset/shutdown` | Extension lifecycle callbacks | Implemented |

The pinned Linux CI smoke passed initialization, real frame/canvas/view discovery and GAL API calls in both Eeschema and PCB Editor. Pixel-level rendering, mouse pass-through, Windows/macOS production builds and platform window ownership remain unverified.

### Proposed common types

- `KUA_HostInfo`: ABI, KiCad source commit/build tag, platform, architecture, editor kind, wxWidgets/compiler ABI details when discoverable, feature flags.
- `KUA_EditorHandle`: editor frame/session and generation.
- `KUA_DocumentHandle`: document kind, project identity and generation.
- `KUA_ObjectHandle`: object kind, UUID/KIID, document and optional sheet path.
- `KUA_SelectionSnapshot`: selected object handles, primary item and reference point.
- `KUA_Point2D/KUA_Rect2D`: coordinate values plus explicit units/coordinate-space enum.
- `KUA_Result`: result code, message and optional operation token.
- `KUA_TaskHandle`: progress, cancellation and terminal result.
- `KUA_EventToken`: explicit unregisterable subscription.

Use documented ownership for strings and buffers. Prefer host-allocated result buffers with a release function or a caller-buffer size-query pattern.

## 3. Native windows and KiCad window grouping

Companion plugins currently create Tk windows. Blocks explicitly limits Tk transient parenting to windows from the same Tk interpreter; launching a second KiCad document is a subprocess. Those windows cannot reliably become owned/transient children of the KiCad editor across process boundaries.

The native host should create wxWidgets dialogs and tool windows inside KiCad's process and set the initiating editor as their owner/parent. wxWidgets then handles the platform-specific native owner/transient relationship, focus, activation and window grouping.

### Proposed window calls

- `window_create_dialog(editor, owner, title, flags)`
- `window_create_tool_window(editor, owner, title, flags)`
- `window_set_owner(window, owner)`
- `window_set_parent(window, parent)`
- `window_show(window)` / `window_hide(window)`
- `window_raise(window)` / `window_focus(window)`
- `window_set_modal(window, mode)` (modeless, editor-window-modal, application-modal)
- `window_set_title(window, title)`
- `window_set_size(window, min_size, preferred_size, max_size)`
- `window_set_position(window, rect, coordinate_space)`
- `window_center_on_parent(window)`
- `window_is_visible(window)` / `window_is_focused(window)`
- `window_close(window, reason)` / `window_destroy(window)`
- `window_on_event(window, callback)` (close, activate, focus, move, resize, DPI/theme)
- `window_get_display_info(window)` (monitor, DPI scale, work area)
- `file_dialog_open/save/directory(editor, options)`

Required semantics:
- Default owner is the editor that invoked the plugin action.
- Modeless utility windows are grouped with that editor and do not steal canvas focus unless requested.
- Editor close triggers transient-window close callbacks before plugin resources are released.
- Persist geometry per editor, not globally; handle monitor/DPI changes.
- A foreign process window must never be reparented or assigned an owner through a guessed OS window ID.

## 4. Docking, shared workspaces and page providers

### Proposed docking calls

- `dock_panel_register(editor, panel_id, title, factory, flags)`
- `dock_panel_unregister(editor, panel_id)`
- `dock_panel_show/hide(editor, panel_id)`
- `dock_panel_set_caption/icon/min_size(editor, panel_id, value)`
- `dock_panel_float(editor, panel_id)` / `dock_panel_dock(editor, panel_id, direction)`
- `dock_panel_get_state(editor, panel_id)`
- `dock_layout_save(editor, layout_id)` / `dock_layout_restore(editor, layout_id)`
- `dock_panel_on_state_changed(editor, panel_id, callback)`

### Proposed workspace calls

- `workspace_register(editor, workspace_id, title, flags)`
- `workspace_add_page(editor, workspace_id, plugin_id, page_id, title, factory)`
- `workspace_remove_page(editor, workspace_id, plugin_id, page_id)`
- `workspace_select_page(editor, workspace_id, plugin_id, page_id)`
- `workspace_set_page_visible(editor, workspace_id, plugin_id, page_id, visible)`
- `workspace_refresh_page(editor, workspace_id, plugin_id, page_id, reason)`
- `workspace_get_active_page(editor, workspace_id)`
- `workspace_set_context(editor, workspace_id, selection_snapshot)`

The design should have **one host-owned workspace pane per editor**, with one wxAuiNotebook/tab strip and pages contributed by the three plugins:

| Plugin | Suggested pages |
|---|---|
| Blocks | Library, Facade Preview, Instance, Implementation, PCB Layout, Publish/Sync |
| JLCPCB | Project Overview, Catalogue, Symbol, Footprint, 3D/STEP |
| Clippy | Findings, Explanation, Related Objects, Fix Preview, Component Intent |

The host owns docking, floating, tab navigation, theme, focus, context propagation, layout persistence and teardown. Plugins own only their page controls and application logic. Do not create one competing dock pane per plugin.

### AUI access constraint

Both editor families use wxAuiManager. In the inspected pinned headers PCB Editor publicly exposes `PCB_BASE_EDIT_FRAME::GetAuiManager()`. Eeschema inherits `m_auimgr` from `EDA_BASE_FRAME`, where the member is protected and no public getter is declared. The Eeschema bridge therefore needs a small version-specific accessor or an upstream public getter. Do not use member-offset hacks or unsafe casts.

## 5. Extending KiCad's built-in Properties UI

There are three distinct feature levels:

| Level | Capability | Status |
|---|---|---|
| Native custom fields | Persist/read user-visible symbol and footprint custom fields | Native data support; KUA wrapper needed |
| Shared contextual inspector | Show plugin-owned tabs in a shared docked workspace | KUA workspace/provider registry needed; recommended first |
| Tabs inside built-in Symbol/Footprint Properties | Add third-party pages to the native properties dialog/property grid | **KiCad API gap** in inspected source |

The inspected `PROPERTIES_PANEL` contains a single wxPropertyGrid and selection-aware rebuilds. `SCH_PROPERTIES_PANEL` and `PCB_PROPERTIES_PANEL` customize property construction and change handling, but no public AddPage/provider registry was found. A true in-dialog extension needs a KiCad-side provider hook.

### Proposed property-provider calls

- `property_provider_register(editor, provider_id, plugin_id, priority, predicate, factory)`
- `property_provider_unregister(editor, provider_id)`
- `property_provider_matches(selection_context)`
- `property_provider_create_page(selection_context, parent)`
- `property_provider_refresh(selection_context, reason)`
- `property_provider_commit(selection_context, edits, transaction)`
- `property_provider_on_selection_changed(callback)`

### Proposed property data calls

- `property_list(object)`
- `property_get(object, name)`
- `property_set(object, name, value, transaction)`
- `property_remove(object, name, transaction)`
- `property_get_custom_fields(object)`
- `property_set_custom_fields(object, fields, transaction)`
- `property_schema_register(plugin_id, schema_id, schema)`
- `property_validate(object, edits, schema_id)`
- `metadata_get/set(scope, key, json, transaction)`

Use standard KiCad fields for simple values such as LCSC/MPN/Value/Footprint when appropriate. Use namespaced project metadata for larger typed objects such as Blocks profiles and Clippy component-intent contracts. Never silently overwrite fields owned by the user or another plugin.

## 6. General API call catalogue

These are proposed method names grouped by responsibility. They are the general reusable API surface that all extensions can build on.

### Host, sessions and lifecycle

- `host_get_info()`, `host_get_capabilities()`, `host_get_editors()`
- `editor_get_context(editor)`, `editor_get_active_document(editor)`, `editor_get_active_tool(editor)`
- `editor_activate(editor)`
- `editor_on_attached/detached/reset/project_changed(callback)`
- `editor_on_document_opened/closed/changed(callback)`
- `editor_get_open_documents(editor)`

### Documents and object identity

- `document_get_identity/path/kind/project/modified_state(document)`
- `document_save(document)`, `document_reload(document, policy)`, `document_close(document, save_policy)`
- `document_open_in_editor(path, editor_kind, owner)`
- `document_list_objects(document, object_kind, filter)`, `document_get_bounds(document)`
- `object_get/get_kind/get_uuid/get_parent/get_document/get_properties(object)`
- `object_get_geometry(object, coordinate_space)`
- `object_resolve(document, kind, uuid, sheet_path)`, `object_find(document, query)`
- `object_create/update/delete(object, data_or_patch, transaction)`

### Schematic model

- `schematic_get_hierarchy/list_sheets/get_sheet_path/get_sheet(document, path)`
- `schematic_get_symbols/sheets/labels/wires/buses/junctions/no_connects(sheet)`
- `schematic_get_symbol(document, uuid, sheet_path)`
- `schematic_get_symbol_pins(symbol)`
- `schematic_get_hierarchical_labels(sheet)`
- `schematic_get_text_and_graphics(sheet)`
- `schematic_get_net_for_pin(symbol, pin_number)`, `schematic_get_netlist(document, options)`
- `schematic_place_symbol(document, library_id, position, transaction)`
- `schematic_place_sheet(document, data, position, transaction)`
- `schematic_update_symbol_fields(symbol, fields, transaction)`
- `schematic_update_pin_map(symbol, pin_map, transaction)`
- `schematic_move_items(document, items, reference_point, transaction)`
- `schematic_interactive_move(document, items, transaction)`
- `schematic_navigate_to_sheet(document, sheet_path)`

### PCB model

- `board_get_info/layers/nets/footprints/tracks/vias/zones/graphics/groups(document)`
- `board_get_footprint(document, uuid)`, `board_get_pads(footprint)`
- `board_get_net_for_pad(pad)`
- `board_place_footprint(document, library_id, position, transaction)`
- `board_import_footprint_library(document, library_id, transaction)`
- `board_merge_items(document, source_items, transform, remap_policy, transaction)`
- `board_interactive_move(document, items, transaction)`
- `board_set_active_layer(document, layer_id)`, `board_set_visibility(document, layer_id, visible)`
- `board_fill_zones(document, zone_ids, options)`
- `board_get_3d_models(footprint)`, `board_open_3d_viewer(document, selection)`

### Selection, focus and navigation

- `selection_get(editor)`, `selection_get_primary(editor)`
- `selection_set/add/remove/clear(editor, objects, reference_point)`
- `selection_subscribe(editor, callback)`, `selection_get_bounds(editor)`
- `editor_focus_object(editor, object)`, `editor_zoom_to_object(editor, object, margin)`
- `editor_zoom_to_bounds(editor, bounds)`
- `editor_cross_probe(source_editor, target_editor, object_or_net)`
- `editor_highlight_objects(editor, objects, style)`, `editor_clear_highlight(editor, owner_id)`

### Libraries, project and settings

- `library_list/get_info/register/unregister/reload/wait_ready(scope, kind, nickname)`
- `library_find_symbol/footprint(library_id)`
- `library_get_symbol/footprint(library_id)`
- `library_import_symbol/footprint(source, destination, conflict_policy)`
- `library_export_symbol/footprint(object, destination)`
- `library_get_3d_assets(footprint)`, `library_import_3d_asset(asset, destination)`
- `project_get_info/path_variables/settings/metadata(document, namespace)`
- `project_set_settings/metadata(document, namespace, json, transaction)`
- `settings_get/set(scope, key, value)`
- `theme_get_palette(editor)`, `theme_on_changed(editor, callback)`
- `editor_get_grid/snap_settings/units(editor)`

### Coordinate conversion and GAL rendering

- `geometry_convert(point, from_space, to_space, editor)`
- `geometry_convert_rect(bounds, from_space, to_space, editor)`
- `view_get_world_bounds/scale/center/pixel_size(editor)`
- `view_zoom_to(editor, bounds, margin)`, `view_refresh(editor)`
- `overlay_create/destroy/clear(editor, owner, layer, mode)`
- `overlay_set_style/z_order/visible/document_scope(overlay, options)`
- `overlay_point/line/polyline/rectangle/circle/arc/polygon/text(overlay, geometry)`
- `overlay_object_outline(overlay, object, style)`, `overlay_update(overlay)`

Coordinate APIs must state whether values are KiCad internal units, millimetres, screen pixels or normalized viewport positions. Overlay targets should include path/text boxes as well as points, segments, rectangles, polygons, circles and arcs.

### Tools, actions and input

- `action_register/unregister/invoke(editor, plugin_id, action_id, parameters)`
- `action_set_enabled(editor, action_id, enabled)`
- `action_add_to_toolbar/menu/context_menu(editor, action_id, placement)`
- `tool_register/unregister/activate/cancel(editor, tool_id)`
- `tool_get_state(editor)`
- `input_get_cursor_position/modifiers(editor)`
- `input_observe(editor, event_mask, callback)`
- `input_consume/release(event_token)`
- `hit_test_register/unregister/query(editor, geometry, callback)`
- `tooltip_show/hide(editor, position, text)`

Passive overlays never consume events; hover observes and hit-tests; interactive markers consume only explicitly registered target events.

### Transactions and undo

- `transaction_begin(document, description)`
- `transaction_create/update/delete(transaction, object, data)`
- `transaction_stage(transaction, object, change_kind)`
- `transaction_checkpoint(transaction)`, `transaction_revert_to_checkpoint(transaction, checkpoint)`
- `transaction_commit/revert(transaction)`
- `transaction_handoff_to_tool(transaction, action_id, parameters)`
- `document_undo/redo(document)`

Map the native side to `SCH_COMMIT::Push/Revert` and `BOARD_COMMIT::Push/Revert`. A tool handoff transfers commit ownership; the caller must not reuse or destroy it afterwards.

### Diagnostics, native checks and fixes

- `diagnostic_publish/update/clear/query(editor, diagnostic)`
- `diagnostic_clear_owner(editor, plugin_id)`
- `diagnostic_select_related/navigate(editor, diagnostic_id, related_index)`
- `diagnostic_set_suppressed(editor, diagnostic_id, suppressed)`
- `analysis_run_erc/drc(document, options)`
- `analysis_export_netlist(document, options)`
- `analysis_get_report/cancel(task_handle)`
- `fix_register(plugin_id, fix_id, callbacks)`
- `fix_preview(document, fix_id, target, parameters)`
- `fix_apply(document, fix_id, target, preconditions, transaction)`
- `fix_verify(document, fix_id, validation_plan)`, `fix_revert(document, fix_id, transaction)`

A diagnostic target must have typed geometry, document identity, UUID/KIID and optional schematic sheet path. Preserve source byte spans separately from physical CAD coordinates. Native ERC/DRC remain authoritative; Clippy can run its Rust engine as a local service and publish its normalized results.

### Background work, status and utilities

- `task_submit/cancel/get_state/get_progress(plugin_id, task_type, parameters)`
- `task_on_progress(task_handle, callback)`
- `status_set/clear(editor, owner_plugin, message, severity)`
- `progress_begin/update/end(editor, owner_plugin, task_handle)`
- `notification_show(editor, owner_plugin, title, body, actions)`
- `file_dialog_open/save/directory(editor, options)`
- `clipboard_get_text/set_text(editor, text)`, `browser_open_url(editor, https_url)`

## 7. Requirements mapped to the three projects

| Required capability | Blocks | JLCPCB Interface | Clippy |
|---|---|---|---|
| Hierarchy and UUID identity | Extract child-sheet trees and facade instances | Scan repeated components across sheets | Resolve rule targets across sheet instances |
| Symbol/pin/field inspection | Build facade pins; preserve pin topology | Read/write Reference, Value, Footprint, Datasheet, LCSC, MPN and custom fields | Resolve pin anchors, symbols, text and graphics |
| Symbol/library operations | Create, publish and sync facade definitions | Import reviewed symbols/footprints; project library registration | Usually read-only; edits only through explicit fixes |
| Native interactive move | Essential for Place Block and layout handoff | Essential for Add Component | Needed for positional fixes |
| PCB objects | Merge layouts and remap nets/references/UUIDs | Footprint and 3D preview/mapping | Pads, footprints, tracks, vias, zones and DRC targets |
| Properties and metadata | Variant/profile/typed parameters/layout link | Supplier/catalogue fields and proposed component mapping | Role/function/rationale, connections, DNP, groups, test-point and geometry intent |
| Windowing/docking | Library/implementation/layout workspace | Overview/catalogue/review/preview workspace | Findings/explanation/fix/intent workspace |
| GAL and interaction | Pin/interface/layout affordances | Optional selected-part highlights | Core diagnostic targets and hover/click markers |
| Transactions/verification | Publish/sync and board merge | Safe metadata edits and asset import | Reviewable fixes, undo and rerun ERC/DRC/rules |

### Important project-specific contracts

**Blocks:** support `KB_VARIANT`, `KB_PROFILE`, `KB_LAYOUT` and `KB_PARAM_<NAME>` as typed per-instance configuration. Pin definitions must remain derived from implementation hierarchical labels. Publishing must preserve placed instance identity when topology is unchanged and report an explicit migration when pin topology changes.

**JLCPCB:** update mode must preserve symbol library identity, graphics, pins, units, electrical types and unrelated custom fields. Library synchronization is a separate confirmed operation on writable libraries. Add mode should import a reviewed project-local symbol/footprint and use native interactive placement. Catalogue stock/price are snapshots, not a purchase guarantee.

**Clippy:** support component kind/role/function/rationale, pin-to-net maps, connector pin expectations, decoupling target IC/pin, capacitor target/return terminals and nets, DNP/population, group membership, frame/title-tab links, grid/label contracts and test-point accessibility. One diagnostic may have multiple related object/geometry targets.

## 8. Delivery order

### P0 — host foundation
1. Editor/document/session handles, lifecycle callbacks and stale-handle rejection.
2. Runtime compatibility gate and exact-build source/API identity.
3. Stable C ABI v2, capability negotiation, extension manifest and GUI dispatch.
4. Native editor-owned window create/parent/close and lifecycle cleanup.
5. One shared docked workspace pane with page registry; solve Eeschema AUI access through a version-specific bridge/upstream accessor.
6. Multi-frame, close, project-switch, focus, DPI and plugin-unload tests.

### P1 — native editing and properties
1. Sheet-path-aware schematic object/selection snapshots and navigation.
2. Eeschema interactive-move commit handoff with confirm/cancel acceptance tests.
3. Shared contextual property provider registry.
4. Separate KiCad-side design for real built-in property-window/grid extension hooks.
5. Custom field CRUD, library import/reload and undo-safe transactions.

### P2 — three project workflows
1. Blocks library/instance/implementation/PCB-layout pages and native board merge/placement.
2. JLCPCB overview, supplier metadata, CAD import, footprint and 3D preview pages.
3. Clippy diagnostic geometry, overlays, navigation, component intent and fix preview/apply.
4. Native ERC/DRC/netlist integration and cancellable background work.

### P3 — production hardening
1. Windows/macOS production adapter builds and live GUI smoke.
2. Native window grouping and docking tests on Windows, macOS and Linux.
3. Packaging, permissions, rollback, crash isolation and supported-build policy.

## Source references

Companion projects:
- [Blocks README](https://github.com/Uhrendoktor/kicad-blocks/blob/main/README.md)
- [Blocks feature reference](https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/reference.md)
- [Blocks UI inventory](https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/ui-inventory.md)
- [Blocks schematic interactive-move proposal](https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/upstream/kicad-schematic-interactive-move-proposal.md)
- [JLCPCB README](https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/README.md)
- [JLCPCB user guide](https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/docs/user-guide.md)
- [Clippy architecture](https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/architecture.md)
- [Clippy component metadata](https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/component-metadata.md)
- [Clippy roadmap](https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/roadmap.md)

Pinned KiCad headers:
- [KIFACE](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/kiway.h)
- [EDA_BASE_FRAME](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/eda_base_frame.h)
- [PCB_BASE_EDIT_FRAME](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/pcbnew/pcb_base_edit_frame.h)
- [TOOL_MANAGER](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/tool/tool_manager.h)
- [VIEW](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view.h)
- [VIEW_OVERLAY](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view_overlay.h)
- [Properties panel](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/common/widgets/properties_panel.h)
- [Schematic Properties panel](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/widgets/sch_properties_panel.h)
- [PCB Properties panel](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/pcbnew/widgets/pcb_properties_panel.h)
- [SCH_COMMIT](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/sch_commit.h)
- [BOARD_COMMIT](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/pcbnew/board_commit.h)
