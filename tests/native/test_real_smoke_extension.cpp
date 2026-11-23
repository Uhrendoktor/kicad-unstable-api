#include "kicad_unstable_api/extension_api.h"

#include <cstdio>
#include <cstdlib>

namespace
{
const KUA_HostApi* g_host{};

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
    record("initialize-entered", host ? host->editor_kind : KUA_EDITOR_UNKNOWN);
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
