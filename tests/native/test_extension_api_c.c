#include "kicad_unstable_api/extension_api.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(KUA_EXTENSION_ABI_V1 == 1u, "Unexpected extension ABI version");
_Static_assert(sizeof(((KUA_HostApi*) 0)->abi_version) == sizeof(uint32_t),
               "Host ABI version must be uint32_t");
_Static_assert(offsetof(KUA_HostApi, overlay_create) >
                   offsetof(KUA_HostApi, post_to_gui),
               "Overlay functions must remain appended to the host API");
_Static_assert(offsetof(KUA_HostApi, ui_window_create) >
                   offsetof(KUA_HostApi, overlay_rectangle),
               "Native UI bindings must be appended after overlay calls");
_Static_assert(offsetof(KUA_HostApi, ui_aui_add_pane) >
                   offsetof(KUA_HostApi, ui_menu_unbind),
               "AUI operations must follow menu operations");
_Static_assert(offsetof(KUA_HostApi, ui_window_get_top_level_windows) >
                   offsetof(KUA_HostApi, ui_property_grid_delete),
               "Window lifecycle calls must append after the existing host ABI");
_Static_assert(offsetof(KUA_HostApi, ui_window_observe) >
                   offsetof(KUA_HostApi, ui_window_get_top_level_windows),
               "Window observer registration must follow enumeration");
_Static_assert(offsetof(KUA_HostApi, ui_window_unobserve) >
                   offsetof(KUA_HostApi, ui_window_observe),
               "Window observer teardown must follow observer registration");
_Static_assert(offsetof(KUA_HostApi, ui_text_ctrl_set_value) >
                   offsetof(KUA_HostApi, ui_window_unobserve),
               "Native control operations must append after window lifecycle methods");
_Static_assert(offsetof(KUA_HostApi, ui_check_box_set_value) >
                   offsetof(KUA_HostApi, ui_text_ctrl_get_value),
               "Checkbox methods must follow text-control methods");
_Static_assert(offsetof(KUA_HostApi, ui_choice_append) >
                   offsetof(KUA_HostApi, ui_check_box_get_value),
               "Choice methods must follow checkbox methods");
_Static_assert(offsetof(KUA_HostApi, ui_window_enable) >
                   offsetof(KUA_HostApi, ui_choice_get_string_selection),
               "Common window methods must follow choice methods");
_Static_assert(offsetof(KUA_HostApi, ui_window_get_position) >
                   offsetof(KUA_HostApi, ui_window_set_position),
               "Window geometry methods must remain in append-only order");
_Static_assert(offsetof(KUA_HostApi, ui_menu_bar_get_menu_count) >
                   offsetof(KUA_HostApi, ui_window_get_position),
               "Menu operations must append after the existing window API");
_Static_assert(offsetof(KUA_HostApi, ui_menu_bar_get_menu) >
                   offsetof(KUA_HostApi, ui_menu_bar_get_menu_count),
               "Menu-bar lookup methods must stay ordered");
_Static_assert(offsetof(KUA_HostApi, ui_menu_bar_insert) >
                   offsetof(KUA_HostApi, ui_menu_bar_get_menu),
               "Menu-bar insertion must follow lookup");
_Static_assert(offsetof(KUA_HostApi, ui_menu_bar_set_label) >
                   offsetof(KUA_HostApi, ui_menu_bar_insert),
               "Menu-bar label setter must follow insertion");
_Static_assert(offsetof(KUA_HostApi, ui_menu_bar_get_label) >
                   offsetof(KUA_HostApi, ui_menu_bar_set_label),
               "Menu-bar label getter must follow setter");
_Static_assert(offsetof(KUA_HostApi, ui_menu_get_item_count) >
                   offsetof(KUA_HostApi, ui_menu_bar_get_label),
               "Menu item methods must follow menu-bar methods");
_Static_assert(offsetof(KUA_HostApi, ui_menu_insert) >
                   offsetof(KUA_HostApi, ui_menu_get_item_count),
               "Menu insertion must follow menu count");
_Static_assert(offsetof(KUA_HostApi, ui_menu_append_separator) >
                   offsetof(KUA_HostApi, ui_menu_insert),
               "Menu separator append must follow item insertion");
_Static_assert(offsetof(KUA_HostApi, ui_menu_insert_separator) >
                   offsetof(KUA_HostApi, ui_menu_append_separator),
               "Menu separator insertion must follow append");
_Static_assert(offsetof(KUA_HostApi, ui_menu_enable) >
                   offsetof(KUA_HostApi, ui_menu_insert_separator),
               "Menu item state methods must follow construction");
_Static_assert(offsetof(KUA_HostApi, ui_menu_check) >
                   offsetof(KUA_HostApi, ui_menu_enable),
               "Menu check method order changed");
_Static_assert(offsetof(KUA_HostApi, ui_menu_is_enabled) >
                   offsetof(KUA_HostApi, ui_menu_check),
               "Menu enabled query order changed");
_Static_assert(offsetof(KUA_HostApi, ui_menu_is_checked) >
                   offsetof(KUA_HostApi, ui_menu_is_enabled),
               "Menu checked query order changed");
_Static_assert(offsetof(KUA_HostApi, ui_menu_set_label) >
                   offsetof(KUA_HostApi, ui_menu_is_checked),
               "Menu label setter order changed");
_Static_assert(offsetof(KUA_HostApi, ui_menu_get_label) >
                   offsetof(KUA_HostApi, ui_menu_set_label),
               "Menu label getter ordering changed");
_Static_assert(offsetof(KUA_HostApi, ui_notebook_get_page) >
                   offsetof(KUA_HostApi, ui_menu_get_label),
               "Notebook page APIs must append after the menu API");
_Static_assert(offsetof(KUA_HostApi, ui_notebook_get_selection) >
                   offsetof(KUA_HostApi, ui_notebook_get_page),
               "Notebook selection getter must follow page lookup");
_Static_assert(offsetof(KUA_HostApi, ui_notebook_set_page_text) >
                   offsetof(KUA_HostApi, ui_notebook_get_selection),
               "Notebook page label setter must follow selection lookup");
_Static_assert(offsetof(KUA_HostApi, ui_notebook_delete_page) >
                   offsetof(KUA_HostApi, ui_notebook_set_page_text),
               "Notebook page deletion ordering changed");
_Static_assert(offsetof(KUA_HostApi, ui_aui_get_pane_state) >
                   offsetof(KUA_HostApi, ui_notebook_delete_page),
               "AUI pane state must append after notebook page operations");
_Static_assert(offsetof(KUA_HostApi, ui_aui_insert_pane) >
                   offsetof(KUA_HostApi, ui_aui_get_pane_state),
               "AUI pane insertion must follow pane state lookup");
_Static_assert(offsetof(KUA_HostApi, ui_aui_pane_show) >
                   offsetof(KUA_HostApi, ui_aui_insert_pane),
               "AUI pane visibility must follow insertion");
_Static_assert(offsetof(KUA_HostApi, ui_aui_save_perspective) >
                   offsetof(KUA_HostApi, ui_aui_pane_show),
               "AUI perspective save must follow pane operations");
_Static_assert(offsetof(KUA_HostApi, ui_aui_load_perspective) >
                   offsetof(KUA_HostApi, ui_aui_save_perspective),
               "AUI perspective load ordering changed");
_Static_assert(offsetof(KUA_HostApi, ui_menu_destroy) >
                   offsetof(KUA_HostApi, ui_aui_load_perspective),
               "Menu lifecycle destroy must append after all prior APIs");
_Static_assert(offsetof(KUA_HostApi, ui_property_grid_delete) >
                   offsetof(KUA_HostApi, ui_property_grid_get_string),
               "Property grid methods must be appended in order");
_Static_assert(offsetof(KUA_ExtensionApi, shutdown) >
                   offsetof(KUA_ExtensionApi, on_editor_reset),
               "Extension lifecycle fields are out of order");

static void noop_callback(void* user_data)
{
    (void) user_data;
}

static int32_t post_to_gui(KUA_GuiCallback callback, void* user_data)
{
    if (!callback)
        return 0;

    callback(user_data);
    return 1;
}

static uint64_t overlay_create(void* frame, void* canvas, void* view)
{
    (void) frame;
    (void) canvas;
    (void) view;
    return 7u;
}

static void extension_shutdown(void)
{
}

int main(void)
{
    KUA_HostApi host = { 0 };
    KUA_EditorContext editor = { 0 };
    KUA_ExtensionApi extension = { 0 };

    host.abi_version = KUA_EXTENSION_ABI_V1;
    host.struct_size = (uint32_t) sizeof(host);
    host.post_to_gui = &post_to_gui;
    host.overlay_create = &overlay_create;

    editor.struct_size = (uint32_t) sizeof(editor);
    editor.editor_kind = KUA_EDITOR_SCHEMATIC;

    extension.abi_version = KUA_EXTENSION_ABI_V1;
    extension.struct_size = (uint32_t) sizeof(extension);
    extension.shutdown = &extension_shutdown;

    if (host.overlay_create(NULL, NULL, NULL) != 7u)
        return 1;

    if (host.post_to_gui(&noop_callback, NULL) != 1)
        return 2;

    extension.shutdown();

    return host.abi_version == KUA_EXTENSION_ABI_V1 &&
                   editor.editor_kind == KUA_EDITOR_SCHEMATIC &&
                   extension.abi_version == KUA_EXTENSION_ABI_V1
               ? 0
               : 3;
}
