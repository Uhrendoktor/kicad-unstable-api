# Native KiCad IPC / binding catalogue

## Scope and boundary

KUA extends KiCad's IPC surface with calls to existing native KiCad and wxWidgets behavior. It does not implement Blocks, JLCPCB or Clippy features, workflows, business logic, metadata schemas or plugin UI.

```
Plugin (Blocks / JLCPCB / Clippy)
  owns: windows, layout, pages, menus, panes, metadata, workflows,
        persistence, event interpretation and business logic
             |
             | IPC calls/events + opaque native handles
             v
KUA bindings
  owns: transport, conversion, validation, handle lifetime,
        GUI-thread dispatch and version-specific native dispatch
             |
             v
Existing KiCad / wxWidgets
  wxWindow / wxDialog / wxFrame / wxNotebook / wxMenu / wxAuiManager
  wxPropertyGrid / wxSizer / TOOL_MANAGER / ACTION_MANAGER
  SCH_COMMIT / BOARD_COMMIT / GAL / VIEW / VIEW_OVERLAY / libraries
```

Each KUA call should map to an existing native method/event or a mechanical sequence needed to invoke it. Plugins decide what to create, when to show it, what it means and how to persist it.

## Status vocabulary

| Label | Meaning |
|---|---|
| Implemented | Present in this prototype and exercised by tests/live smoke. |
| Native behavior exists | KiCad/wxWidgets implements it; KUA needs a binding. |
| Binding gap | Existing behavior is not yet reachable through the exposed IPC surface. |
| Native extension point absent | No corresponding native container/hook exists in the inspected target. |
| Unverified | Needs exact-build or real-editor testing. |

## 1. Binding requirements

- Use a C-compatible/versioned IPC contract with opaque native handles and fixed-width values.
- Never expose raw C++ pointer values or native object layout to external clients.
- Bind handles to editor/document generation; reject after close/reload/project switch.
- Schematic identity includes document, sheet-instance path and item UUID/KIID. PCB identity includes board/document and UUID/KIID.
- Marshal wxWidgets/AUI/GAL/tool operations to the owning editor GUI thread.
- Preserve native ownership, focus, modal scope, event routing, selection, snapping, undo and cancel.
- Each plugin owns its own windows, pages, menus, panes and property content.
- Do not add a shared workspace, page/provider registry, plugin metadata model, workflow manager, job framework or feature-specific widgets.

## 2. Existing KUA API slice

| Existing call/field | Behavior | Status |
|---|---|---|
| `abi_version`, `struct_size` | ABI negotiation | Implemented |
| `host_build_tag`, `editor_kind` | Host/editor identity | Implemented |
| `log(level, message)` | Logging | Implemented |
| `post_to_gui(callback, user_data)` | GUI-thread dispatch | Implemented |
| `overlay_create/destroy/clear` | GAL overlay lifetime | Implemented |
| `overlay_set_style` | Overlay style | Implemented |
| `overlay_line/circle/rectangle` | Initial GAL primitives | Implemented |
| `KUA_EditorContext.frame/canvas/view` | Borrowed editor handles | Implemented |
| extension lifecycle callbacks | Initialize, editor-created, reset and shutdown | Implemented |

## 3. Native windows and controls

Expose wxWidgets windows and controls so plugins can create native subwindows inside KiCad and parent them to the invoking editor.

### Window APIs

- `UI.Window.Create(kind, parent, id, title, position, size, style)`
- `UI.Window.Destroy(handle)`, `UI.Window.Close(handle, force)`
- `UI.Window.Show(handle, visible)`, `UI.Window.Raise(handle)`, `UI.Window.SetFocus(handle)`
- `UI.Window.Enable(handle, enabled)`, `UI.Window.SetTitle(handle, title)`
- `UI.Window.SetSize(handle, rect)`, `UI.Window.SetPosition(handle, point)`
- `UI.Window.GetParent/GetChildren/GetTopLevelParent(handle)`
- `UI.Window.GetName/GetId/GetClassName(handle)`
- `UI.Window.FindChildByName(parent, name, recursive)`
- `UI.Window.FindChildrenByClass(parent, wx_class_name, recursive)`
- `UI.Window.GetState(handle)` (shown, enabled, focused, iconized, geometry)
- `UI.Window.BindEvent/UnbindEvent(handle, event_type, callback_id)`

Supported control kinds should include wxDialog, wxFrame, wxPanel, wxNotebook, wxButton, wxTextCtrl, wxChoice, wxCheckBox, wxStaticText, wxListCtrl, wxTreeCtrl, wxGrid and wxPropertyGrid. Add native wxWidgets control types as required; do not create feature-specific KUA widgets.

A plugin-owned dialog should use the invoking KiCad editor frame as its wxWidgets parent/owner. wxWidgets supplies native platform ownership/transient behavior and window grouping. The plugin owns content, layout, controls, state and close behavior.

### Layout/sizers

- `UI.Sizer.Create(kind, parameters)` (wxBoxSizer, wxFlexGridSizer, wxGridSizer, wxStaticBoxSizer)
- `UI.Sizer.Add/Insert(sizer, window_or_sizer, proportion, flags, border)`
- `UI.Sizer.Detach(sizer, window_or_sizer)`
- `UI.Window.SetSizer(window, sizer)`, `UI.Window.Layout(window)`, `UI.Window.Fit(window)`

Expose common native control methods such as text get/set, button label/enabled, choice items/selection, checkbox value, list/tree items, grid cells and event binding as thin wrappers around wxWidgets.

## 4. Notebook/page operations

Expose wxNotebook directly:

- `UI.Notebook.GetPageCount/GetPage/GetSelection(notebook, ...)`
- `UI.Notebook.SetSelection/ChangeSelection(notebook, index)`
- `UI.Notebook.AddPage(notebook, page_window, label, select, image_id)`
- `UI.Notebook.InsertPage(notebook, index, page_window, label, select, image_id)`
- `UI.Notebook.RemovePage/DeletePage(notebook, index)`
- `UI.Notebook.SetPageText/GetPageText(notebook, index, label)`
- `UI.Notebook.SetPageImage(notebook, index, image_id)`
- `UI.Notebook.BindEvent(notebook, event_type, callback_id)`

### Verified notebook containers in KiCad

In pinned KiCad source `b7066f886375b186ca8fa469de3a6382f6bba400`:

- Eeschema `DIALOG_SYMBOL_PROPERTIES_BASE` contains protected `wxNotebook* m_notebook1`. Existing pages include General, Pin Functions and Pin Map; the derived dialog may add Embedded Files.
- PCB Editor `DIALOG_FOOTPRINT_PROPERTIES_BASE` contains protected `wxNotebook* m_NoteBook`. Existing pages include General and Clearance Overrides & Pad Connections; the derived dialog adds 3D Models and Embedded Files.

The notebook fields are protected, but each notebook is an ordinary child in the dialog's wxWindow tree. A plugin can enumerate descendants, inspect runtime class/page labels, create a wxPanel parented to the matching notebook, and call AddPage/InsertPage. This is direct wxWindow/wxNotebook behavior, not a KUA page registry or a new KiCad feature.

The native page-changing handlers commit pending built-in grid edits. Plugin controls are not automatically persisted by KiCad's TransferDataFromWindow; the plugin must own its page validation and write its values through the relevant object/commit calls. Select the correct notebook by dialog type and page labels, never by blindly choosing the first notebook in an arbitrary window.

## 5. Menu and toolbar operations

Expose wxMenuBar/wxMenu methods and events so plugins can insert their own menu entries:

- `UI.MenuBar.Get(frame)`
- `UI.MenuBar.GetMenuCount/GetMenu(menu_bar, index)`
- `UI.MenuBar.Append/Insert(menu_bar, menu, title)`
- `UI.MenuBar.Remove/Replace(menu_bar, index, menu, title)`
- `UI.MenuBar.SetMenuLabel/GetMenuLabel(menu_bar, index, title)`
- `UI.Menu.Create()`
- `UI.Menu.Append(menu, id, label, help, kind)`
- `UI.Menu.AppendSeparator(menu)`, `UI.Menu.AppendSubMenu(menu, submenu, label, help)`
- `UI.Menu.Insert(menu, index, id, label, help, kind)`
- `UI.Menu.Remove/Delete(menu, id)`, `UI.Menu.Enable/Check(menu, id, state)`
- `UI.Menu.SetLabel(menu, id, label)`
- `UI.Menu.BindEvent/UnbindEvent(frame, menu_id, callback_id)`

Use unique IDs. Plugins remove their own entries and unbind their own handlers when unloading.

Expose existing ACTION_MANAGER/toolbar methods where available: list/run registered actions, get a toolbar, add/delete/enable tool items and bind their native command events. Do not implement a new KUA action or hotkey framework.

## 6. AUI docking

Expose wxAuiManager and wxAuiPaneInfo directly:

- `UI.Aui.GetManager(window)`, `UI.Aui.GetManagedWindow(manager)`
- `UI.Aui.GetPane/GetAllPanes(manager, window_or_name)`
- `UI.Aui.AddPane(manager, window, pane_info)`
- `UI.Aui.InsertPane(manager, window, pane_info, insert_level)`
- `UI.Aui.DetachPane(manager, window)`, `UI.Aui.Update(manager)`
- `UI.Aui.SavePerspective/LoadPerspective(manager, perspective, update)`
- `UI.Aui.SavePaneInfo/LoadPaneInfo(...)`
- `UI.AuiPaneInfo.Get/Set(pane, property, value)`

Expose native pane properties: name, caption, left/right/top/bottom, center, floating position/size, best/min/max size, layer, row, position, show/hide, close/pin buttons, movable, resizable, dockable, toolbar and border flags.

wxWidgets `wxAuiManager::GetManager(wxWindow*)` accepts the managed frame or any child/grandchild. The pinned KiCad source calls it from a child pane. This avoids an Eeschema-only AUI accessor. Plugins create their own wxPanel/window, add it to the existing manager and choose pane settings. Never call UnInit on KiCad's manager.

## 7. Existing Properties panel and property-grid operations

Two native controls must be distinguished.

### Persistent side Properties panel

Pinned KiCad headers expose `EDA_DRAW_FRAME::GetPropertiesPanel()`, `PROPERTIES_PANEL::GetPropertyGrid()` and `PROPERTIES_PANEL::UpdateData()`. Bind these plus native wxPropertyGrid methods:

- `Properties.GetPanel/GetGrid/UpdateData(...)`
- `PropertyGrid.Append/Insert/AppendIn(grid, property_spec)`
- `PropertyGrid.DeleteProperty/Clear(grid, name)`
- `PropertyGrid.GetValue/SetValue(grid, name, value)`
- `PropertyGrid.SetAttribute(grid, name, attribute, value)`
- `PropertyGrid.BindEvent(grid, event_type, callback_id)`

This side panel contains one wxPropertyGrid, not a notebook. Plugins add their own rows/categories, define values/editors and refresh them when native selection/grid content changes.

### Modal Symbol/Footprint Properties

The modal Eeschema Symbol Properties and PCB Footprint Properties dialogs already contain the wxNotebook controls described above. Plugins add an owned page through native wxNotebook calls. No KUA-managed provider registry is needed.

## 8. Existing editor/document/object operations

Expose KiCad accessors through typed requests and opaque handles.

### Editor/document
- `Editor.GetContext/GetActiveDocument/GetActiveTool/Activate()`
- `Document.GetIdentity/GetPath/GetKind/GetProject/GetModifiedState(document)`
- `Document.Save/Reload/Close(document, policy)`
- `Document.OpenInEditor(path, editor_kind)`
- `Document.GetItems(document, kind, filter)`
- `Document.GetItemById(document, uuid, sheet_path)`

### Common objects
- `Object.GetKind/GetUuid/GetParent/GetPosition/GetOrientation/GetBoundingBox/GetProperties(object)`
- `Object.GetField/GetFields(object)`
- `Object.SetField/SetFields(object, fields, commit)`
- `Object.GetGeometry/GetRelatedObjects(object)`

### Schematic
- `Schematic.GetHierarchy/GetSheets/GetSheet(sheet_path)`
- `Schematic.GetSymbols/GetSymbol/GetSymbolPins(sheet_path, uuid)`
- `Schematic.GetLabels/GetWires/GetBuses/GetJunctions/GetNoConnects(sheet_path)`
- `Schematic.GetTextAndGraphics/GetNetForPin/ExportNetlist(...)`
- `Schematic.PlaceSymbol/UpdateSymbolFields/NavigateToSheet(...)`

### PCB
- `Board.GetInfo/GetLayers/GetNets/GetFootprints/GetTracks/GetVias/GetZones/GetGraphics/GetGroups()`
- `Board.GetFootprint/GetPads/GetNetForPad(...)`
- `Board.PlaceFootprint/SetActiveLayer/SetVisibleLayers/Get3DModels(...)`

Selection/navigation:
- `Editor.GetSelection/GetPrimarySelection/ClearSelection/AddToSelection/RemoveFromSelection/SetSelection`
- `Editor.Selection.Subscribe(callback_id)`
- `Editor.FocusItems/ZoomToItems/ZoomToBounds`
- `Editor.CrossProbeToSchematic/CrossProbeToBoard/HighlightNet/ClearHighlight`

Preserve schematic sheet paths and PCB board identity. Do not add a second domain object model beyond typed IPC wrappers.

## 9. Native tools, placement and transactions

Expose existing TOOL_MANAGER/ACTION_MANAGER:
- `Editor.GetToolManager/GetActionManager()`
- `Action.GetRegisteredActions/Run(action_id, parameters)`
- `Tool.Invoke/Cancel/GetState()`
- `Tool.PostAPIAction(action_id, parameters, commit)`
- `Input.GetCursorPosition/GetModifiers/Subscribe(...)`

Eeschema already has `SCH_ACTIONS::move` and `TOOL_MANAGER::PostAPIAction()`. The binding gap is a call that lets a plugin hand an existing SCH_COMMIT and selection to that native action. KiCad's tool remains responsible for cursor, snapping, confirm, Escape and undo. KUA does not implement a move mode.

PCB already exposes `Board.interactive_move()` through IPC; bind it unchanged.

Expose native commits:
- `Schematic.BeginCommit`, `SCH_COMMIT.Stage/Push/Revert/RevertToCheckpoint`
- `Board.BeginCommit`, `BOARD_COMMIT.Stage/Push/Revert/RevertToCheckpoint`

The plugin decides transaction boundaries and workflow.

## 10. GAL/view bindings

Expose existing EDA_DRAW_PANEL_GAL, VIEW and VIEW_OVERLAY:
- `Canvas.GetView()`
- `View.GetWorldBounds/GetScale/GetCenter/GetPixelSize/ToWorld/ToScreen/Refresh`
- `View.MakeOverlay/Remove/SetLayerVisible/SetLayerDisplayOnly`
- `Overlay.Clear/Line/Segment/Circle/Arc/Rectangle/Polygon/Polyline/BitmapText`
- `Overlay.SetStrokeColor/SetFillColor/SetLineWidth/SetIsFill/SetIsStroke`

Coordinate units must be explicit (KiCad IU, mm or screen pixels). Overlays are view-owned transient drawing; plugins decide what and when to draw.

## 11. Libraries and native analysis

Expose existing APIs as required:
- `Project.GetInfo/GetPathVariables/GetSettings`
- `Library.GetSymbolLibraryTable/GetFootprintLibraryTable/FindSymbol/FindFootprint/LoadSymbol/LoadFootprint/Register/Remove/Reload/SaveSymbol/SaveFootprint/Get3DModels`
- `Schematic.RunERC/GetERCReport/ExportNetlist`
- `Board.RunDRC/GetDRCReport`

Do not implement JLCPCB catalogue services, Clippy rules/diagnostics or Blocks library policy in KUA.

## 12. Ownership: what remains in plugins

| Concern | Owner |
|---|---|
| Block library/browser, facade generation, variants/profiles/parameters, publish and PCB layout workflow | kicad-blocks |
| LCSC/JLCPCB/EasyEDA calls, caches, matching, supplier field semantics, stock/price UI, CAD attribution/import | kicad-jlcpcb-interface |
| Clippy parsers/rules, intent schema, diagnostics, finding-to-geometry mapping, fix planner and verification policy | kicad-clippy |
| Which windows/dialogs/controls/pages/menus/panes to create and their contents | Each plugin |
| Pane names/configuration and perspective persistence | Each plugin |
| Menu items, labels, ordering and callbacks | Each plugin |
| Property rows/pages, validation and persistence | Each plugin |
| IPC transport, native handle safety, GUI dispatch and native method invocation | KUA |

## 13. Implementation sequence

1. Safe editor/frame/window handles and lifecycle invalidation.
2. wxWindow create/parent/child enumeration, sizers and control methods.
3. wxNotebook child discovery and AddPage/InsertPage/RemovePage.
4. wxMenuBar/wxMenu operations and event binding.
5. wxAuiManager GetManager/AddPane/InsertPane/DetachPane/Update/perspective.
6. EDA_DRAW_FRAME::GetPropertiesPanel, PROPERTIES_PANEL::GetPropertyGrid and wxPropertyGrid methods.
7. Native document/object/fields/selection/navigation calls.
8. TOOL_MANAGER/ACTION_MANAGER and Eeschema move commit handoff; reuse PCB interactive_move.
9. Remaining GAL, library and ERC/DRC bindings.
10. Verify each call against supported KiCad builds/platforms.

No shared workspace, plugin page registry, host property-provider system, metadata model, feature workflow engine, diagnostics/fix engine or central job framework is in scope.

## Source references

- [EDA_DRAW_FRAME](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/eda_draw_frame.h)
- [PROPERTIES_PANEL](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/common/widgets/properties_panel.h)
- [Symbol Properties notebook](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/dialogs/dialog_symbol_properties_base.h)
- [Footprint Properties notebook](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/pcbnew/dialogs/dialog_footprint_properties_base.h)
- [TOOL_MANAGER](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/tool/tool_manager.h)
- [VIEW](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view.h)
- [VIEW_OVERLAY](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/include/view/view_overlay.h)
- [SCH_COMMIT](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/eeschema/sch_commit.h)
- [BOARD_COMMIT](https://github.com/KiCad/kicad-source-mirror/blob/b7066f886375b186ca8fa469de3a6382f6bba400/pcbnew/board_commit.h)
