#ifndef KICAD_UNSTABLE_API_EXTENSION_API_H
#define KICAD_UNSTABLE_API_EXTENSION_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUA_EXTENSION_ABI_V1 1u

typedef enum KUA_EditorKind
{
    KUA_EDITOR_UNKNOWN = 0,
    KUA_EDITOR_SCHEMATIC = 1,
    KUA_EDITOR_PCB = 2
} KUA_EditorKind;

/* on_editor_window_created is emitted only for the primary schematic or PCB editor frame. */
typedef struct KUA_EditorContext
{
    uint32_t struct_size;
    uint32_t editor_kind;
    int32_t class_id;
    void* frame;
    void* program;
    void* kiway;
    /* Borrowed, host-owned handles; valid only while the editor frame remains alive. */
    void* canvas;
    void* view;
} KUA_EditorContext;

typedef void (*KUA_GuiCallback)(void* user_data);

typedef struct KUA_HostApi
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* host_build_tag;
    uint32_t editor_kind;
    void* user_data;
    void (*log)(int32_t level, const char* message);
    int32_t (*post_to_gui)(KUA_GuiCallback callback, void* user_data);
} KUA_HostApi;

typedef struct KUA_ExtensionApi
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* extension_id;
    int32_t (*initialize)(const KUA_HostApi* host);
    void (*on_editor_window_created)(const KUA_EditorContext* context);
    void (*on_editor_reset)(uint32_t editor_kind);
    void (*shutdown)(void);
} KUA_ExtensionApi;

typedef const KUA_ExtensionApi* (*KUA_GetExtensionApi)(uint32_t requested_abi);

#if defined(_WIN32)
#  define KUA_EXTENSION_EXPORT __declspec(dllexport)
#else
#  define KUA_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

/*
 * Every extension module exports:
 *
 *   extern "C" KUA_EXTENSION_EXPORT
 *   const KUA_ExtensionApi* KUA_GetExtension(uint32_t requested_abi);
 *
 * ABI structs contain only fixed-width integers, C strings, opaque pointers and
 * C function pointers. Never pass wxWidgets or KiCad C++ objects by value
 * across this boundary.
 */

#ifdef __cplusplus
}
#endif

#endif
