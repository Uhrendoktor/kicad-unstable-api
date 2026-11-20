#include "synthetic_kiface.hpp"
#include "kicad_unstable_api/extension_api.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#if defined(_WIN32)
#  include <windows.h>
using Module = HMODULE;
Module open_module(const std::filesystem::path& p) { return LoadLibraryW(p.c_str()); }
void* symbol(Module m, const char* n) { return reinterpret_cast<void*>(GetProcAddress(m, n)); }
#else
#  include <dlfcn.h>
using Module = void*;
Module open_module(const std::filesystem::path& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL); }
void* symbol(Module m, const char* n) { return dlsym(m, n); }
#endif
[[noreturn]] void fail(const std::string& message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }
int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
        fail("usage: test_host <proxy-module> <extension-module> [expected-editor-kind]");
    const uint32_t expected_editor_kind =
        argc == 4 ? static_cast<uint32_t>(std::stoul(argv[3])) : KUA_EDITOR_SCHEMATIC;
    const std::filesystem::path proxy_path(argv[1]), extension_path(argv[2]);
    Module proxy = open_module(proxy_path);
    require(proxy != nullptr, "could not load proxy KIFACE module");
    auto getter = reinterpret_cast<KIFACE_GETTER_FUNC*>(symbol(proxy, "KIFACE_1"));
    require(getter != nullptr, "proxy does not export KIFACE_1");
    int version = 0;
    auto* iface = getter(&version, 1, reinterpret_cast<PGM_BASE*>(0x101));
    require(iface != nullptr && version == 1, "KIFACE getter/version forwarding failed");
    require(iface->OnKifaceStart(reinterpret_cast<PGM_BASE*>(0x101), 7,
                                 reinterpret_cast<KIWAY*>(0x202)),
            "OnKifaceStart failed");
    wxWindow* frame = iface->CreateKiWindow(nullptr, 123, reinterpret_cast<KIWAY*>(0x202), 9);
    require(frame == reinterpret_cast<wxWindow*>(static_cast<uintptr_t>(0x1234)),
            "CreateKiWindow result not forwarded");
    wxString s1{}, s2{}, s3{}, s4{}, s5{}, errors{};
    iface->SaveFileAs(s1, s2, s3, s4, s5, errors);
    require(iface->IfaceOrAddress(17) == reinterpret_cast<void*>(static_cast<uintptr_t>(0x4567)),
            "IfaceOrAddress not forwarded");
    std::vector<TOOL_ACTION*> actions;
    iface->GetActions(actions);
    require(actions.size() == 1, "GetActions not forwarded");
    require(iface->HandleJob(nullptr, nullptr, nullptr) == 73, "HandleJob result not forwarded");
    require(iface->HandleJobConfig(nullptr, nullptr), "HandleJobConfig result not forwarded");
    KIFACE::DOCUMENT_SPEC spec{};
    require(iface->HandleApiOpenDocument(spec, nullptr, &errors), "OpenDocument result not forwarded");
    require(iface->HandleApiCloseDocument(spec, nullptr, &errors), "CloseDocument result not forwarded");
    iface->PreloadLibraries(nullptr);
    iface->CancelPreload(false);
    iface->ProjectChanged();
    iface->RegisterLibraryHandlers(nullptr);
    require(iface->LoadAllLibraries(), "LoadAllLibraries not forwarded");

    Module extension = open_module(extension_path);
    require(extension != nullptr, "could not open extension for metrics");
    using MetricFn = int (*)(int);
    using PostAfterShutdownFn = int (*)();
    auto metric = reinterpret_cast<MetricFn>(symbol(extension, "KUA_TestExtensionMetric"));
    auto post_after_shutdown = reinterpret_cast<PostAfterShutdownFn>(
        symbol(extension, "KUA_TestPostAfterShutdown"));
    require(metric != nullptr && post_after_shutdown != nullptr,
            "extension test exports missing");
    if (metric(0) != 1 || metric(4) != 1 || metric(1) != 1 ||
        metric(5) != static_cast<int>(expected_editor_kind))
    {
        fail("extension callbacks: initialize=" + std::to_string(metric(0)) +
             ", gui=" + std::to_string(metric(4)) +
             ", window=" + std::to_string(metric(1)) +
             ", editor-kind=" + std::to_string(metric(5)) +
             ", expected=" + std::to_string(expected_editor_kind));
    }
    iface->Reset();
    require(metric(2) == 1, "extension reset callback not forwarded");
    iface->OnKifaceEnd();
    require(metric(3) == 1, "extension shutdown callback not forwarded");
    require(post_after_shutdown() == 0 && metric(4) == 1,
            "host accepted a GUI callback after extension shutdown");

    auto original_path = proxy_path.parent_path() /
        (proxy_path.stem().string() + ".original" + proxy_path.extension().string());
    Module original = open_module(original_path);
    require(original != nullptr, "preserved original module not loadable");
    using CounterFn = int (*)(int);
    auto counter = reinterpret_cast<CounterFn>(symbol(original, "KUA_OriginalCounter"));
    require(counter != nullptr, "original module counters missing");
    for (int index = 0; index < 16; ++index)
        require(counter(index) == 1,
                "original KIFACE forwarding failed at slot " + std::to_string(index));
    std::cout << "PASS: KIFACE forwarding, original retention, extension discovery and lifecycle.\n";
    return 0;
}
