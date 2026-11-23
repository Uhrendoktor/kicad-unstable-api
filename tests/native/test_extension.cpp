#include "kicad_unstable_api/extension_api.h"
#include <array>
#include <cstddef>
#include <cstdint>
#if defined(_WIN32)
#  define TEST_EXPORT __declspec(dllexport)
#else
#  define TEST_EXPORT __attribute__((visibility("default")))
#endif
namespace
{
enum Metric { Initialize, WindowCreated, Reset, Shutdown, GuiCallback, EditorKind, OverlayApi, MetricCount };
std::array<int, MetricCount> metrics{};
const KUA_HostApi* host_api{};
void callback(void*) { ++metrics[GuiCallback]; }
int initialize(const KUA_HostApi* host)
{
    ++metrics[Initialize];
    host_api = host;
    if (!host || host->abi_version != KUA_EXTENSION_ABI_V1 ||
        (host->editor_kind != KUA_EDITOR_SCHEMATIC && host->editor_kind != KUA_EDITOR_PCB) ||
        !host->post_to_gui || host->struct_size < sizeof(KUA_HostApi) ||
        !host->overlay_create || !host->overlay_destroy || !host->overlay_clear ||
        !host->overlay_set_style || !host->overlay_line || !host->overlay_circle ||
        !host->overlay_rectangle || !host->ui_window_get_handle || !host->ui_window_create ||
        !host->ui_window_destroy || !host->ui_window_get_children ||
        !host->ui_window_get_top_level_windows || !host->ui_window_observe ||
        !host->ui_window_unobserve ||
        !host->ui_text_ctrl_set_value || !host->ui_text_ctrl_get_value ||
        !host->ui_check_box_set_value || !host->ui_check_box_get_value ||
        !host->ui_choice_append || !host->ui_choice_clear || !host->ui_choice_get_count ||
        !host->ui_choice_set_selection || !host->ui_choice_get_selection ||
        !host->ui_choice_get_string_selection || !host->ui_window_enable ||
        !host->ui_window_raise || !host->ui_window_set_focus ||
        !host->ui_window_set_size || !host->ui_window_get_size ||
        !host->ui_window_set_position || !host->ui_window_get_position ||
        !host->ui_notebook_add_page || !host->ui_menu_bar_get || !host->ui_menu_create ||
        !host->ui_aui_get_manager || !host->ui_aui_add_pane ||
        !host->ui_properties_get_panel || !host->ui_properties_get_grid ||
        !host->ui_property_grid_append_string || !host->ui_property_grid_delete)
        return 0;
    if (host->overlay_create(nullptr, nullptr, nullptr) != 0 ||
        host->overlay_destroy(0) != -1 || host->overlay_clear(0) != -1 ||
        host->overlay_set_style(0, 0,0,0,1, 0,0,0,1, 1, 1,0) != -1 ||
        host->overlay_line(0, 0,0,1,1) != -1 ||
        host->overlay_circle(0, 0,0,1) != -1 ||
        host->overlay_rectangle(0, 0,0,1,1) != -1 ||
        host->ui_window_get_handle(nullptr) != 0 ||
        host->ui_window_create(0, KUA_UI_WINDOW_PANEL, -1, "", 0) != 0 ||
        host->ui_window_destroy(0) != -1 ||
        host->ui_window_get_children(0, nullptr, 0) != -1 ||
        host->ui_window_get_top_level_windows(0, nullptr, 0) != -1 ||
        host->ui_window_observe(0, nullptr, nullptr) != 0 ||
        host->ui_window_unobserve(0) != -1 ||
        host->ui_text_ctrl_set_value(0, "") != -1 ||
        host->ui_text_ctrl_get_value(0, nullptr, 0) != -1 ||
        host->ui_check_box_set_value(0, 1) != -1 ||
        host->ui_check_box_get_value(0) != -1 ||
        host->ui_choice_append(0, "") != -1 ||
        host->ui_choice_clear(0) != -1 ||
        host->ui_choice_get_count(0) != -1 ||
        host->ui_choice_set_selection(0, 0) != -1 ||
        host->ui_choice_get_selection(0) != -1 ||
        host->ui_choice_get_string_selection(0, nullptr, 0) != -1 ||
        host->ui_window_enable(0, 1) != -1 ||
        host->ui_window_raise(0) != -1 ||
        host->ui_window_set_focus(0) != -1 ||
        host->ui_window_set_size(0, 1, 1) != -1 ||
        host->ui_window_get_size(0, nullptr, nullptr) != -1 ||
        host->ui_window_set_position(0, 1, 1) != -1 ||
        host->ui_window_get_position(0, nullptr, nullptr) != -1 ||
        host->ui_notebook_add_page(0, 0, "", 0) != -1 ||
        host->ui_menu_create(0) != 0 ||
        host->ui_aui_get_manager(0) != 0 ||
        host->ui_aui_add_pane(0, 0, nullptr) != -1 ||
        host->ui_properties_get_panel(0) != 0 ||
        host->ui_properties_get_grid(0) != 0 ||
        host->ui_property_grid_append_string(0, "", "", "") != -1)
        return 0;
    metrics[OverlayApi] = 1;
    metrics[EditorKind] = static_cast<int>(host->editor_kind);
    host->post_to_gui(&callback, nullptr);
    return 1;
}
void on_window(const KUA_EditorContext* context)
{
    if (context && context->editor_kind == static_cast<uint32_t>(metrics[EditorKind]) &&
        context->class_id == 123 &&
        context->frame == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234)) &&
        context->struct_size >= sizeof(KUA_EditorContext) && !context->canvas && !context->view)
        ++metrics[WindowCreated];
}
void on_reset(uint32_t editor_kind)
{
    if (editor_kind == static_cast<uint32_t>(metrics[EditorKind]))
        ++metrics[Reset];
}
void shutdown() { ++metrics[Shutdown]; }
const KUA_ExtensionApi api{KUA_EXTENSION_ABI_V1, sizeof(KUA_ExtensionApi), "test.extension",
                           &initialize, &on_window, &on_reset, &shutdown};
}
extern "C" KUA_EXTENSION_EXPORT const KUA_ExtensionApi* KUA_GetExtension(uint32_t requested_abi)
{
    return requested_abi == KUA_EXTENSION_ABI_V1 ? &api : nullptr;
}
extern "C" KUA_EXTENSION_EXPORT int KUA_TestExtensionMetric(int index)
{
    if (index < 0 || index >= MetricCount)
        return -1;
    return metrics[static_cast<size_t>(index)];
}

extern "C" KUA_EXTENSION_EXPORT int KUA_TestPostAfterShutdown()
{
    if (!host_api || !host_api->post_to_gui)
        return -1;
    return host_api->post_to_gui(&callback, nullptr);
}
