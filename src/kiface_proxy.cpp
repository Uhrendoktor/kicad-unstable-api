#include "kicad_unstable_api/extension_api.h"

#if defined(KUA_SYNTHETIC_ABI)
#  include "synthetic_kiface.hpp"
#else
#  include <kiway.h>
#  include <eda_draw_frame.h>
#  include <wx/app.h>
#  include <wx/thread.h>
#  include <view/view.h>
#  include <view/view_overlay.h>
#  include <gal/color4d.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
using KUA_ModuleHandle = HMODULE;
#else
#  include <dlfcn.h>
using KUA_ModuleHandle = void*;
#endif

#if defined(_WIN32)
#  define KUA_EXPORT __declspec(dllexport)
#else
#  define KUA_EXPORT __attribute__((visibility("default")))
#endif

extern "C" KUA_EXPORT KIFACE* KIFACE_1(int* version, int kiway_version, PGM_BASE* program);

namespace
{
#if defined(KUA_BUILD_EDITOR_PCB)
constexpr uint32_t kEditorKind = KUA_EDITOR_PCB;
#else
constexpr uint32_t kEditorKind = KUA_EDITOR_SCHEMATIC;
#endif

bool is_primary_editor_frame(int class_id)
{
#if defined(KUA_SYNTHETIC_ABI)
    // The synthetic harness models the primary editor frame with class ID 123.
    return class_id == 123;
#elif defined(KUA_BUILD_EDITOR_PCB)
    return class_id == FRAME_PCB_EDITOR;
#else
    return class_id == FRAME_SCH;
#endif
}

struct LoadedExtension
{
    KUA_ModuleHandle handle{};
    const KUA_ExtensionApi* api{};
};

std::vector<LoadedExtension> g_extensions;
KUA_HostApi g_host_api{};
KUA_ModuleHandle g_original_handle{};
KIFACE* g_original_kiface{};
bool g_extensions_initialized = false;
bool g_extensions_shutdown = false;
std::atomic<bool> g_gui_callbacks_enabled{true};

#if !defined(KUA_SYNTHETIC_ABI)
struct OverlayState
{
    void* frame{};
    EDA_DRAW_PANEL_GAL* canvas{};
    KIGFX::VIEW* view{};
    std::shared_ptr<KIGFX::VIEW_OVERLAY> overlay;
};

std::unordered_map<void*, std::unique_ptr<OverlayState>> g_overlays;
struct EditorFrameState { EDA_DRAW_PANEL_GAL* canvas{}; KIGFX::VIEW* view{}; };
std::unordered_map<void*, EditorFrameState> g_editor_frames;

bool on_gui_thread()
{
    return wxIsMainThread() && g_gui_callbacks_enabled.load(std::memory_order_acquire);
}

OverlayState* find_overlay(void* handle)
{
    const auto it = g_overlays.find(handle);
    return it == g_overlays.end() ? nullptr : it->second.get();
}

void destroy_overlays_for_frame(void* frame)
{
    for (auto it = g_overlays.begin(); it != g_overlays.end();)
    {
        OverlayState& state = *it->second;
        if (state.frame != frame)
        {
            ++it;
            continue;
        }
        if (state.view && state.overlay)
            state.view->Remove(state.overlay.get());
        if (state.canvas)
            state.canvas->Refresh(false);
        it = g_overlays.erase(it);
    }
    g_editor_frames.erase(frame);
}

void destroy_all_overlays()
{
    for (auto& entry : g_overlays)
    {
        OverlayState& state = *entry.second;
        if (state.view && state.overlay)
            state.view->Remove(state.overlay.get());
        if (state.canvas)
            state.canvas->Refresh(false);
    }
    g_overlays.clear();
    g_editor_frames.clear();
}
#endif

void log_message(int level, const char* message)
{
    std::fprintf(stderr, "[kicad-unstable-api:%d] %s\n", level, message ? message : "");
    std::fflush(stderr);
}

#if defined(KUA_SYNTHETIC_ABI)
int post_to_gui(KUA_GuiCallback callback, void* user_data)
{
    if (!callback || !g_gui_callbacks_enabled.load(std::memory_order_acquire))
        return 0;
    callback(user_data);
    return 1;
}
#else
struct DeferredCall
{
    KUA_GuiCallback callback{};
    void* user_data{};
};

int post_to_gui(KUA_GuiCallback callback, void* user_data)
{
    if (!callback || !wxTheApp ||
        !g_gui_callbacks_enabled.load(std::memory_order_acquire))
        return 0;

    auto* deferred = new DeferredCall{callback, user_data};
    wxTheApp->CallAfter([deferred]() {
        if (g_gui_callbacks_enabled.load(std::memory_order_acquire))
            deferred->callback(deferred->user_data);
        delete deferred;
    });
    return 1;
}
#endif

#if defined(KUA_SYNTHETIC_ABI)
void* overlay_create(void*, void*, void*) { return nullptr; }
int32_t overlay_destroy(void*) { return -1; }
int32_t overlay_clear(void*) { return -1; }
int32_t overlay_set_style(void*, double, double, double, double, double, double, double, double,
                          double, int32_t, int32_t) { return -1; }
int32_t overlay_line(void*, double, double, double, double) { return -1; }
int32_t overlay_circle(void*, double, double, double) { return -1; }
int32_t overlay_rectangle(void*, double, double, double, double) { return -1; }
#else
void* overlay_create(void* frame, void* canvas, void* view)
{
    if (!on_gui_thread() || !frame || !canvas || !view)
        return nullptr;
    const auto frame_it = g_editor_frames.find(frame);
    if (frame_it == g_editor_frames.end() || frame_it->second.canvas != canvas ||
        frame_it->second.view != view)
        return nullptr;
    auto state = std::make_unique<OverlayState>();
    state->frame = frame;
    state->canvas = static_cast<EDA_DRAW_PANEL_GAL*>(canvas);
    state->view = static_cast<KIGFX::VIEW*>(view);
    state->overlay = state->view->MakeOverlay();
    if (!state->overlay)
        return nullptr;
    void* handle = state.get();
    g_overlays.emplace(handle, std::move(state));
    return handle;
}

int32_t overlay_destroy(void* handle)
{
    if (!on_gui_thread())
        return -1;
    const auto it = g_overlays.find(handle);
    if (it == g_overlays.end())
        return -1;
    OverlayState& state = *it->second;
    if (state.view && state.overlay)
        state.view->Remove(state.overlay.get());
    if (state.canvas)
        state.canvas->Refresh(false);
    g_overlays.erase(it);
    return 1;
}

int32_t overlay_clear(void* handle)
{
    if (!on_gui_thread())
        return -1;
    OverlayState* state = find_overlay(handle);
    if (!state || !state->overlay)
        return -1;
    state->overlay->Clear();
    state->canvas->Refresh(false);
    return 1;
}

int32_t overlay_set_style(void* handle, double sr, double sg, double sb, double sa,
                          double fr, double fg, double fb, double fa, double width,
                          int32_t stroke, int32_t fill)
{
    if (!on_gui_thread() || width < 0.0 ||
        sr < 0.0 || sr > 1.0 || sg < 0.0 || sg > 1.0 || sb < 0.0 || sb > 1.0 || sa < 0.0 || sa > 1.0 ||
        fr < 0.0 || fr > 1.0 || fg < 0.0 || fg > 1.0 || fb < 0.0 || fb > 1.0 || fa < 0.0 || fa > 1.0)
        return -1;
    OverlayState* state = find_overlay(handle);
    if (!state || !state->overlay)
        return -1;
    state->overlay->SetStrokeColor(KIGFX::COLOR4D(sr, sg, sb, sa));
    state->overlay->SetFillColor(KIGFX::COLOR4D(fr, fg, fb, fa));
    state->overlay->SetLineWidth(width);
    state->overlay->SetIsStroke(stroke != 0);
    state->overlay->SetIsFill(fill != 0);
    state->canvas->Refresh(false);
    return 1;
}

int32_t overlay_line(void* handle, double x1, double y1, double x2, double y2)
{
    if (!on_gui_thread())
        return -1;
    OverlayState* state = find_overlay(handle);
    if (!state || !state->overlay)
        return -1;
    state->overlay->Line(VECTOR2D(x1, y1), VECTOR2D(x2, y2));
    state->canvas->Refresh(false);
    return 1;
}

int32_t overlay_circle(void* handle, double cx, double cy, double radius)
{
    if (!on_gui_thread() || radius < 0.0)
        return -1;
    OverlayState* state = find_overlay(handle);
    if (!state || !state->overlay)
        return -1;
    state->overlay->Circle(VECTOR2D(cx, cy), radius);
    state->canvas->Refresh(false);
    return 1;
}

int32_t overlay_rectangle(void* handle, double x1, double y1, double x2, double y2)
{
    if (!on_gui_thread())
        return -1;
    OverlayState* state = find_overlay(handle);
    if (!state || !state->overlay)
        return -1;
    state->overlay->Rectangle(VECTOR2D(x1, y1), VECTOR2D(x2, y2));
    state->canvas->Refresh(false);
    return 1;
}
#endif

KUA_HostApi make_host_api()
{
    KUA_HostApi host{};
    host.abi_version = KUA_EXTENSION_ABI_V1;
    host.struct_size = sizeof(KUA_HostApi);
    #if defined(KUA_KICAD_ABI_TAG)
    host.host_build_tag = KUA_KICAD_ABI_TAG;
#else
    host.host_build_tag = "kicad-unstable-api/synthetic-test-build";
#endif
    host.editor_kind = kEditorKind;
    host.log = &log_message;
    host.post_to_gui = &post_to_gui;
    host.overlay_create = &overlay_create;
    host.overlay_destroy = &overlay_destroy;
    host.overlay_clear = &overlay_clear;
    host.overlay_set_style = &overlay_set_style;
    host.overlay_line = &overlay_line;
    host.overlay_circle = &overlay_circle;
    host.overlay_rectangle = &overlay_rectangle;
    return host;
}

std::filesystem::path current_module_path()
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&KIFACE_1), &module))
        return {};
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&KIFACE_1), &info) == 0 || !info.dli_fname)
        return {};
    return std::filesystem::absolute(info.dli_fname);
#endif
}

KUA_ModuleHandle open_module(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}

void* find_symbol(KUA_ModuleHandle handle, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
#else
    return dlsym(handle, name);
#endif
}

std::filesystem::path original_module_path(const std::filesystem::path& proxy_path)
{
    const auto parent = proxy_path.parent_path();
    const auto extension = proxy_path.extension();
    const auto stem = proxy_path.stem().string();
    return parent / (stem + ".original" + extension.string());
}

bool load_original(KIFACE_GETTER_FUNC** getter_out, int* version, int kiway_version,
                   PGM_BASE* program)
{
    const auto proxy_path = current_module_path();
    if (proxy_path.empty())
    {
        log_message(3, "Cannot resolve the loaded proxy module path.");
        return false;
    }

    const auto original_path = original_module_path(proxy_path);
    g_original_handle = open_module(original_path);
    if (!g_original_handle)
    {
        log_message(3, ("Cannot load preserved original KIFACE: " + original_path.string()).c_str());
        return false;
    }

    auto* symbol = find_symbol(g_original_handle, "KIFACE_1");
    if (!symbol)
    {
        log_message(3, "Preserved original module does not export KIFACE_1.");
        return false;
    }

    auto getter = reinterpret_cast<KIFACE_GETTER_FUNC*>(symbol);
    g_original_kiface = getter(version, kiway_version, program);
    if (!g_original_kiface)
    {
        log_message(3, "Original KIFACE_1 returned a null interface.");
        return false;
    }

    *getter_out = getter;
    return true;
}

class KIFACE_PROXY final : public KIFACE
{
public:
    explicit KIFACE_PROXY(KIFACE* original) : m_original(original) {}

    bool OnKifaceStart(PGM_BASE* program, int control_bits, KIWAY* kiway) override
    {
        if (!m_original || !m_original->OnKifaceStart(program, control_bits, kiway))
            return false;

        initialize_extensions(program, kiway);
        return true;
    }

    void OnKifaceEnd() override
    {
        shutdown_extensions();
        if (m_original)
            m_original->OnKifaceEnd();
    }

    void Reset() override
    {
        if (m_original)
            m_original->Reset();

        for (const auto& extension : g_extensions)
        {
            if (extension.api && extension.api->on_editor_reset)
                extension.api->on_editor_reset(kEditorKind);
        }
    }

    wxWindow* CreateKiWindow(wxWindow* parent, int class_id, KIWAY* kiway,
                             int control_bits = 0) override
    {
        if (!m_original)
            return nullptr;

        wxWindow* window = m_original->CreateKiWindow(parent, class_id, kiway, control_bits);
        if (window && is_primary_editor_frame(class_id))
        {
#if !defined(KUA_SYNTHETIC_ABI)
            window->Bind(wxEVT_CLOSE_WINDOW, [window](wxCloseEvent& event) {
                destroy_overlays_for_frame(window);
                event.Skip();
            });
#endif
            KUA_EditorContext context{};
            context.struct_size = sizeof(KUA_EditorContext);
            context.editor_kind = kEditorKind;
            context.class_id = class_id;
            context.frame = window;
            context.program = m_program;
            context.kiway = kiway;
#if !defined(KUA_SYNTHETIC_ABI)
            // The class ID filter above guarantees this is an EDA_DRAW_FRAME.
            // These accessors are inline/virtual in the exact KiCad headers.
            auto* draw_frame = static_cast<EDA_DRAW_FRAME*>(window);
            auto* canvas = draw_frame->GetCanvas();
            context.canvas = canvas;
            context.view = canvas ? canvas->GetView() : nullptr;
            g_editor_frames[window] = {canvas, static_cast<KIGFX::VIEW*>(context.view)};
#endif
            notify_window_created(context);
        }
        return window;
    }

    void SaveFileAs(const wxString& source_project_base, const wxString& source_project_name,
                    const wxString& target_project_base, const wxString& target_project_name,
                    const wxString& source_file, wxString& errors) override
    {
        if (m_original)
            m_original->SaveFileAs(source_project_base, source_project_name, target_project_base,
                                   target_project_name, source_file, errors);
    }

    void* IfaceOrAddress(int data_id) override
    {
        return m_original ? m_original->IfaceOrAddress(data_id) : nullptr;
    }

    void GetActions(std::vector<TOOL_ACTION*>& actions) const override
    {
        if (m_original)
            m_original->GetActions(actions);
    }

    int HandleJob(JOB* job, REPORTER* reporter, PROGRESS_REPORTER* progress) override
    {
        return m_original ? m_original->HandleJob(job, reporter, progress) : 0;
    }

    bool HandleJobConfig(JOB* job, wxWindow* parent) override
    {
        return m_original ? m_original->HandleJobConfig(job, parent) : false;
    }

    bool HandleApiOpenDocument(const DOCUMENT_SPEC& spec, KICAD_API_SERVER* server,
                               wxString* error) override
    {
        return m_original ? m_original->HandleApiOpenDocument(spec, server, error) : false;
    }

    bool HandleApiCloseDocument(const DOCUMENT_SPEC& spec, KICAD_API_SERVER* server,
                                wxString* error) override
    {
        return m_original ? m_original->HandleApiCloseDocument(spec, server, error) : false;
    }

    void PreloadLibraries(KIWAY* kiway) override
    {
        if (m_original)
            m_original->PreloadLibraries(kiway);
    }

    void CancelPreload(bool block = true) override
    {
        if (m_original)
            m_original->CancelPreload(block);
    }

    void ProjectChanged() override
    {
        if (m_original)
            m_original->ProjectChanged();
    }

    void RegisterLibraryHandlers(KICAD_API_SERVER* server) override
    {
        if (m_original)
            m_original->RegisterLibraryHandlers(server);
    }

    bool LoadAllLibraries() override
    {
        return m_original ? m_original->LoadAllLibraries() : false;
    }

    void set_program(PGM_BASE* program) { m_program = program; }

private:
    void initialize_extensions(PGM_BASE* program, KIWAY* kiway)
    {
        if (g_extensions_initialized)
            return;
        g_extensions_initialized = true;

        const auto module_path = current_module_path();
        if (module_path.empty())
            return;

        const auto extension_dir = module_path.parent_path() / "kicad-unstable-api" / "extensions";
        std::error_code ec;
        if (!std::filesystem::is_directory(extension_dir, ec))
            return;

        g_host_api = make_host_api();
        for (const auto& entry : std::filesystem::directory_iterator(extension_dir, ec))
        {
            if (ec || !entry.is_regular_file())
                continue;

            const auto extension = entry.path().extension().string();
#if defined(_WIN32)
            const bool candidate = extension == ".dll";
#elif defined(__APPLE__)
            const bool candidate = extension == ".dylib";
#else
            const bool candidate = extension == ".so";
#endif
            if (!candidate)
                continue;

            KUA_ModuleHandle handle = open_module(entry.path());
            if (!handle)
            {
                log_message(2, ("Could not load extension " + entry.path().string()).c_str());
                continue;
            }

            auto get_api = reinterpret_cast<KUA_GetExtensionApi>(
                find_symbol(handle, "KUA_GetExtension"));
            if (!get_api)
            {
                log_message(2, "Extension is missing KUA_GetExtension.");
                continue;
            }

            const KUA_ExtensionApi* api = get_api(KUA_EXTENSION_ABI_V1);
            if (!api || api->abi_version != KUA_EXTENSION_ABI_V1 ||
                api->struct_size < sizeof(KUA_ExtensionApi) || !api->initialize)
            {
                log_message(2, "Extension ABI mismatch or incomplete descriptor.");
                continue;
            }

            if (api->initialize(&g_host_api) == 0)
            {
                log_message(2, ("Extension initialization failed: " +
                                std::string(api->extension_id ? api->extension_id : "<unnamed>")).c_str());
                continue;
            }

            g_extensions.push_back({handle, api});
            log_message(1, ("Loaded extension: " +
                            std::string(api->extension_id ? api->extension_id : "<unnamed>")).c_str());
        }

        (void)program;
        (void)kiway;
    }

    void notify_window_created(const KUA_EditorContext& context)
    {
        for (const auto& extension : g_extensions)
        {
            if (!extension.api || !extension.api->on_editor_window_created)
                continue;

            // CreateKiWindow is a GUI-thread lifecycle callback. Invoke synchronously
            // so the wxWindow pointer cannot go stale while a deferred callback waits.
            extension.api->on_editor_window_created(&context);
        }
    }

    void shutdown_extensions()
    {
        if (g_extensions_shutdown)
            return;

#if !defined(KUA_SYNTHETIC_ABI)
        destroy_all_overlays();
#endif
        // Stop queued callbacks before extension-owned state is torn down. CallAfter
        // callbacks check this flag on the GUI thread before entering extension code.
        g_gui_callbacks_enabled.store(false, std::memory_order_release);
        g_extensions_shutdown = true;

        for (auto it = g_extensions.rbegin(); it != g_extensions.rend(); ++it)
        {
            if (it->api && it->api->shutdown)
                it->api->shutdown();
        }
        // Keep native modules loaded until process exit. KiCad may still retain
        // RTTI/vtable references originating in an extension.
    }

    KIFACE* m_original{};
    PGM_BASE* m_program{};
};

KIFACE_PROXY* g_proxy{};
} // namespace

extern "C" KUA_EXPORT KIFACE* KIFACE_1(int* version, int kiway_version, PGM_BASE* program)
{
    KIFACE_GETTER_FUNC* unused_getter = nullptr;
    if (!load_original(&unused_getter, version, kiway_version, program))
        return nullptr;

    if (!g_proxy)
        g_proxy = new KIFACE_PROXY(g_original_kiface);
    g_proxy->set_program(program);
    return g_proxy;
}
