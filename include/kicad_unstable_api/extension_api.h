#ifndef KICAD_UNSTABLE_API_EXTENSION_API_H
#define KICAD_UNSTABLE_API_EXTENSION_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUA_EXTENSION_ABI_V1 1u

typedef enum KUA_EditorKind
{
    KUA_EDITOR_UNKNOWN = 0,
    KUA_EDITOR_SCHEMATIC = 1,
    KUA_EDITOR_PCB = 2
} KUA_EditorKind;

/* on_editor_window_created is emitted only for the primary schematic or PCB editor frame. */
typedef struct KUA_EditorContext
{
    uint32_t struct_size;
    uint32_t editor_kind;
    int32_t class_id;
    void* frame;
    void* program;
    void* kiway;
    /* Borrowed, host-owned handles; valid only while the editor frame remains alive. */
    void* canvas;
    void* view;
} KUA_EditorContext;

typedef void (*KUA_GuiCallback)(void* user_data);
typedef void (*KUA_UiWindowEventCallback)(uint64_t window, uint32_t event_kind,
                                          void* user_data);

typedef enum KUA_UiWindowKind
{
    KUA_UI_WINDOW_PANEL = 1,
    KUA_UI_WINDOW_DIALOG = 2,
    KUA_UI_WINDOW_FRAME = 3,
    KUA_UI_WINDOW_NOTEBOOK = 4,
    KUA_UI_WINDOW_STATIC_TEXT = 5,
    KUA_UI_WINDOW_BUTTON = 6,
    KUA_UI_WINDOW_TEXT_CTRL = 7,
    KUA_UI_WINDOW_CHECK_BOX = 8,
    KUA_UI_WINDOW_CHOICE = 9
} KUA_UiWindowKind;

/*
 * Window notifications are delivered synchronously on the KiCad GUI thread.
 * The handle passed for DESTROYING is valid only during the callback.
 * Observers must return promptly and must not consume or replace wx events.
 */
typedef enum KUA_UiWindowEventKind
{
    KUA_UI_WINDOW_SHOWN = 1,
    KUA_UI_WINDOW_HIDDEN = 2,
    KUA_UI_WINDOW_DESTROYING = 3
} KUA_UiWindowEventKind;

typedef enum KUA_UiSizerKind
{
    KUA_UI_SIZER_BOX = 1,
    KUA_UI_SIZER_FLEX_GRID = 2
} KUA_UiSizerKind;

typedef enum KUA_UiMenuItemKind
{
    KUA_UI_MENU_NORMAL = 0,
    KUA_UI_MENU_CHECK = 1,
    KUA_UI_MENU_RADIO = 2
} KUA_UiMenuItemKind;

typedef enum KUA_AuiDirection
{
    KUA_AUI_LEFT = 0,
    KUA_AUI_RIGHT = 1,
    KUA_AUI_TOP = 2,
    KUA_AUI_BOTTOM = 3,
    KUA_AUI_CENTER = 4,
    KUA_AUI_FLOATING = 5
} KUA_AuiDirection;

enum
{
    KUA_AUI_CLOSE_BUTTON = 1u << 0,
    KUA_AUI_PIN_BUTTON = 1u << 1,
    KUA_AUI_CAPTION_VISIBLE = 1u << 2,
    KUA_AUI_PANE_BORDER = 1u << 3,
    KUA_AUI_MOVABLE = 1u << 4,
    KUA_AUI_RESIZABLE = 1u << 5,
    KUA_AUI_FLOATABLE = 1u << 6,
    KUA_AUI_DOCKABLE = 1u << 7
};

typedef struct KUA_AuiPaneOptions
{
    uint32_t struct_size;
    const char* name;
    const char* caption;
    uint32_t direction;
    int32_t layer;
    int32_t row;
    int32_t position;
    int32_t best_width;
    int32_t best_height;
    int32_t min_width;
    int32_t min_height;
    uint32_t flags;
} KUA_AuiPaneOptions;

/*
 * Overlay coordinates are KiCad world coordinates (internal units), not pixels.
 * Handles are host-owned and must only be used on the editor GUI thread.
 */
typedef struct KUA_HostApi
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* host_build_tag;
    uint32_t editor_kind;
    void* user_data;
    void (*log)(int32_t level, const char* message);
    int32_t (*post_to_gui)(KUA_GuiCallback callback, void* user_data);
    /* Appended ABI v1 capabilities; check struct_size before reading these fields. */
    uint64_t (*overlay_create)(void* frame, void* canvas, void* view);
    int32_t (*overlay_destroy)(uint64_t overlay);
    int32_t (*overlay_clear)(uint64_t overlay);
    int32_t (*overlay_set_style)(uint64_t overlay,
                                 double stroke_r, double stroke_g, double stroke_b, double stroke_a,
                                 double fill_r, double fill_g, double fill_b, double fill_a,
                                 double line_width, int32_t stroke_enabled, int32_t fill_enabled);
    int32_t (*overlay_line)(uint64_t overlay, double x1, double y1, double x2, double y2);
    int32_t (*overlay_circle)(uint64_t overlay, double cx, double cy, double radius);
    int32_t (*overlay_rectangle)(uint64_t overlay, double x1, double y1, double x2, double y2);

    /* Native wxWidgets/KiCad UI bindings. Opaque handles are GUI-thread-only. */
    /* Wraps a borrowed wxWindow owned by one of the registered KiCad editor frames. */
    uint64_t (*ui_window_get_handle)(void* borrowed_native_window);
    uint64_t (*ui_window_create)(uint64_t parent, uint32_t kind, int32_t id,
                                 const char* text, int32_t style);
    int32_t (*ui_window_destroy)(uint64_t window);
    int32_t (*ui_window_show)(uint64_t window, int32_t show);
    int32_t (*ui_window_get_children)(uint64_t parent, uint64_t* handles, uint32_t capacity);
    int32_t (*ui_window_get_class_name)(uint64_t window, char* buffer, uint32_t capacity);
    int32_t (*ui_window_set_text)(uint64_t window, const char* text);
    uint64_t (*ui_sizer_create)(uint32_t kind, int32_t orientation,
                                int32_t rows, int32_t cols, int32_t vgap, int32_t hgap);
    int32_t (*ui_sizer_add)(uint64_t sizer, uint64_t child, int32_t proportion,
                            int32_t flags, int32_t border);
    int32_t (*ui_window_set_sizer)(uint64_t window, uint64_t sizer);
    int32_t (*ui_window_layout)(uint64_t window);

    int32_t (*ui_notebook_add_page)(uint64_t notebook, uint64_t page,
                                    const char* label, int32_t select);
    int32_t (*ui_notebook_insert_page)(uint64_t notebook, uint32_t index, uint64_t page,
                                       const char* label, int32_t select);
    int32_t (*ui_notebook_remove_page)(uint64_t notebook, uint32_t index);
    int32_t (*ui_notebook_get_page_count)(uint64_t notebook);
    int32_t (*ui_notebook_set_selection)(uint64_t notebook, int32_t index);
    int32_t (*ui_notebook_get_page_text)(uint64_t notebook, uint32_t index,
                                         char* buffer, uint32_t capacity);

    uint64_t (*ui_menu_bar_get)(uint64_t frame);
    uint64_t (*ui_menu_create)(uint64_t frame);
    int32_t (*ui_menu_append)(uint64_t menu, int32_t id, const char* label,
                              const char* help, uint32_t kind);
    int32_t (*ui_menu_delete)(uint64_t menu, int32_t id);
    int32_t (*ui_menu_bar_append)(uint64_t menu_bar, uint64_t menu, const char* title);
    int32_t (*ui_menu_bar_remove)(uint64_t menu_bar, uint32_t index);
    uint64_t (*ui_menu_bind)(uint64_t frame, int32_t id,
                             KUA_GuiCallback callback, void* user_data);
    int32_t (*ui_menu_unbind)(uint64_t binding);

    uint64_t (*ui_aui_get_manager)(uint64_t window);
    int32_t (*ui_aui_add_pane)(uint64_t manager, uint64_t window,
                               const KUA_AuiPaneOptions* options);
    int32_t (*ui_aui_detach_pane)(uint64_t manager, uint64_t window);
    int32_t (*ui_aui_update)(uint64_t manager);

    uint64_t (*ui_properties_get_panel)(uint64_t editor_frame);
    uint64_t (*ui_properties_get_grid)(uint64_t properties_panel);
    int32_t (*ui_property_grid_append_string)(uint64_t grid, const char* label,
                                              const char* name, const char* value);
    int32_t (*ui_property_grid_append_bool)(uint64_t grid, const char* label,
                                            const char* name, int32_t value);
    int32_t (*ui_property_grid_append_int)(uint64_t grid, const char* label,
                                           const char* name, int64_t value);
    int32_t (*ui_property_grid_append_float)(uint64_t grid, const char* label,
                                             const char* name, double value);
    int32_t (*ui_property_grid_set_string)(uint64_t grid, const char* name, const char* value);
    int32_t (*ui_property_grid_get_string)(uint64_t grid, const char* name,
                                           char* buffer, uint32_t capacity);
    int32_t (*ui_property_grid_delete)(uint64_t grid, const char* name);

    /* Appended v1 extensions: keep all earlier KUA_HostApi offsets stable. */
    /* Top-level windows owned by this editor and lifecycle notifications for native dialogs. */
    /* Returns the total count; fills up to capacity handles when handles is non-null. */
    int32_t (*ui_window_get_top_level_windows)(uint64_t editor_frame,
                                               uint64_t* handles, uint32_t capacity);
    /* Observe top-level/child windows whose parent chain reaches editor_frame. */
    uint64_t (*ui_window_observe)(uint64_t editor_frame,
                                  KUA_UiWindowEventCallback callback, void* user_data);
    int32_t (*ui_window_unobserve)(uint64_t observer);

    /* Appended native-control and common wxWindow operations; earlier offsets stay fixed. */
    /* Each control operation rejects a stale handle or a handle of the wrong wx class. */
    /* Text getters write UTF-8, NUL-terminated when capacity > 0; success is 1, failure -1. */
    int32_t (*ui_text_ctrl_set_value)(uint64_t text_ctrl, const char* value);
    int32_t (*ui_text_ctrl_get_value)(uint64_t text_ctrl, char* buffer, uint32_t capacity);
    /* Checkbox setter treats zero as unchecked and any nonzero value as checked. */
    /* Checkbox getter returns 0/1, or -1 for a stale/wrong-type handle. */
    int32_t (*ui_check_box_set_value)(uint64_t check_box, int32_t checked);
    int32_t (*ui_check_box_get_value)(uint64_t check_box);
    /* Choice append/clear return 1 on success and -1 on failure. */
    int32_t (*ui_choice_append)(uint64_t choice, const char* item);
    int32_t (*ui_choice_clear)(uint64_t choice);
    int32_t (*ui_choice_get_count)(uint64_t choice);
    /* Choice selection accepts -1 (wxNOT_FOUND) to clear the current selection. */
    int32_t (*ui_choice_set_selection)(uint64_t choice, int32_t selection);
    /* Returns selected index; -1 means no selection or an invalid handle/type. */
    int32_t (*ui_choice_get_selection)(uint64_t choice);
    /* Writes UTF-8 and NUL-terminates; no selection produces an empty string. */
    int32_t (*ui_choice_get_string_selection)(uint64_t choice,
                                              char* buffer, uint32_t capacity);
    /* Common window operations return 1 for a valid handle, -1 on failure. */
    int32_t (*ui_window_enable)(uint64_t window, int32_t enabled);
    int32_t (*ui_window_raise)(uint64_t window);
    int32_t (*ui_window_set_focus)(uint64_t window);
    int32_t (*ui_window_set_size)(uint64_t window, int32_t width, int32_t height);
    int32_t (*ui_window_get_size)(uint64_t window, int32_t* width, int32_t* height);
    int32_t (*ui_window_set_position)(uint64_t window, int32_t x, int32_t y);
    int32_t (*ui_window_get_position)(uint64_t window, int32_t* x, int32_t* y);

    /* Native wxMenuBar and wxMenu composition/state operations. */
    int32_t (*ui_menu_bar_get_menu_count)(uint64_t menu_bar);
    uint64_t (*ui_menu_bar_get_menu)(uint64_t menu_bar, uint32_t index);
    int32_t (*ui_menu_bar_insert)(uint64_t menu_bar, uint32_t index,
                                  uint64_t menu, const char* title);
    int32_t (*ui_menu_bar_set_label)(uint64_t menu_bar, uint32_t index,
                                     const char* title);
    int32_t (*ui_menu_bar_get_label)(uint64_t menu_bar, uint32_t index,
                                     char* buffer, uint32_t capacity);
    int32_t (*ui_menu_get_item_count)(uint64_t menu);
    int32_t (*ui_menu_insert)(uint64_t menu, uint32_t index, int32_t id,
                              const char* label, const char* help, uint32_t kind);
    int32_t (*ui_menu_append_separator)(uint64_t menu);
    int32_t (*ui_menu_insert_separator)(uint64_t menu, uint32_t index);
    int32_t (*ui_menu_enable)(uint64_t menu, int32_t id, int32_t enabled);
    int32_t (*ui_menu_check)(uint64_t menu, int32_t id, int32_t checked);
    int32_t (*ui_menu_is_enabled)(uint64_t menu, int32_t id);
    int32_t (*ui_menu_is_checked)(uint64_t menu, int32_t id);
    int32_t (*ui_menu_set_label)(uint64_t menu, int32_t id, const char* label);
    int32_t (*ui_menu_get_label)(uint64_t menu, int32_t id,
                                 char* buffer, uint32_t capacity);
} KUA_HostApi;

typedef struct KUA_ExtensionApi
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* extension_id;
    int32_t (*initialize)(const KUA_HostApi* host);
    void (*on_editor_window_created)(const KUA_EditorContext* context);
    void (*on_editor_reset)(uint32_t editor_kind);
    void (*shutdown)(void);
} KUA_ExtensionApi;

typedef const KUA_ExtensionApi* (*KUA_GetExtensionApi)(uint32_t requested_abi);

#if defined(_WIN32)
#  define KUA_EXTENSION_EXPORT __declspec(dllexport)
#else
#  define KUA_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

/*
 * Every extension module exports:
 *
 *   extern "C" KUA_EXTENSION_EXPORT
 *   const KUA_ExtensionApi* KUA_GetExtension(uint32_t requested_abi);
 *
 * ABI structs contain only fixed-width integers, C strings, opaque pointers and
 * C function pointers. Never pass wxWidgets or KiCad C++ objects by value
 * across this boundary.
 */

#ifdef __cplusplus
}
#endif

#endif
