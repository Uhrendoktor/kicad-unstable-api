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
