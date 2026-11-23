# Native API requirements from the three projects

This matrix was derived from the current main branches of:
- https://github.com/Uhrendoktor/kicad-blocks
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface
- https://github.com/Uhrendoktor/kicad-clippy

## 1. Capability inventory

| Capability | kicad-blocks | kicad-jlcpcb-interface | kicad-clippy |
|---|---|---|---|
| Schematic object model | Facade symbols, hierarchy, pins, sheet paths, UUID selection | Symbol instances, pin maps, fields, hierarchy | Rule targets, symbol/pin/wire geometry, sheet paths |
| PCB object model | Import and position stored layouts, groups and footprints | Footprint previews and optional 3D assets | Pads, footprints, tracks, vias, zones, DRC targets |
| Dockable workspace | Library browser, implementation/variant, configuration, publish | Component overview and catalogue workspace | Diagnostics, rule filters, fix details |
| Shared tab host | Library / instance / layout / publishing tabs | Catalogue / symbol / footprint / 3D tabs | Findings / explanation / fix preview |
| Contextual property pages | Block instance metadata and variant selector | LCSC, MPN, package and supplier data | Component intent, role and lint-specific metadata |
| Cursor-attached schematic placement | Required for Place Block; temporary center placement is current fallback | Required for Add Component; current fallback is page-center placement | Useful for fix previews and suggested geometry |
| Native interactive move | Needs Schematic equivalent of Board.interactive_move | Needs same Eeschema move handoff | Needed for safe position/layout fixes |
| Grid/snap integration | Must use native schematic snap and move tools | Must use native schematic snap and move tools | Highlights and proposed positions must share KiCad coordinates |
| GAL overlays | Visual block interfaces and pin/layout previews | CAD preview context, not the primary need | Core requirement: point, segment, shape, object highlights |
| Mouse pass-through | Normal editor selection must keep working under passive overlays | Normal editor selection | Passive highlight, hover tooltips, click targets |
| Interactive canvas hints | Placement and block affordances | Optional inline part hints | Hover explanations and click-to-fix markers |
| Undo/transactions | Place/move/publish should not split undo semantics | Add/update symbol should use KiCad commits | Applying fixes must be one reviewable undoable edit |
| Cross-project/persistence | Block library and per-instance configuration | Project symbol libraries and fields | Project profile, component intent, rule policy |

## 2. Requirements from KiCad Blocks

Current product workflows include:
- Create a reusable block from a selected hierarchical sheet.
- Browse a shared block library and preview the facade.
- Place a facade in the active schematic.
- Configure an instance's implementation variant, profile and typed parameters.
- Edit facade pin geometry and publish revisions.
- Open a block implementation and validate the block hierarchy.
- Create, store and apply PCB layouts; native PCB item movement is available through Board.interactive_move().

The important unresolved editor operation is cursor-attached schematic placement. The repository explicitly rejects global keyboard injection and platform-specific mouse automation. Its proposal is to create the symbol inside an API commit, set selection/reference point, and transfer the commit to SCH_ACTIONS::move through TOOL_MANAGER::PostAPIAction(). Confirm commits one undo step; Escape cancels/rolls back.

Relevant sources:
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/AGENTS.md
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/explanation/compatibility.md
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/ui-inventory.md
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/docs/upstream/kicad-schematic-interactive-move-proposal.md
- https://github.com/Uhrendoktor/kicad-blocks/blob/main/plugin/kicad-blocks/kicad_blocks/pcb_placement.py

## 3. Requirements from JLCPCB Interface

Current workflows include:
- Project-wide component overview for LCSC-linked symbols.
- Per-component supplier properties, pricing, availability and MPN matches.
- Add/update catalogue fields on an existing selected symbol without changing library identity, graphics or pin mapping.
- Add mode creates a project-local symbol copy and selects it in Eeschema.
- Tabbed preview workspace for symbol, footprint and STEP/3D model.
- Select linked symbols in the schematic and navigate to their sheet/viewport location.

The existing add workflow inserts at the sheet center, focuses and selects the symbol. The native API should allow the plugin to request placement through Eeschema's existing move tool instead.

A shared Properties/Inspector provider should expose supplier fields and catalogue data for the selected symbol. It should not create a separate top-level properties window for every selected object.

Relevant sources:
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/README.md
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/docs/user-guide.md
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/plugin/kicad-jlcpcb-interface/kicad_jlcpcb/plugin.py
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/plugin/kicad-jlcpcb-interface/kicad_jlcpcb/placement.py
- https://github.com/Uhrendoktor/kicad-jlcpcb-interface/blob/main/plugin/kicad-jlcpcb-interface/kicad_jlcpcb/selection.py

## 4. Requirements from kicad-clippy

The engine already emits stable rule IDs, severity, summary, explanation/help, source path and byte span, sheet path, schematic coordinates, related UUIDs and related locations. It has schematic and PCB rules, but the visual integration needs a richer target geometry contract.

Required target types:
- point;
- line segment/polyline;
- rectangle/bounding box;
- circle/arc;
- polygon/path;
- object UUID;
- collection of related objects;
- hierarchical sheet path and board-document identity.

The same diagnostic can have multiple targets, such as a wire and the obstacle it violates. The renderer should not infer geometry from message text.

Required user experience:
- passive highlights that never intercept mouse or keyboard;
- hover highlights with a native tooltip;
- interactive markers that can consume click and keyboard focus;
- a shared dockable findings page with severity/rule filters;
- related-object navigation;
- fix preview, apply, ignore/suppress and re-run;
- component-intent property editing, including decoupling target IC/pin, capacitor target/return terminals, return net and distance constraints.

Auto-fixes must be separate from diagnostics and use preconditions, an undoable KiCad commit, and post-edit revalidation.

Relevant sources:
- https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/architecture.md
- https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/component-metadata.md
- https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/rules.md
- https://github.com/Uhrendoktor/kicad-clippy/blob/main/src/core/diagnostic.rs
- https://github.com/Uhrendoktor/kicad-clippy/blob/main/docs/roadmap.md

## 5. Shared native API shape

The first stable surface should be capability-oriented, not expose raw KiCad internals everywhere.

### Editor lifecycle and context
- editor kind, build identity, process identity;
- attach/detach callbacks for Eeschema and PCB Editor;
- active frame, document, sheet path and selection;
- GUI-thread scheduling;
- version-specific C++ adapter behind a stable C ABI.

### GAL overlay service
- create/update/remove transient overlay groups;
- point/line/rectangle/polygon/arc/text primitives;
- document-mm to view coordinates;
- z-order, severity palette and visibility;
- display-only/pass-through mode;
- independent overlay hit testing.

### Interaction service
- passive, hover and interactive modes;
- event propagation/consumption;
- hover tooltip and keyboard focus;
- cursor/selection query;
- native tool/action invocation;
- start/commit/cancel undoable transactions.

### Docking and shared property system
- one shared dockable host pane per editor, with one notebook/tab strip;
- editor-owned native wxDialogs and modeless tool windows, parented to the invoking editor so native window ownership/transient grouping, focus and close lifetime match KiCad;
- tabs contributed by plugin ID, with priority, object applicability predicate, page factory and lifecycle callbacks;
- contextual providers for selected symbol, sheet, pin, footprint, pad, track, via, zone, board item or schematic item;
- selection/document/project notifications and provider refresh/disposal callbacks;
- provider-owned page content but host-owned docking, floating, theme, focus, window group, persistence and selection context;
- native custom-field/property CRUD through undoable transactions.

This should initially be a Clippy Workspace/Inspector pane that can host Blocks, JLCPCB and Clippy tabs. The inspected `PROPERTIES_PANEL` / `SCH_PROPERTIES_PANEL` / `PCB_PROPERTIES_PANEL` are single property grids and expose no third-party page registry. A true tab injected into KiCad's built-in Symbol/Footprint Properties UI needs a KiCad-side provider hook and is a separate, unimplemented integration gate.

The current pinned headers also expose AUI asymmetrically: PCB Editor has a public `GetAuiManager()`; Eeschema's base frame stores its manager in a protected `m_auimgr` with no public getter. A KUA host needs a narrow Eeschema adapter/upstream accessor rather than unsafe member access.

### Object and placement services
- selection by UUID and hierarchy path;
- focus/navigate to object;
- native snap/grid access;
- create/place symbol/footprint;
- interactive move handoff;
- board interactive move;
- transactions and undo/redo.

### Window grouping and native UI ownership

All three products currently create plugin-owned UI. Their Tk top-level windows do not become reliable owned windows of the KiCad editor across process boundaries. The native host needs a window manager API for editor-owned dialogs, modeless tool windows, activation/focus, modal scope, close lifecycle, DPI/monitor state and native file dialogs. Use the invoking Eeschema/PCB frame as the default owner. Docked pages must use the editor's existing AUI manager and remain part of the same editor window group when floating.

### Property extension levels

| Extension level | Desired behavior | Current feasibility |
|---|---|---|
| KiCad custom fields | Persist supplier, block and component-intent values in native symbol/footprint fields | Existing data model; host CRUD and safe transactions needed |
| Shared context inspector | Plugin pages update from current selection; one tab host shared by all plugins | KUA workspace and provider registry needed; recommended first |
| Native built-in property window tabs | Add tabs/providers inside built-in Symbol Properties and Footprint Properties | Not present in inspected `PROPERTIES_PANEL`; needs KiCad-side provider hook |
| Native property-grid contribution | Add plugin-defined categories/rows to the existing single property grid | Needs an explicit, selection-safe extension hook; do not emulate with repeated rebuilds |

### General API call catalogue

The full proposed method list is in [api-capability-catalog.md](api-capability-catalog.md). It covers lifecycle/session, windows and docking, workspace pages, property providers, documents/objects, schematic and PCB models, selection/navigation, libraries, overlays/coordinates, tools/input, transactions, diagnostics/fixes, ERC/DRC/netlists, background tasks, settings and file dialogs.

## 6. Priority

P0:
1. KIFACE proxy load/forwarding on three OSes.
2. Editor lifecycle hooks and native extension discovery.
3. Exact-build compatibility gate and reversible installer.

P1:
4. GAL view discovery and transient VIEW_OVERLAY drawing.
5. Passive/hover/interactive event routing.
6. Eeschema native move handoff with real snapping and undo/cancel.
7. Shared dockable pane and tab-provider registry.

P2:
8. Selected-object property provider registry.
9. Block, JLCPCB and Clippy pages.
10. Fix planner, preview and apply.
11. Full KiCad API surface inventory and generated export/symbol map per release.
