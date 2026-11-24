# Native KiCad binding catalogue

## Scope and boundary

The current prototype exposes existing KiCad and wxWidgets behavior through `KUA_HostApi`, an in-process C ABI consumed by native extension modules loaded into KiCad. It does not implement an external IPC/RPC server/client or wire transport. This catalogue describes the native binding surface; an eventual out-of-process transport and typed conversion layer are separate future work.

KUA does not implement Blocks, JLCPCB or Clippy features, workflows, business logic, metadata schemas or plugin UI.

```
Native extension module (inside KiCad)
  owns: windows, layout, pages, menus, panes, metadata, workflows,
        persistence, event interpretation and business logic
             |
             | KUA_HostApi calls + opaque native handles (in process)
             v
KUA native bindings
  owns: ABI validation, handle lifetime, GUI-thread dispatch,
        conversion at the native boundary and version-specific invocation
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
| Binding gap | Existing behavior is not yet reachable through the exposed native extension ABI. |
| Native extension point absent | No corresponding native container/hook exists in the inspected target. |
| Unverified | Needs exact-build or real-editor testing. |

## 1. Binding requirements

- The current native extension boundary is a C-compatible/versioned in-process ABI with opaque native handles and fixed-width values. Any future IPC/RPC wire contract must be separately versioned and must not expose pointers or native object layout.
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
| `ui_window_get_top_level_windows` | Enumerate top-level windows owned by an editor frame | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| `ui_window_observe/unobserve` | Observe native window show/hide/destroy events without consuming KiCad events | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| native window destroy tracking | Invalidate window and owned sizer/property handles when the wxWindow is destroyed | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| precise menu callback unbind | Remove only KUA's own `wxEVT_MENU` handler for its ID | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| menu-bar insert/list/label | Query and insert top-level menus and read/change their titles | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| menu item insert/separator/state/label | Insert normal/check/radio items, separators, enable/check them and read/write labels | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| KUA menu ownership/disposal | Explicitly destroy detached KUA menus; reject attached/borrowed handles; reclaim detached menus when their editor-owned window is destroyed and defer attached menus to wxMenuBar ownership | Implemented; exact-head live-editor smoke passed with `menu_owner_destroy=1` and `menu_lifetime=1` in both editors (run 45491972521). Process-level `KIFACE_PROXY::OnKifaceEnd` cleanup is implemented but not directly smoke-tested because the harness terminates KiCad with SIGTERM. |
| `ui_text_ctrl_*` | Read/write native `wxTextCtrl` values | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| `ui_check_box_*` | Read/write native `wxCheckBox` state | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| `ui_choice_*` | Append/clear items, count, select and read selected text | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| `ui_window_enable/raise/set_focus` | Direct native window enable, raise and focus calls | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| `ui_window_set/get_size/position` | Read/write native window geometry | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| AUI manager owner tracking | Invalidate manager handles with `wxAuiManager::GetManagedWindow()` | Implemented; live-editor smoke passed in both Eeschema and PCB Editor (run 45478187892). |
| AUI pane state/insert/visibility/perspective | Return stable pane-state snapshots, insert/reposition via wxAuiManager::InsertPane, show/hide, save/load native perspectives | Implemented; exact-head live-editor smoke passed with `aui_ops=1` in both Eeschema and PCB Editor (run 45488120370). |
| Notebook page lifecycle | Retrieve page handles, query selection, update labels, remove pages without destroying them and explicitly delete pages | Implemented; exact-head Eeschema/PCB Editor smoke passed with `notebook_ops=1` in both editors (run 45484673610). |



### PR #8 exact-build live-editor evidence

The append-only menu-bar/menu-item binding slice was merged in [PR #8](https://github.com/Uhrendoktor/kicad-unstable-api/pull/8) at commit `73606aa16af0631f0f81114281e49186b6f5a1b3`. Exact-head fast validation [45478202553](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45478202553) passed on Ubuntu, Windows and macOS. Package-backed real-editor validation [45478187892](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45478187892) passed against KiCad nightly package `202611231147+b7066f8863~190~ubuntu26.04.1`, which matched pinned source SHA `b7066f886375b186ca8fa469de3a6382f6bba400`.

Both Eeschema (`editor=1`) and PCB Editor (`editor=2`) emitted `native-ui-ready` with `menu_ops=1`, `menu_dispatch=1`, `unbind=1`, `window_ops=1`, `aui_owner_lifetime=1` and `property=1`. The diagnostic artifact is [14961087054](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45478187892/artifacts/14961087054). KiCad and GAL were not compiled in this validation: only the two KUA production proxies (~10.4 seconds) and smoke extension (~1.8 seconds) were compiled. The default three-platform PR matrix is capped at five minutes per platform and never builds KiCad.

### PR #9 notebook page lifecycle evidence

The append-only notebook page operations were merged in [PR #9](https://github.com/Uhrendoktor/kicad-unstable-api/pull/9) at commit `17d9c2a5fb087aa1580d580dc8a53ae106dbbfbb`. Exact-head Linux, Windows and macOS fast validation [run 45484687798](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45484687798) passed. Exact-source live validation [run 45484673610](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45484673610) passed using the archived official KiCad nightly package matching the pinned source revision. Both Eeschema (`editor=1`) and PCB Editor (`editor=2`) emitted `native-ui-ready` with `notebook_ops=1`, `menu_ops=1`, and `menu_dispatch=1`. The smoke proved page lookup/identity, selection, page-label round-trip, remove-without-destroy, reinsertion, delete and stale-handle invalidation. The diagnostics are in [artifact 14963222092](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45484673610/artifacts/14963222092). The full job took 194 seconds end-to-end including prerequisite installation; the KUA proxy compilation step took 15 seconds and the smoke-extension build step 3 seconds. No KiCad or GAL source build was run.

### PR #10 AUI pane state and perspective evidence

[PR #10](https://github.com/Uhrendoktor/kicad-unstable-api/pull/10) merged as commit `b95e4929790d32b3635c6be094a9547e1c9bb148`. The append-only ABI exposes a value snapshot of `wxAuiPaneInfo`, relative `InsertPane` for new or existing windows, pane visibility changes, and native `SavePerspective/LoadPerspective`.

Exact-head Linux, Windows and macOS fast validation [run 45488137860](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45488137860) passed. Exact-source live-editor validation [run 45488120370](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45488120370) passed. Both Eeschema (`editor=1`) and PCB Editor (`editor=2`) emitted `native-ui-ready` with `aui_ops=1`, `notebook_ops=1`, `menu_ops=1` and `menu_dispatch=1`; AUI owner-lifetime, sizer, window/control, property-grid and event binding checks also passed.

The smoke covers `GetPane` state/direction/layer/flags, hide/show, perspective size-query and save/load round-trip, inserting a previously unmanaged window, repositioning an already-managed pane, invalid insert-level rejection, detach/destruction and stale-handle invalidation. The artifact [14964718370](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45488120370/artifacts/14964718370) contains both KIFACE proxies, the smoke extension and the two editor logs. The job completed in 168 seconds; KUA proxy and smoke-extension compilation took 11 and 2 seconds respectively. The exact-source official KiCad nightly package was used; no KiCad or GAL source build was run.

### PR #11 menu ownership and disposal evidence

[PR #11](https://github.com/Uhrendoktor/kicad-unstable-api/pull/11) merged as [commit `a669be9`](https://github.com/Uhrendoktor/kicad-unstable-api/commit/a669be93b31a06abfddfe48ef333afcd3e0d887e). It adds an append-only `ui_menu_destroy` call and tracks whether a menu was created/transferred to KUA or is still attached to its wxMenuBar. Destruction is rejected for attached or borrowed menus. `wxMenuBar::Remove` detaches the menu and transfers ownership back to KUA.

Exact-head Linux, Windows and macOS fast validation [run 45491984432](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45491984432) passed. Exact-source live-editor validation [run 45491972521](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45491972521) passed. Both Eeschema (`editor=1`) and PCB Editor (`editor=2`) emitted `native-ui-ready` with `menu_owner_destroy=1`, `menu_lifetime=1`, `menu_ops=1`, `menu_dispatch=1`, and the existing notebook/AUI/window/control/property-grid tests passing. The smoke covers explicit disposal of an unattached menu, rejection of destroy while attached, Remove then destroy, and destruction of an editor-owned child frame containing one detached and one attached KUA menu. The detached handle is invalidated by KUA's destroy hook, while the attached menu is left to the wxMenuBar destructor. Artifact [14965339877](https://github.com/Uhrendoktor/kicad-unstable-api/actions/runs/45491972521/artifacts/14965339877) contains both production proxies, the smoke extension and editor diagnostics.

The pinned official nightly package version was `202611231147+b7066f8863~190~ubuntu26.04.1`, matching source SHA `b7066f886375b186ca8fa469de3a6382f6bba400`. The live job compiled only the KUA proxies and smoke extension (15 and 3 seconds); it did not compile KiCad or GAL.

Teardown boundary: `destroy_owned_menus_for_shutdown()` is implemented to remove and release KUA-owned menus still attached to a live bar before the proxy clears its state. However, the smoke runner stops the editor with SIGTERM after `native-ui-ready`; it does not exercise graceful process-level `KIFACE_PROXY::OnKifaceEnd`. Keep a dedicated graceful application-exit test in the remaining lifecycle validation scope.

## 3. Native windows and controls

Expose wxWidgets windows and controls so plugins can create native subwindows inside KiCad and parent them to the invoking editor.

### Window APIs

- `UI.Window.Create(kind, parent, id, title, position, size, style)`
- `UI.Window.Destroy(handle)`, `UI.Window.Close(handle, force)`
- `UI.Window.Show(handle, visible)`, `UI.Window.Raise(handle)`, `UI.Window.SetFocus(handle)`
- `UI.Window.Enable(handle, enabled)`, `UI.Window.SetTitle(handle, title)`
- `UI.Window.SetSize(handle, rect)`, `UI.Window.SetPosition(handle, point)`
- `UI.Window.GetTopLevelWindows()` / `UI.Window.GetTopLevelWindows(editor)`
- `UI.Window.GetParent/GetChildren/GetTopLevelParent(handle)`
- `UI.Window.FindTopLevelByClass(wx_class_name)`
- `UI.Window.GetName/GetId/GetClassName(handle)`
- `UI.Window.FindChildByName(parent, name, recursive)`
- `UI.Window.FindChildrenByClass(parent, wx_class_name, recursive)`
- `UI.Window.GetState(handle)` (shown, enabled, focused, iconized, geometry)
- `UI.Window.BindEvent/UnbindEvent(handle, event_type, callback_id)`

The current native binding slice adds `ui_window_get_top_level_windows(editor_frame, handles, capacity)`, which lists top-level wxWindows whose parent chain leads to the given editor. This includes native modal Properties dialogs parented to that editor. It also adds `ui_window_observe(editor_frame, callback, user_data)` / `ui_window_unobserve(observer)`. Observers receive opaque handles and one of `KUA_UI_WINDOW_SHOWN`, `KUA_UI_WINDOW_HIDDEN` or `KUA_UI_WINDOW_DESTROYING`. The observer is a thin `wxEventFilter` over existing wxWidgets events; it always returns `Event_Skip` so KiCad continues processing them normally. Notifications run on the GUI thread. A handle received for `DESTROYING` is valid only during that callback.

Every registered native window has a `wxEVT_DESTROY` cleanup hook. Destroying a window invalidates its window handle and handles for resources owned by that window, including an attached sizer or property grid. Owner-bound native handles are revoked before `DESTROYING` observers run: wxWidgets emits `wxEVT_DESTROY` from the base window destructor, after derived members such as a member-owned `wxAuiManager` have already been destroyed. The temporary window handle remains valid only for the callback; owned manager/sizer/menu handles do not. Destroying the editor invalidates all remaining handles and removes its observers. These lifetime rules apply to native windows destroyed by KiCad as well as windows explicitly destroyed through KUA. `ui_window_get_handle()` can wrap a borrowed native window only when its parent chain belongs to a registered KiCad editor frame.

The initial control operations are direct wxWidgets calls: `ui_text_ctrl_set/get_value`, `ui_check_box_set/get_value`, and `ui_choice_append/clear/get_count/set/get_selection/get_string_selection`. Common window operations expose enable, raise, focus, and size/position get/set. Wrong control classes and stale handles are rejected; these calls do not create KUA-owned widgets. The pinned live smoke round-trips text, checkbox and choice state, then reads back a requested window position and size.

AUI manager handles are associated with `wxAuiManager::GetManagedWindow()`, not merely the editor frame passed by the caller, so destroying a child managed window invalidates the manager handle.

Supported control kinds should include wxDialog, wxFrame, wxPanel, wxNotebook, wxButton, wxTextCtrl, wxChoice, wxCheckBox, wxStaticText, wxListCtrl, wxTreeCtrl, wxGrid and wxPropertyGrid. Add native wxWidgets control types as required; do not create feature-specific KUA widgets.

A plugin-owned dialog should use the invoking KiCad editor frame as its wxWidgets parent/owner. wxWidgets supplies native platform ownership/transient behavior and window grouping. The plugin owns content, layout, controls, state and close behavior.

### Layout/sizers

- `UI.Sizer.Create(kind, parameters)` (wxBoxSizer, wxFlexGridSizer, wxGridSizer, wxStaticBoxSizer)
- `UI.Sizer.Add/Insert(sizer, window_or_sizer, proportion, flags, border)`
- `UI.Sizer.Detach(sizer, window_or_sizer)`
- `UI.Window.SetSizer(window, sizer)`, `UI.Window.Layout(window)`, `UI.Window.Fit(window)`

KUA tracks ownership recursively through nested sizers. Replacing a window's root sizer invalidates all registered handles in the old tree before wxWidgets deletes it; destroying the owning window invalidates the root and nested sizer handles. A registered sizer cannot be attached to more than one owner.

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

Menu-bar bindings expose count/index lookup, insertion, title get/set, append and removal. Menu bindings expose item count, insertion of normal/check/radio items, separator append/insert, enable/check state get/set, and label get/set. Item operations validate IDs and checkability; menu-bar append/insert reject menus created for another editor or menus already attached to a menu bar. All calls execute on the editor GUI thread.

Menu lifetime is explicit: `ui_menu_create` returns a KUA-owned detached menu. Append/Insert transfer it to wxMenuBar ownership. Remove detaches it and transfers ownership back to the KUA handle; extensions should retain the handle returned by Create or obtain it through GetMenu before removal. `ui_menu_destroy` accepts only detached owned menus and rejects attached/borrowed handles. Detached owned menus are reclaimed on editor destruction; KIFACE shutdown removes and destroys KUA-owned menus still attached to a live editor bar. Native KiCad menus returned by GetMenu are borrowed until Remove transfers ownership.

Use unique IDs. Menu callbacks use a per-binding wxEvtHandler object attached to the target frame; dispatch is independent of wxCommandEvent::GetEventObject(), which wxWidgets sets to the source wxMenu. Unbinding removes only the matching handler object and ID, leaving unrelated KiCad or plugin handlers intact. Editor close and extension shutdown disconnect outstanding KUA menu handlers before their callback state is released.

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

wxWidgets `wxAuiManager::GetManager(wxWindow*)` accepts the managed frame or any child/grandchild. The pinned KiCad source calls it from a child pane. This avoids an Eeschema-only AUI accessor. A manager handle is borrowed and tied to `GetManagedWindow()`: it is invalidated before `DESTROYING` callbacks for that window, because a member-owned manager may already have been destructed before `wxEVT_DESTROY` is emitted. Plugins create their own wxPanel/window, add it to the existing manager and choose pane settings. Never call UnInit on KiCad's manager.

## 7. Existing Properties panel and property-grid operations

Two native controls must be distinguished.

### Persistent side Properties panel

Pinned KiCad headers expose `EDA_DRAW_FRAME::GetPropertiesPanel()`, `PROPERTIES_PANEL::GetPropertyGrid()` and `PROPERTIES_PANEL::UpdateData()`. Bind these plus native wxPropertyGrid methods:

- `Properties.GetPanel/GetGrid/UpdateData(...)`
- `PropertyGrid.CreateProperty(kind, label, name, initial_value)` (wxStringProperty, wxIntProperty, wxFloatProperty, wxBoolProperty, wxEnumProperty, wxFileProperty, wxDirProperty, wxDateProperty, wxColourProperty, wxFontProperty, etc.)
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

Preserve schematic sheet paths and PCB board identity. Do not add a second domain object model beyond typed native-extension wrappers.

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
| In-process native extension ABI, native handle safety, GUI dispatch and native method invocation | KUA prototype |
| External IPC/RPC server/client transport and wire conversion | Not implemented |

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
