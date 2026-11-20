#include "kicad_unstable_api/extension_api.h"

#if defined(KUA_SYNTHETIC_ABI)
#  include "synthetic_kiface.hpp"
#else
#  if defined(_WIN32) && !defined(BUILD_KIWAY_DLL)
#    define BUILD_KIWAY_DLL
#  endif
#  include <kiway.h>
#  include <wx/app.h>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
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
constexpr uint32_t kEditorKind = KUA_BUILD_EDITOR_PCB;
#else
constexpr uint32_t kEditorKind = KUA_EDITOR_SCHEMATIC;
#endif

struct LoadedExtension
{
    KUA_ModuleHandle handle{};
    const KUA_ExtensionApi* api{};
};

std::vector<LoadedExtension> g_extensions;
KUA_ModuleHandle g_original_handle{};
KIFACE* g_original_kiface{};
bool g_extensions_initialized = false;

void log_message(int level, const char* message)
{
    std::fprintf(stderr, "[kicad-unstable-api:%d] %s\n", level, message ? message : "");
    std::fflush(stderr);
}

#if defined(KUA_SYNTHETIC_ABI)
int post_to_gui(KUA_GuiCallback callback, void* user_data)
{
    if (!callback)
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
    if (!callback || !wxTheApp)
        return 0;

    auto* deferred = new DeferredCall{callback, user_data};
    wxTheApp->CallAfter([deferred]() {
        deferred->callback(deferred->user_data);
        delete deferred;
    });
    return 1;
}
#endif

KUA_HostApi make_host_api()
{
    KUA_HostApi host{};
    host.abi_version = KUA_EXTENSION_ABI_V1;
    host.struct_size = sizeof(KUA_HostApi);
    host.host_build_tag = "kicad-unstable-api/kiface-proxy-prototype";
    host.editor_kind = kEditorKind;
    host.log = &log_message;
    host.post_to_gui = &post_to_gui;
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
        if (window)
        {
            KUA_EditorContext context{};
            context.struct_size = sizeof(KUA_EditorContext);
            context.editor_kind = kEditorKind;
            context.class_id = class_id;
            context.frame = window;
            context.program = m_program;
            context.kiway = kiway;
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

        const auto extension_dir = module_path.parent_path() / "clippy-native" / "extensions";
        std::error_code ec;
        if (!std::filesystem::is_directory(extension_dir, ec))
            return;

        const auto host = make_host_api();
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

            if (api->initialize(&host) == 0)
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

            struct Notification
            {
                const KUA_ExtensionApi* api;
                KUA_EditorContext context;
            };
            auto* notification = new Notification{extension.api, context};
            post_to_gui([](void* opaque) {
                std::unique_ptr<Notification> value(static_cast<Notification*>(opaque));
                value->api->on_editor_window_created(&value->context);
            }, notification);
        }
    }

    void shutdown_extensions()
    {
        for (auto it = g_extensions.rbegin(); it != g_extensions.rend(); ++it)
        {
            if (it->api && it->api->shutdown)
                it->api->shutdown();
        }
        // Keep native modules loaded until process exit. KiCad may still have queued
        // GUI callbacks or RTTI/vtable references originating in an extension.
        g_extensions_initialized = false;
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
