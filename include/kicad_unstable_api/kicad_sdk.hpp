#pragma once

/*
 * Convenience umbrella for native extensions built against an exact KiCad
 * source/build revision. This is not a stable SDK: build flags, generated
 * headers, compiler, wxWidgets ABI and shared-library exports must match the
 * KiCad process into which the extension is loaded.
 *
 * Include more specialized editor headers only in editor-specific adapters.
 */
#include <kiway.h>
#include <eda_base_frame.h>
#include <eda_draw_frame.h>
#include <class_draw_panel_gal.h>
#include <view/view.h>
#include <view/view_overlay.h>
#include <tool/tool_manager.h>
#include <wx/aui/aui.h>
#include <wx/notebook.h>

/*
 * Eeschema-specific headers (add these in a schematic adapter):
 *   <sch_edit_frame.h>
 *   <sch_base_frame.h>
 *   <sch_draw_panel.h>
 *   <sch_symbol.h>
 *   <sch_sheet.h>
 *   <sch_commit.h>
 *   <tools/sch_actions.h>
 *   <tools/sch_selection_tool.h>
 *
 * PCB-specific headers (add these in a PCB adapter):
 *   <pcb_edit_frame.h>
 *   <pcb_base_edit_frame.h>
 *   <pcb_draw_panel_gal.h>
 *   <board.h>
 *   <footprint.h>
 *   <pad.h>
 *   <board_commit.h>
 *
 * A declaration in a public header does not guarantee that a non-inline
 * implementation is exported from the binary. See docs/native-api-catalog.md.
 */
