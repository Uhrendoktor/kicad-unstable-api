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
enum Metric { Initialize, WindowCreated, Reset, Shutdown, GuiCallback, EditorKind, MetricCount };
std::array<int, MetricCount> metrics{};
void callback(void*) { ++metrics[GuiCallback]; }
int initialize(const KUA_HostApi* host)
{
 ++metrics[Initialize];
 if (!host || host->abi_version != KUA_EXTENSION_ABI_V1 ||\n     (host->editor_kind != KUA_EDITOR_SCHEMATIC && host->editor_kind != KUA_EDITOR_PCB) ||\n     !host->post_to_gui) return 0;\n metrics[EditorKind] = static_cast<int>(host->editor_kind);
 host->post_to_gui(&callback, nullptr);
 return 1;
}
void on_window(const KUA_EditorContext* context)
{
 if (context && context->editor_kind == static_cast<uint32_t>(metrics[EditorKind]) && context->class_id == 123 &&
     context->frame == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234))) ++metrics[WindowCreated];
}
void on_reset(uint32_t editor_kind) { if (editor_kind == KUA_EDITOR_SCHEMATIC) ++metrics[Reset]; }
void shutdown() { ++metrics[Shutdown]; }
const KUA_ExtensionApi api{KUA_EXTENSION_ABI_V1, sizeof(KUA_ExtensionApi), "test.extension",
                           &initialize, &on_window, &on_reset, &shutdown};
}
extern "C" KUA_EXTENSION_EXPORT const KUA_ExtensionApi* KUA_GetExtension(uint32_t requested_abi)
{ return requested_abi == KUA_EXTENSION_ABI_V1 ? &api : nullptr; }
extern "C" KUA_EXTENSION_EXPORT int KUA_TestExtensionMetric(int index)
{ if (index < 0 || index >= MetricCount) return -1; return metrics[static_cast<size_t>(index)]; }
