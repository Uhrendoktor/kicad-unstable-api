#include "kicad_unstable_api/extension_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

#if defined(KUA_SMOKE_WITH_WX)
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/aui/aui.h>
#include <wx/panel.h>
#endif

namespace
{
const KUA_HostApi* g_host{};
int g_window_shown_events = 0;
int g_window_hidden_events = 0;
int g_window_destroying_events = 0;
int g_destroy_event_handle_valid = 0;
uint64_t g_last_destroy_event_handle = 0;
uint64_t g_managed_aui_panel_handle = 0;
uint64_t g_managed_aui_manager_handle = 0;
int g_aui_manager_destroy_callback_result = 0;

#if defined(KUA_SMOKE_WITH_WX)
class KuaSmokeManagedAuiPanel final : public wxPanel
{
public:
    explicit KuaSmokeManagedAuiPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY), manager_(this)
    {
    }

private:
    // Mirrors the common ownership pattern where a child window owns its manager.
    wxAuiManager manager_;
};
#endif

void on_native_window_event(uint64_t window, uint32_t event_kind, void*)
{
    if (!window)
        return;
    if (event_kind == KUA_UI_WINDOW_SHOWN)
        ++g_window_shown_events;
    else if (event_kind == KUA_UI_WINDOW_HIDDEN)
        ++g_window_hidden_events;
    else if (event_kind == KUA_UI_WINDOW_DESTROYING)
    {
        ++g_window_destroying_events;
        g_last_destroy_event_handle = window;
        char class_name[64]{};
        if (g_host && g_host->ui_window_get_class_name &&
            g_host->ui_window_get_class_name(window, class_name, sizeof(class_name)) > 0)
            ++g_destroy_event_handle_valid;

        if (window == g_managed_aui_panel_handle && g_managed_aui_manager_handle &&
            g_host && g_host->ui_aui_update)
        {
            // The manager is a derived-window member, so its handle must
            // already be stale by the time wxWindow emits DESTROY.
            g_aui_manager_destroy_callback_result =
                g_host->ui_aui_update(g_managed_aui_manager_handle) == -1 ? 1 : -1;
        }
    }
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
        !host->overlay_circle || !host->overlay_rectangle ||
        !host->ui_window_get_top_level_windows || !host->ui_window_observe ||
        !host->ui_window_unobserve)
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
    // Child windows are destroyed synchronously by wxWindow::Destroy(), so
    // this probe verifies delivery of the native wxEVT_DESTROY notification.
    const uint64_t destroy_probe = frame_window
        ? g_host->ui_window_create(frame_window, KUA_UI_WINDOW_PANEL, -1, "KUA destroy probe", 0)
        : 0;
    const int32_t destroy_probe_result = destroy_probe
        ? g_host->ui_window_destroy(destroy_probe)
        : -1;
    char destroyed_probe_class[64]{};
    const int32_t stale_destroy_probe = destroy_probe
        ? g_host->ui_window_get_class_name(destroy_probe, destroyed_probe_class,
                                           sizeof(destroyed_probe_class))
        : 0;
    char expired_destroy_event_class[64]{};
    const int32_t expired_destroy_event_handle = g_last_destroy_event_handle
        ? g_host->ui_window_get_class_name(g_last_destroy_event_handle,
                                           expired_destroy_event_class,
                                           sizeof(expired_destroy_event_class))
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

    int sizer_replacement_ok = 1;
    int sizer_owner_destroyed_ok = 1;
    int sizer_ownership_ok = 1;
#if defined(KUA_SMOKE_WITH_WX)
    const uint64_t old_root_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t old_nested_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t old_deep_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t old_deep_added = old_nested_sizer && old_deep_sizer
        ? g_host->ui_sizer_add(old_nested_sizer, old_deep_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t old_nested_added = old_root_sizer && old_nested_sizer
        ? g_host->ui_sizer_add(old_root_sizer, old_nested_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t old_sizer_attached = old_root_sizer
        ? g_host->ui_window_set_sizer(panel, old_root_sizer) : -1;

    const uint64_t new_root_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t new_nested_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t new_deep_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t new_deep_added = new_nested_sizer && new_deep_sizer
        ? g_host->ui_sizer_add(new_nested_sizer, new_deep_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t new_nested_added = new_root_sizer && new_nested_sizer
        ? g_host->ui_sizer_add(new_root_sizer, new_nested_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t new_sizer_attached = new_root_sizer
        ? g_host->ui_window_set_sizer(panel, new_root_sizer) : -1;
    const uint64_t late_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t late_sizer_added = new_deep_sizer && late_sizer
        ? g_host->ui_sizer_add(new_deep_sizer, late_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t old_root_stale = old_root_sizer
        ? g_host->ui_sizer_add(old_root_sizer, frame_window, 0, 0, 0) : 0;
    const int32_t old_nested_stale = old_nested_sizer
        ? g_host->ui_sizer_add(old_nested_sizer, frame_window, 0, 0, 0) : 0;
    const int32_t old_deep_stale = old_deep_sizer
        ? g_host->ui_sizer_add(old_deep_sizer, frame_window, 0, 0, 0) : 0;
    sizer_replacement_ok = old_deep_added > 0 && old_nested_added > 0 &&
                           old_sizer_attached > 0 && new_deep_added > 0 &&
                           new_nested_added > 0 && new_sizer_attached > 0 &&
                           late_sizer_added > 0 && old_root_stale == -1 &&
                           old_nested_stale == -1 && old_deep_stale == -1;

    const uint64_t sizer_owner_a = g_host->ui_window_create(
        panel, KUA_UI_WINDOW_PANEL, -1, "KUA sizer owner A", 0);
    const uint64_t sizer_owner_b = g_host->ui_window_create(
        panel, KUA_UI_WINDOW_PANEL, -1, "KUA sizer owner B", 0);
    const uint64_t sizer_owner_c = g_host->ui_window_create(
        panel, KUA_UI_WINDOW_PANEL, -1, "KUA sizer owner C", 0);
    const uint64_t sizer_owner_d = g_host->ui_window_create(
        panel, KUA_UI_WINDOW_PANEL, -1, "KUA sizer owner D", 0);
    const uint64_t sizer_foreign_window = sizer_owner_b
        ? g_host->ui_window_create(sizer_owner_b, KUA_UI_WINDOW_BUTTON, -1, "foreign", 0)
        : 0;
    const uint64_t sizer_owned_window = sizer_owner_a
        ? g_host->ui_window_create(sizer_owner_a, KUA_UI_WINDOW_BUTTON, -1, "owned", 0)
        : 0;
    const uint64_t window_sizer_a = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t window_sizer_b = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t first_window_membership = window_sizer_a && sizer_owned_window
        ? g_host->ui_sizer_add(window_sizer_a, sizer_owned_window, 0, 0, 0) : -1;
    const int32_t duplicate_window_membership = window_sizer_b && sizer_owned_window
        ? g_host->ui_sizer_add(window_sizer_b, sizer_owned_window, 0, 0, 0) : -1;
    const int32_t window_sizer_a_attached = sizer_owner_a && window_sizer_a
        ? g_host->ui_window_set_sizer(sizer_owner_a, window_sizer_a) : -1;
    const int32_t window_sizer_b_attached = sizer_owner_b && window_sizer_b
        ? g_host->ui_window_set_sizer(sizer_owner_b, window_sizer_b) : -1;
    const int32_t window_sizer_same_owner = sizer_owner_a && window_sizer_a
        ? g_host->ui_window_set_sizer(sizer_owner_a, window_sizer_a) : -1;
    const int32_t window_sizer_second_owner = sizer_owner_b && window_sizer_a
        ? g_host->ui_window_set_sizer(sizer_owner_b, window_sizer_a) : 0;
    const int32_t foreign_window_membership = window_sizer_a && sizer_foreign_window
        ? g_host->ui_sizer_add(window_sizer_a, sizer_foreign_window, 0, 0, 0) : -1;

    const uint64_t nested_sizer_a = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t nested_sizer_b = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const uint64_t nested_child_sizer = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t nested_first_parent = nested_sizer_a && nested_child_sizer
        ? g_host->ui_sizer_add(nested_sizer_a, nested_child_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t nested_second_parent = nested_sizer_b && nested_child_sizer
        ? g_host->ui_sizer_add(nested_sizer_b, nested_child_sizer, 1, wxEXPAND, 0) : -1;
    const int32_t nested_direct_window_root = sizer_owner_c && nested_child_sizer
        ? g_host->ui_window_set_sizer(sizer_owner_c, nested_child_sizer) : -1;
    const int32_t nested_root_a_attached = sizer_owner_c && nested_sizer_a
        ? g_host->ui_window_set_sizer(sizer_owner_c, nested_sizer_a) : -1;
    const int32_t nested_root_b_attached = sizer_owner_d && nested_sizer_b
        ? g_host->ui_window_set_sizer(sizer_owner_d, nested_sizer_b) : -1;

    const uint64_t foreign_window_root = g_host->ui_sizer_create(
        KUA_UI_SIZER_BOX, wxVERTICAL, 0, 0, 0, 0);
    const int32_t foreign_window_root_add = foreign_window_root && sizer_foreign_window
        ? g_host->ui_sizer_add(foreign_window_root, sizer_foreign_window, 0, 0, 0) : -1;
    const int32_t foreign_window_wrong_root = sizer_owner_a && foreign_window_root
        ? g_host->ui_window_set_sizer(sizer_owner_a, foreign_window_root) : -1;
    const int32_t foreign_window_right_root = sizer_owner_b && foreign_window_root
        ? g_host->ui_window_set_sizer(sizer_owner_b, foreign_window_root) : -1;

    sizer_ownership_ok = first_window_membership > 0 &&
                         duplicate_window_membership == -1 &&
                         window_sizer_a_attached > 0 && window_sizer_b_attached > 0 &&
                         window_sizer_same_owner > 0 && window_sizer_second_owner == -1 &&
                         foreign_window_membership == -1 &&
                         nested_first_parent > 0 && nested_second_parent == -1 &&
                         nested_direct_window_root == -1 &&
                         nested_root_a_attached > 0 && nested_root_b_attached > 0 &&
                         foreign_window_root_add > 0 && foreign_window_wrong_root == -1 &&
                         foreign_window_right_root > 0;
#endif

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

    int aui_owner_lifetime_ok = 1;
#if defined(KUA_SMOKE_WITH_WX)
    auto* managed_editor_frame = dynamic_cast<wxFrame*>(static_cast<wxWindow*>(context->frame));
    auto* managed_panel = managed_editor_frame
        ? new KuaSmokeManagedAuiPanel(managed_editor_frame)
        : nullptr;
    const uint64_t managed_panel_handle = managed_panel
        ? g_host->ui_window_get_handle(managed_panel)
        : 0;
    const uint64_t managed_manager = managed_panel_handle
        ? g_host->ui_aui_get_manager(managed_panel_handle)
        : 0;
    const int32_t managed_manager_before_destroy = managed_manager
        ? g_host->ui_aui_update(managed_manager)
        : -1;
    g_managed_aui_panel_handle = managed_panel_handle;
    g_managed_aui_manager_handle = managed_manager;
    g_aui_manager_destroy_callback_result = 0;
    const bool managed_panel_destroyed = managed_panel && managed_panel->Destroy();
    const int32_t managed_manager_after_destroy = managed_manager
        ? g_host->ui_aui_update(managed_manager)
        : -2;
    aui_owner_lifetime_ok = managed_panel_handle && managed_manager &&
                            managed_manager_before_destroy > 0 &&
                            managed_panel_destroyed &&
                            g_aui_manager_destroy_callback_result == 1 &&
                            managed_manager_after_destroy == -1;
    g_managed_aui_panel_handle = 0;
    g_managed_aui_manager_handle = 0;
#endif

    const uint64_t menu_bar = frame_window ? g_host->ui_menu_bar_get(frame_window) : 0;
    const uint64_t menu = menu_bar ? g_host->ui_menu_create(frame_window) : 0;
    constexpr int32_t smoke_menu_id = 61000;
    const int32_t menu_item = menu
        ? g_host->ui_menu_append(menu, smoke_menu_id, "KUA UI binding smoke", "", KUA_UI_MENU_NORMAL)
        : -1;
    const int32_t menu_attached = menu_item > 0
        ? g_host->ui_menu_bar_append(menu_bar, menu, "KUA Smoke")
        : -1;
    int menu_callback_count = 0;
    const uint64_t menu_binding = menu_attached > 0
        ? g_host->ui_menu_bind(frame_window, smoke_menu_id, &on_menu_command,
                               &menu_callback_count)
        : 0;
    int menu_dispatch_ok = 1;
#if defined(KUA_SMOKE_WITH_WX)
    auto* editor_frame = dynamic_cast<wxFrame*>(static_cast<wxWindow*>(context->frame));
    wxMenuBar* native_menu_bar = editor_frame ? editor_frame->GetMenuBar() : nullptr;
    const int native_menu_count = native_menu_bar ? native_menu_bar->GetMenuCount() : 0;
    wxMenu* source_menu = native_menu_count > 0
        ? native_menu_bar->GetMenu(native_menu_count - 1) : nullptr;
    bool menu_event_processed = false;
    if (editor_frame && source_menu && menu_binding)
    {
        wxCommandEvent menu_event(wxEVT_MENU, smoke_menu_id);
        menu_event.SetEventObject(source_menu);
        menu_event_processed = editor_frame->GetEventHandler()->ProcessEvent(menu_event);
    }
    menu_dispatch_ok = menu_event_processed && menu_callback_count == 1 ? 1 : 0;
#endif
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

    const int32_t panel_destroyed = panel ? g_host->ui_window_destroy(panel) : -1;
#if defined(KUA_SMOKE_WITH_WX)
    const int32_t new_root_stale = new_root_sizer
        ? g_host->ui_sizer_add(new_root_sizer, frame_window, 0, 0, 0) : 0;
    const int32_t new_nested_stale = new_nested_sizer
        ? g_host->ui_sizer_add(new_nested_sizer, frame_window, 0, 0, 0) : 0;
    const int32_t new_deep_stale = new_deep_sizer
        ? g_host->ui_sizer_add(new_deep_sizer, frame_window, 0, 0, 0) : 0;
    const int32_t late_sizer_stale = late_sizer
        ? g_host->ui_sizer_add(late_sizer, frame_window, 0, 0, 0) : 0;
    sizer_owner_destroyed_ok = panel_destroyed > 0 && new_root_stale == -1 &&
                               new_nested_stale == -1 && new_deep_stale == -1 &&
                               late_sizer_stale == -1;
#endif

    char ui_details[512]{};
    std::snprintf(ui_details, sizeof(ui_details),
                  "frame=%d|top=%d|dialog=%d|shown=%d|hidden=%d|enumerated=%d|destroy=%d|stale=%d|probe=%d|probe_destroy=%d|probe_stale=%d|destroy_event_valid=%d|destroy_event_stale=%d|observer=%d|show_events=%d|hide_events=%d|destroy_events=%d|panel=%d|panel_destroyed=%d|page=%d|count=%d|class=%d|sizer_replace=%d|sizer_destroy=%d|sizer_ownership=%d|aui=%d|aui_owner_lifetime=%d|pane=%d|update=%d|detach=%d|menu=%d|bind=%d|menu_dispatch=%d|menu_calls=%d|unbind=%d|property=%d",
                  frame_window != 0, top_level_initial, dialog != 0, dialog_shown, dialog_hidden,
                  dialog_enumerated, dialog_destroyed, stale_dialog_handle, destroy_probe != 0,
                  destroy_probe_result, stale_destroy_probe, g_destroy_event_handle_valid,
                  expired_destroy_event_handle, window_observer_removed,
                  g_window_shown_events, g_window_hidden_events, g_window_destroying_events,
                  panel != 0, panel_destroyed, page_added, page_count, class_read,
                  sizer_replacement_ok, sizer_owner_destroyed_ok, sizer_ownership_ok, manager != 0, aui_owner_lifetime_ok, pane_added,
                  aui_updated, pane_detached, menu_attached, menu_binding != 0, menu_dispatch_ok,
                  menu_callback_count, menu_unbound, property_result);
    const bool ui_ok = frame_window && top_level_initial > 0 && window_observer &&
                       dialog && dialog_shown > 0 && dialog_hidden > 0 && dialog_enumerated &&
                       dialog_destroyed > 0 && stale_dialog_handle == -1 &&
                       g_destroy_event_handle_valid > 0 && expired_destroy_event_handle == -1 &&
                       window_observer_removed > 0 && g_window_shown_events > 0 &&
                       g_window_hidden_events > 0 && g_window_destroying_events > 0 &&
                       panel && panel_destroyed > 0 && notebook && page_added > 0 &&
                       page_count == 1 && class_read > 0 &&
                       std::string(class_name).find("wxNotebook") != std::string::npos &&
                       sizer_replacement_ok && sizer_owner_destroyed_ok && sizer_ownership_ok &&
                       aui_owner_lifetime_ok && pane_added > 0 && aui_updated > 0 && pane_detached > 0 &&
                       menu_attached > 0 && menu_binding != 0 && menu_dispatch_ok &&
                       menu_unbound > 0 && (!property_grid || property_result > 0);
    record(ui_ok ? "native-ui-ready" : "native-ui-failed",
           context->editor_kind, ui_details);
}

void on_menu_command(void* user_data)
{
    if (user_data)
        ++*static_cast<int*>(user_data);
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
