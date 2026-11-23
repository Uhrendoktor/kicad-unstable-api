#include "kicad_unstable_api/extension_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

namespace
{
const KUA_HostApi* g_host{};
int g_window_shown_events = 0;
int g_window_hidden_events = 0;
int g_window_destroying_events = 0;

void on_native_window_event(uint64_t window, uint32_t event_kind, void*)
{
    if (!window)
        return;
    if (event_kind == KUA_UI_WINDOW_SHOWN)
        ++g_window_shown_events;
    else if (event_kind == KUA_UI_WINDOW_HIDDEN)
        ++g_window_hidden_events;
    else if (event_kind == KUA_UI_WINDOW_DESTROYING)
        ++g_window_destroying_events;
}

void record(const char* event, uint32_t editor_kind, const char* details = "")
{
    const char* path = std::getenv("KUA_SMOKE_LOG");
    if (!path || !*path)
        return;

    if (FILE* file = std::fopen(path, "a"))
    {
        std::fprintf(file, "%s|editor=%u|%s\n", event, editor_kind, details);
        std::fflush(file);
        std::fclose(file);
    }
}

int initialize(const KUA_HostApi* host)
{
    record("initialize-entered", host ? host->editor_kind : static_cast<uint32_t>(KUA_EDITOR_UNKNOWN));
    if (!host || host->abi_version != KUA_EXTENSION_ABI_V1 ||
        host->struct_size < sizeof(KUA_HostApi) || !host->overlay_create ||
        !host->overlay_destroy || !host->overlay_set_style || !host->overlay_line ||
        !host->overlay_circle || !host->overlay_rectangle)
    {
        record("initialize-rejected", host ? host->editor_kind : KUA_EDITOR_UNKNOWN,
               "invalid-host-api-descriptor");
        return 0;
    }

    g_host = host;
    record("initialize", host->editor_kind, host->host_build_tag ? host->host_build_tag : "");
    return 1;
}

void on_menu_command(void* user_data);

void on_window(const KUA_EditorContext* context)
{
    if (!context || context->struct_size < sizeof(KUA_EditorContext))
        return;

    char details[128]{};
    std::snprintf(details, sizeof(details), "frame=%d|canvas=%d|view=%d",
                  context->frame != nullptr, context->canvas != nullptr, context->view != nullptr);
    record("window", context->editor_kind, details);

    if (!g_host || !context->frame || !context->canvas || !context->view)
    {
        record("overlay-failed", context->editor_kind, "missing-frame-canvas-or-view");
        return;
    }

    const uint64_t overlay = g_host->overlay_create(context->frame, context->canvas, context->view);
    if (!overlay)
    {
        record("overlay-failed", context->editor_kind, "overlay_create-returned-zero");
        return;
    }

    const int32_t styled = g_host->overlay_set_style(
        overlay, 0.10, 0.85, 0.20, 1.0, 0.10, 0.85, 0.20, 0.15, 1.0, 1, 0);
    const int32_t line = g_host->overlay_line(overlay, -200000.0, 0.0, 200000.0, 0.0);
    const int32_t circle = g_host->overlay_circle(overlay, 0.0, 0.0, 50000.0);
    const int32_t rectangle = g_host->overlay_rectangle(
        overlay, -100000.0, -100000.0, 100000.0, 100000.0);

    std::snprintf(details, sizeof(details),
                  "handle=%llu|style=%d|line=%d|circle=%d|rectangle=%d",
                  static_cast<unsigned long long>(overlay), styled, line, circle, rectangle);
    record((styled > 0 && line > 0 && circle > 0 && rectangle > 0)
               ? "overlay-ready"
               : "overlay-failed",
           context->editor_kind, details);

    // Exercise native wxWidgets UI bindings through KUA: child windows,
    // wxNotebook pages, the editor's existing AUI manager and menu bar.
    const uint64_t frame_window = g_host->ui_window_get_handle(context->frame);
    const int32_t top_level_initial = frame_window
        ? g_host->ui_window_get_top_level_windows(frame_window, nullptr, 0)
        : -1;
    const uint64_t window_observer = frame_window
        ? g_host->ui_window_observe(frame_window, &on_native_window_event, nullptr)
        : 0;
    const uint64_t dialog = frame_window
        ? g_host->ui_window_create(frame_window, KUA_UI_WINDOW_DIALOG, -1, "KUA dialog smoke", 0)
        : 0;
    const int32_t dialog_shown = dialog ? g_host->ui_window_show(dialog, 1) : -1;
    const int32_t dialog_hidden = dialog ? g_host->ui_window_show(dialog, 0) : -1;
    uint64_t top_level_windows[128]{};
    const int32_t top_level_count = frame_window
        ? g_host->ui_window_get_top_level_windows(frame_window, top_level_windows, 128)
        : -1;
    bool dialog_enumerated = false;
    for (int32_t i = 0; i < top_level_count && i < 128; ++i)
        dialog_enumerated = dialog_enumerated || top_level_windows[i] == dialog;
    const int32_t dialog_destroyed = dialog ? g_host->ui_window_destroy(dialog) : -1;
    char destroyed_class[64]{};
    const int32_t stale_dialog_handle = dialog
        ? g_host->ui_window_get_class_name(dialog, destroyed_class, sizeof(destroyed_class))
        : 0;
    const int32_t window_observer_removed = window_observer
        ? g_host->ui_window_unobserve(window_observer)
        : -1;

    const uint64_t panel = frame_window
        ? g_host->ui_window_create(frame_window, KUA_UI_WINDOW_PANEL, -1, "KUA UI smoke", 0)
        : 0;
    const uint64_t notebook = panel
        ? g_host->ui_window_create(panel, KUA_UI_WINDOW_NOTEBOOK, -1, "", 0)
        : 0;
    const uint64_t page = notebook
        ? g_host->ui_window_create(notebook, KUA_UI_WINDOW_PANEL, -1, "", 0)
        : 0;
    const int32_t page_added = notebook && page
        ? g_host->ui_notebook_add_page(notebook, page, "Native binding", 1)
        : -1;
    const int32_t page_count = notebook ? g_host->ui_notebook_get_page_count(notebook) : -1;

    char class_name[128]{};
    const int32_t class_read = notebook
        ? g_host->ui_window_get_class_name(notebook, class_name, sizeof(class_name))
        : -1;

    const uint64_t manager = frame_window ? g_host->ui_aui_get_manager(frame_window) : 0;
    KUA_AuiPaneOptions pane{};
    pane.struct_size = sizeof(pane);
    pane.name = "KUA_UiBindingSmoke";
    pane.caption = "KUA UI binding smoke";
    pane.direction = KUA_AUI_RIGHT;
    pane.layer = 1;
    pane.best_width = 260;
    pane.best_height = 220;
    pane.min_width = 160;
    pane.min_height = 120;
    pane.flags = KUA_AUI_CLOSE_BUTTON | KUA_AUI_CAPTION_VISIBLE |
                 KUA_AUI_PANE_BORDER | KUA_AUI_MOVABLE | KUA_AUI_RESIZABLE |
                 KUA_AUI_FLOATABLE | KUA_AUI_DOCKABLE;
    const int32_t pane_added = manager && panel
        ? g_host->ui_aui_add_pane(manager, panel, &pane)
        : -1;
    const int32_t aui_updated = pane_added > 0 ? g_host->ui_aui_update(manager) : -1;
    const int32_t pane_detached = pane_added > 0
        ? g_host->ui_aui_detach_pane(manager, panel)
        : -1;
    if (pane_detached > 0)
        g_host->ui_aui_update(manager);

    const uint64_t menu_bar = frame_window ? g_host->ui_menu_bar_get(frame_window) : 0;
    const uint64_t menu = menu_bar ? g_host->ui_menu_create(frame_window) : 0;
    constexpr int32_t smoke_menu_id = 61000;
    const int32_t menu_item = menu
        ? g_host->ui_menu_append(menu, smoke_menu_id, "KUA UI binding smoke", "", KUA_UI_MENU_NORMAL)
        : -1;
    const int32_t menu_attached = menu_item > 0
        ? g_host->ui_menu_bar_append(menu_bar, menu, "KUA Smoke")
        : -1;
    const uint64_t menu_binding = menu_attached > 0
        ? g_host->ui_menu_bind(frame_window, smoke_menu_id, &on_menu_command, nullptr)
        : 0;
    const int32_t menu_unbound = menu_binding
        ? g_host->ui_menu_unbind(menu_binding)
        : -1;

    const uint64_t properties_panel = frame_window
        ? g_host->ui_properties_get_panel(frame_window)
        : 0;
    const uint64_t property_grid = properties_panel
        ? g_host->ui_properties_get_grid(properties_panel)
        : 0;
    int32_t property_result = 0;
    if (property_grid)
    {
        constexpr const char* property_name = "KUA_UI_Binding_Smoke";
        property_result = g_host->ui_property_grid_append_string(
            property_grid, "KUA UI binding smoke", property_name, "created");
        if (property_result > 0)
        {
            char value[64]{};
            const int32_t set = g_host->ui_property_grid_set_string(
                property_grid, property_name, "updated");
            const int32_t get = g_host->ui_property_grid_get_string(
                property_grid, property_name, value, sizeof(value));
            const int32_t remove = g_host->ui_property_grid_delete(property_grid, property_name);
            property_result = set > 0 && get > 0 && remove > 0 &&
                              std::strcmp(value, "updated") == 0 ? 1 : -1;
        }
    }

    char ui_details[256]{};
    std::snprintf(ui_details, sizeof(ui_details),
                  "frame=%d|top=%d|dialog=%d|shown=%d|hidden=%d|enumerated=%d|destroy=%d|stale=%d|observer=%d|show_events=%d|hide_events=%d|destroy_events=%d|panel=%d|page=%d|count=%d|class=%d|aui=%d|pane=%d|update=%d|detach=%d|menu=%d|bind=%d|unbind=%d|property=%d",
                  frame_window != 0, top_level_initial, dialog != 0, dialog_shown, dialog_hidden,
                  dialog_enumerated, dialog_destroyed, stale_dialog_handle, window_observer_removed,
                  g_window_shown_events, g_window_hidden_events, g_window_destroying_events,
                  panel != 0, page_added, page_count, class_read, manager != 0, pane_added,
                  aui_updated, pane_detached, menu_attached, menu_binding != 0, menu_unbound,
                  property_result);
    const bool ui_ok = frame_window && top_level_initial > 0 && window_observer &&
                       dialog && dialog_shown > 0 && dialog_hidden > 0 && dialog_enumerated &&
                       dialog_destroyed > 0 && stale_dialog_handle == -1 &&
                       window_observer_removed > 0 && g_window_shown_events > 0 &&
                       g_window_hidden_events > 0 && panel && notebook && page_added > 0 &&
                       page_count == 1 && class_read > 0 &&
                       std::string(class_name).find("wxNotebook") != std::string::npos &&
                       pane_added > 0 && aui_updated > 0 && pane_detached > 0 &&
                       menu_attached > 0 && menu_binding != 0 && menu_unbound > 0 &&
                       (!property_grid || property_result > 0);
    record(ui_ok ? "native-ui-ready" : "native-ui-failed",
           context->editor_kind, ui_details);

    if (panel)
        g_host->ui_window_destroy(panel);
}

void on_menu_command(void*)
{
    record("menu-command", g_host ? g_host->editor_kind : KUA_EDITOR_UNKNOWN);
}

void on_reset(uint32_t editor_kind)
{
    record("reset", editor_kind);
}

void shutdown()
{
    record("shutdown", g_host ? g_host->editor_kind : KUA_EDITOR_UNKNOWN);
}

const KUA_ExtensionApi api{
    KUA_EXTENSION_ABI_V1, sizeof(KUA_ExtensionApi), "kicad-unstable-api.real-smoke",
    &initialize, &on_window, &on_reset, &shutdown
};
} // namespace

extern "C" KUA_EXTENSION_EXPORT const KUA_ExtensionApi* KUA_GetExtension(uint32_t requested_abi)
{
    return requested_abi == KUA_EXTENSION_ABI_V1 ? &api : nullptr;
}
