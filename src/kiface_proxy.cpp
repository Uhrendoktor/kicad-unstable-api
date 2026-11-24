#include "kicad_unstable_api/extension_api.h"

#if defined(KUA_SYNTHETIC_ABI)
#  include "synthetic_kiface.hpp"
#else
#  include <kiway.h>
#  include <eda_draw_frame.h>
#  include <wx/app.h>
#  include <wx/thread.h>
#  include <wx/weakref.h>
#  include <wx/dialog.h>
#  include <wx/frame.h>
#  include <wx/panel.h>
#  include <wx/notebook.h>
#  include <wx/button.h>
#  include <wx/textctrl.h>
#  include <wx/checkbox.h>
#  include <wx/choice.h>
#  include <wx/stattext.h>
#  include <wx/sizer.h>
#  include <wx/menu.h>
#  include <wx/eventfilter.h>
#  include <wx/event.h>
#  include <wx/aui/aui.h>
#  include <wx/propgrid/propgrid.h>
#  include <wx/propgrid/advprops.h>
#  include <widgets/properties_panel.h>
#  include <view/view.h>
#  include <view/view_overlay.h>
#  include <gal/color4d.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <cstring>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
bool on_gui_thread();
struct OverlayState
{
    void* frame{};
    EDA_DRAW_PANEL_GAL* canvas{};
    KIGFX::VIEW* view{};
    std::shared_ptr<KIGFX::VIEW_OVERLAY> overlay;
};

std::unordered_map<uint64_t, std::unique_ptr<OverlayState>> g_overlays;
uint64_t g_next_overlay_handle = 1;
struct EditorFrameState { EDA_DRAW_PANEL_GAL* canvas{}; KIGFX::VIEW* view{}; };
std::unordered_map<void*, EditorFrameState> g_editor_frames;

enum class UiHandleKind { Window, Sizer, MenuBar, Menu, AuiManager, PropertyGrid };

struct UiHandleEntry
{
    UiHandleKind kind{};
    void* pointer{};
    void* frame{};
    wxWindow* owner_window{};
};

struct KuaMenuEventHandler : public wxEvtHandler
{
    KUA_GuiCallback callback{};
    void* user_data{};

    void on_menu(wxCommandEvent&)
    {
        if (callback)
            callback(user_data);
    }
};

struct MenuBindingEntry
{
    wxWindow* frame{};
    int id{};
    std::unique_ptr<KuaMenuEventHandler> handler;
};

struct WindowObserverEntry
{
    wxWindow* frame{};
    KUA_UiWindowEventCallback callback{};
    void* user_data{};
};

std::unordered_map<uint64_t, UiHandleEntry> g_ui_handles;
std::unordered_map<uint64_t, MenuBindingEntry> g_menu_bindings;
std::unordered_map<uint64_t, WindowObserverEntry> g_window_observers;
std::unordered_set<wxWindow*> g_watched_ui_windows;
uint64_t g_next_ui_handle = 1;
bool g_window_event_filter_installed = false;

bool on_gui_thread();
void forget_ui_handles_for_frame(void* frame);

uint64_t register_ui_handle(void* pointer, UiHandleKind kind, void* frame,
                            wxWindow* owner_window = nullptr)
{
    if (!pointer)
        return 0;

    for (auto& [handle, entry] : g_ui_handles)
    {
        if (entry.pointer == pointer && entry.kind == kind)
        {
            if (!entry.frame)
                entry.frame = frame;
            if (!entry.owner_window)
                entry.owner_window = owner_window;
            return handle;
        }
    }

    uint64_t handle = g_next_ui_handle++;
    if (handle == 0)
        handle = g_next_ui_handle++;
    g_ui_handles.emplace(handle, UiHandleEntry{kind, pointer, frame, owner_window});
    return handle;
}

UiHandleEntry* find_ui_handle(uint64_t handle, UiHandleKind kind)
{
    const auto it = g_ui_handles.find(handle);
    if (it == g_ui_handles.end() || it->second.kind != kind)
        return nullptr;
    return &it->second;
}

wxWindow* find_ui_window(uint64_t handle)
{
    UiHandleEntry* entry = find_ui_handle(handle, UiHandleKind::Window);
    return entry ? static_cast<wxWindow*>(entry->pointer) : nullptr;
}

void remove_menu_bindings_for_window(wxWindow* frame)
{
    if (!frame)
        return;

    for (auto it = g_menu_bindings.begin(); it != g_menu_bindings.end();)
    {
        if (it->second.frame == frame)
        {
            frame->Unbind(wxEVT_MENU, &KuaMenuEventHandler::on_menu,
                          it->second.handler.get(), it->second.id, it->second.id);
            it = g_menu_bindings.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void clear_window_observers_for_frame(wxWindow* frame);

void invalidate_ui_owner_handles(wxWindow* owner_window)
{
    if (!owner_window)
        return;

    // A native owner may destroy non-window members (for example a
    // wxAuiManager stored in the managed wxWindow) before its base wxWindow
    // destructor emits wxEVT_DESTROY. Revoke those borrowed handles before
    // invoking extension observers; retain the window's own handle so the
    // DESTROYING callback can inspect the native window itself.
    for (auto it = g_ui_handles.begin(); it != g_ui_handles.end();)
    {
        const bool is_owner_window_handle =
            it->second.kind == UiHandleKind::Window &&
            it->second.pointer == owner_window;

        if (it->second.owner_window == owner_window && !is_owner_window_handle)
            it = g_ui_handles.erase(it);
        else
            ++it;
    }
}

void invalidate_ui_window(wxWindow* window, bool destroying = false)
{
    if (!window)
        return;

    for (auto it = g_ui_handles.begin(); it != g_ui_handles.end();)
    {
        if ((it->second.kind == UiHandleKind::Window && it->second.pointer == window) ||
            it->second.owner_window == window)
            it = g_ui_handles.erase(it);
        else
            ++it;
    }

    remove_menu_bindings_for_window(window);
    clear_window_observers_for_frame(window);
    if (destroying)
        g_watched_ui_windows.erase(window);
}

void install_ui_window_destroy_hook(wxWindow* window)
{
    if (!window || !g_watched_ui_windows.insert(window).second)
        return;

    window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent& event) {
        // wxWindowDestroyEvent derives from wxCommandEvent and can propagate
        // through parent handlers. This hook is installed on every registered
        // window, so only invalidate the window that the event actually names;
        // otherwise a child's destroy event would invalidate its editor parent.
        if (event.GetWindow() != window)
        {
            event.Skip();
            return;
        }

        invalidate_ui_window(window, true);
        if (g_editor_frames.find(window) != g_editor_frames.end())
        {
            g_editor_frames.erase(window);
            forget_ui_handles_for_frame(window);
        }
        event.Skip();
    });
}

uint64_t register_ui_window(wxWindow* window, void* frame)
{
    if (!window)
        return 0;
    const uint64_t handle = register_ui_handle(window, UiHandleKind::Window, frame, window);
    if (handle)
        install_ui_window_destroy_hook(window);
    return handle;
}

void forget_ui_handles_for_frame(void* frame)
{
    remove_menu_bindings_for_window(static_cast<wxWindow*>(frame));
    clear_window_observers_for_frame(static_cast<wxWindow*>(frame));

    for (auto it = g_ui_handles.begin(); it != g_ui_handles.end();)
    {
        if (it->second.frame == frame)
            it = g_ui_handles.erase(it);
        else
            ++it;
    }
}

void forget_ui_window_tree(wxWindow* window)
{
    if (!window)
        return;

    const wxWindowList& children = window->GetChildren();
    std::vector<wxWindow*> child_windows;
    for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext())
        child_windows.push_back(node->GetData());

    invalidate_ui_window(window);
    for (wxWindow* child : child_windows)
        forget_ui_window_tree(child);
}

bool window_belongs_to_editor(wxWindow* window, wxWindow* editor_frame)
{
    for (wxWindow* current = window; current; current = current->GetParent())
    {
        if (current == editor_frame)
            return true;
    }
    return false;
}

class KuaWindowEventFilter final : public wxEventFilter
{
public:
    int FilterEvent(wxEvent& event) override
    {
        uint32_t event_kind = 0;
        bool destroying = false;

        if (event.GetEventType() == wxEVT_SHOW)
        {
            auto* show_event = dynamic_cast<wxShowEvent*>(&event);
            if (!show_event)
                return Event_Skip;
            event_kind = show_event->IsShown() ? KUA_UI_WINDOW_SHOWN : KUA_UI_WINDOW_HIDDEN;
        }
        else if (event.GetEventType() == wxEVT_DESTROY)
        {
            event_kind = KUA_UI_WINDOW_DESTROYING;
            destroying = true;
        }
        else
        {
            return Event_Skip;
        }

        auto* window = dynamic_cast<wxWindow*>(event.GetEventObject());
        if (!window || !on_gui_thread())
            return Event_Skip;

        std::vector<WindowObserverEntry> observers;
        observers.reserve(g_window_observers.size());
        for (const auto& [handle, observer] : g_window_observers)
        {
            (void)handle;
            if (window_belongs_to_editor(window, observer.frame))
                observers.push_back(observer);
        }

        // wxWidgets emits wxEVT_DESTROY from the base wxWindow destructor,
        // after a derived window's members have already been destroyed. Revoke
        // handles to owner-bound native members before extension callbacks.
        if (destroying)
            invalidate_ui_owner_handles(window);

        for (const WindowObserverEntry& observer : observers)
        {
            const uint64_t window_handle = destroying
                ? register_ui_handle(window, UiHandleKind::Window, observer.frame, window)
                : register_ui_window(window, observer.frame);
            if (observer.callback && window_handle)
                observer.callback(window_handle, event_kind, observer.user_data);
        }

        if (destroying)
            invalidate_ui_window(window, true);

        return Event_Skip;
    }
};

KuaWindowEventFilter g_window_event_filter;

void clear_window_observers_for_frame(wxWindow* frame)
{
    if (!frame)
        return;

    for (auto it = g_window_observers.begin(); it != g_window_observers.end();)
    {
        if (it->second.frame == frame)
            it = g_window_observers.erase(it);
        else
            ++it;
    }

    // Do not remove the filter from inside FilterEvent(): wxWidgets iterates
    // the filter linked list while invoking it. With no observers it continues
    // native handle invalidation, but emits no plugin callbacks, until shutdown.
}

void copy_utf8_to_buffer(const wxString& value, char* buffer, uint32_t capacity)
{
    if (!buffer || capacity == 0)
        return;

    const wxScopedCharBuffer utf8 = value.utf8_str();
    const char* source = utf8.data();
    const size_t count = source ? std::min<size_t>(std::strlen(source), capacity - 1) : 0;
    if (count)
        std::memcpy(buffer, source, count);
    buffer[count] = '\0';
}

uint64_t ui_window_get_handle(void* borrowed_window)
{
    if (!on_gui_thread() || !borrowed_window)
        return 0;

    auto* window = static_cast<wxWindow*>(borrowed_window);
    for (const auto& [frame, state] : g_editor_frames)
    {
        (void)state;
        auto* editor_frame = static_cast<wxWindow*>(frame);
        if (window_belongs_to_editor(window, editor_frame))
            return register_ui_window(window, frame);
    }

    return 0;
}

uint64_t ui_window_create(uint64_t parent_handle, uint32_t kind, int32_t id,
                          const char* text, int32_t style)
{
    if (!on_gui_thread())
        return 0;

    wxWindow* parent = find_ui_window(parent_handle);
    if (!parent)
        return 0;

    const wxWindowID window_id = id == -1 ? wxID_ANY : id;
    const wxString label = wxString::FromUTF8(text ? text : "");
    wxWindow* window = nullptr;

    switch (kind)
    {
    case KUA_UI_WINDOW_PANEL:
        window = new wxPanel(parent, window_id);
        break;
    case KUA_UI_WINDOW_DIALOG:
        window = new wxDialog(parent, window_id, label, wxDefaultPosition, wxDefaultSize, style);
        break;
    case KUA_UI_WINDOW_FRAME:
        window = new wxFrame(parent, window_id, label, wxDefaultPosition, wxDefaultSize, style);
        break;
    case KUA_UI_WINDOW_NOTEBOOK:
        window = new wxNotebook(parent, window_id, wxDefaultPosition, wxDefaultSize, style);
        break;
    case KUA_UI_WINDOW_STATIC_TEXT:
        window = new wxStaticText(parent, window_id, label);
        break;
    case KUA_UI_WINDOW_BUTTON:
        window = new wxButton(parent, window_id, label);
        break;
    case KUA_UI_WINDOW_TEXT_CTRL:
        window = new wxTextCtrl(parent, window_id, label);
        break;
    case KUA_UI_WINDOW_CHECK_BOX:
        window = new wxCheckBox(parent, window_id, label);
        break;
    case KUA_UI_WINDOW_CHOICE:
        window = new wxChoice(parent, window_id);
        break;
    default:
        return 0;
    }

    if (!window)
        return 0;

    return register_ui_window(window, g_ui_handles[parent_handle].frame);
}

int32_t ui_window_destroy(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;

    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;

    forget_ui_window_tree(window);
    return window->Destroy() ? 1 : 0;
}

int32_t ui_window_show(uint64_t handle, int32_t show)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    return window ? (window->Show(show != 0) ? 1 : 0) : -1;
}

int32_t ui_window_get_children(uint64_t parent_handle, uint64_t* handles, uint32_t capacity)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* parent = find_ui_window(parent_handle);
    if (!parent)
        return -1;

    const wxWindowList& children = parent->GetChildren();
    int32_t count = 0;
    for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext())
    {
        if (handles && static_cast<uint32_t>(count) < capacity)
            handles[count] = register_ui_window(node->GetData(), g_ui_handles[parent_handle].frame);
        ++count;
    }
    return count;
}

int32_t ui_window_get_top_level_windows(uint64_t editor_frame_handle,
                                            uint64_t* handles, uint32_t capacity)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* editor_frame = find_ui_window(editor_frame_handle);
    if (!editor_frame)
        return -1;

    int32_t count = 0;
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst();
         node; node = node->GetNext())
    {
        wxWindow* window = node->GetData();
        if (!window_belongs_to_editor(window, editor_frame))
            continue;
        if (handles && static_cast<uint32_t>(count) < capacity)
            handles[count] = register_ui_window(window, editor_frame);
        ++count;
    }
    return count;
}

uint64_t ui_window_observe(uint64_t editor_frame_handle,
                           KUA_UiWindowEventCallback callback, void* user_data)
{
    if (!on_gui_thread() || !callback)
        return 0;
    wxWindow* frame = find_ui_window(editor_frame_handle);
    if (!frame)
        return 0;

    uint64_t observer = g_next_ui_handle++;
    if (observer == 0)
        observer = g_next_ui_handle++;
    const bool first_observer = g_window_observers.empty();
    g_window_observers.emplace(observer, WindowObserverEntry{frame, callback, user_data});
    if (first_observer && !g_window_event_filter_installed)
    {
        wxEvtHandler::AddFilter(&g_window_event_filter);
        g_window_event_filter_installed = true;
    }
    return observer;
}

int32_t ui_window_unobserve(uint64_t observer)
{
    if (!on_gui_thread())
        return -1;
    const auto it = g_window_observers.find(observer);
    if (it == g_window_observers.end())
        return -1;
    g_window_observers.erase(it);
    // The filter remains installed and becomes a no-op until host shutdown.
    return 1;
}

int32_t ui_window_get_class_name(uint64_t handle, char* buffer, uint32_t capacity)
{
    if (!on_gui_thread() || !buffer || capacity == 0)
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    wxClassInfo* info = window->GetClassInfo();
    copy_utf8_to_buffer(info ? wxString(info->GetClassName()) : wxString("wxWindow"),
                        buffer, capacity);
    return 1;
}

int32_t ui_window_set_text(uint64_t handle, const char* text)
{
    if (!on_gui_thread() || !text)
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    if (auto* top_level = dynamic_cast<wxTopLevelWindow*>(window))
        top_level->SetTitle(wxString::FromUTF8(text));
    else
        window->SetLabel(wxString::FromUTF8(text));
    return 1;
}

int32_t ui_text_ctrl_set_value(uint64_t handle, const char* value)
{
    if (!on_gui_thread() || !value)
        return -1;
    auto* control = dynamic_cast<wxTextCtrl*>(find_ui_window(handle));
    if (!control)
        return -1;
    control->SetValue(wxString::FromUTF8(value));
    return 1;
}

int32_t ui_text_ctrl_get_value(uint64_t handle, char* buffer, uint32_t capacity)
{
    if (!on_gui_thread() || !buffer || capacity == 0)
        return -1;
    auto* control = dynamic_cast<wxTextCtrl*>(find_ui_window(handle));
    if (!control)
        return -1;
    copy_utf8_to_buffer(control->GetValue(), buffer, capacity);
    return 1;
}

int32_t ui_check_box_set_value(uint64_t handle, int32_t checked)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxCheckBox*>(find_ui_window(handle));
    if (!control)
        return -1;
    control->SetValue(checked != 0);
    return 1;
}

int32_t ui_check_box_get_value(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxCheckBox*>(find_ui_window(handle));
    return control ? (control->GetValue() ? 1 : 0) : -1;
}

int32_t ui_choice_append(uint64_t handle, const char* item)
{
    if (!on_gui_thread() || !item)
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    if (!control)
        return -1;
    return control->Append(wxString::FromUTF8(item)) == wxNOT_FOUND ? -1 : 1;
}

int32_t ui_choice_clear(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    if (!control)
        return -1;
    control->Clear();
    return 1;
}

int32_t ui_choice_get_count(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    if (!control || control->GetCount() > static_cast<unsigned int>(std::numeric_limits<int32_t>::max()))
        return -1;
    return static_cast<int32_t>(control->GetCount());
}

int32_t ui_choice_set_selection(uint64_t handle, int32_t selection)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    if (!control || selection < wxNOT_FOUND ||
        (selection != wxNOT_FOUND && static_cast<unsigned int>(selection) >= control->GetCount()))
        return -1;
    control->SetSelection(selection);
    return 1;
}

int32_t ui_choice_get_selection(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    return control ? control->GetSelection() : -1;
}

int32_t ui_choice_get_string_selection(uint64_t handle, char* buffer, uint32_t capacity)
{
    if (!on_gui_thread() || !buffer || capacity == 0)
        return -1;
    auto* control = dynamic_cast<wxChoice*>(find_ui_window(handle));
    if (!control)
        return -1;
    copy_utf8_to_buffer(control->GetStringSelection(), buffer, capacity);
    return 1;
}

int32_t ui_window_enable(uint64_t handle, int32_t enabled)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    window->Enable(enabled != 0);
    return 1;
}

int32_t ui_window_raise(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    window->Raise();
    return 1;
}

int32_t ui_window_set_focus(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    window->SetFocus();
    return 1;
}

int32_t ui_window_set_size(uint64_t handle, int32_t width, int32_t height)
{
    if (!on_gui_thread() || width < 0 || height < 0)
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    window->SetSize(wxSize(width, height));
    return 1;
}

int32_t ui_window_get_size(uint64_t handle, int32_t* width, int32_t* height)
{
    if (!on_gui_thread() || !width || !height)
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    const wxSize size = window->GetSize();
    *width = size.GetWidth();
    *height = size.GetHeight();
    return 1;
}

int32_t ui_window_set_position(uint64_t handle, int32_t x, int32_t y)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    window->Move(wxPoint(x, y));
    return 1;
}

int32_t ui_window_get_position(uint64_t handle, int32_t* x, int32_t* y)
{
    if (!on_gui_thread() || !x || !y)
        return -1;
    wxWindow* window = find_ui_window(handle);
    if (!window)
        return -1;
    const wxPoint position = window->GetPosition();
    *x = position.x;
    *y = position.y;
    return 1;
}
uint64_t ui_sizer_create(uint32_t kind, int32_t orientation,
                         int32_t rows, int32_t cols, int32_t vgap, int32_t hgap)
{
    if (!on_gui_thread())
        return 0;
    wxSizer* sizer = nullptr;
    if (kind == KUA_UI_SIZER_BOX)
        sizer = new wxBoxSizer(orientation);
    else if (kind == KUA_UI_SIZER_FLEX_GRID && rows >= 0 && cols >= 0 && vgap >= 0 && hgap >= 0)
        sizer = new wxFlexGridSizer(rows, cols, vgap, hgap);
    return register_ui_handle(sizer, UiHandleKind::Sizer, nullptr);
}

bool ui_sizer_contains(wxSizer* root, wxSizer* target)
{
    if (!root || !target)
        return false;
    if (root == target)
        return true;

    const wxSizerItemList& children = root->GetChildren();
    for (wxSizerItemList::compatibility_iterator node = children.GetFirst();
         node; node = node->GetNext())
    {
        wxSizerItem* item = node->GetData();
        if (item && item->IsSizer() && ui_sizer_contains(item->GetSizer(), target))
            return true;
    }
    return false;
}

void set_sizer_owner_recursive(wxSizer* sizer, wxWindow* owner_window, void* frame)
{
    if (!sizer)
        return;

    for (auto& [handle, entry] : g_ui_handles)
    {
        (void)handle;
        if (entry.kind == UiHandleKind::Sizer && entry.pointer == sizer)
        {
            entry.owner_window = owner_window;
            entry.frame = frame;
            break;
        }
    }

    const wxSizerItemList& children = sizer->GetChildren();
    for (wxSizerItemList::compatibility_iterator node = children.GetFirst();
         node; node = node->GetNext())
    {
        wxSizerItem* item = node->GetData();
        if (item && item->IsSizer())
            set_sizer_owner_recursive(item->GetSizer(), owner_window, frame);
    }
}

void forget_ui_sizer_tree_handles(wxSizer* sizer)
{
    if (!sizer)
        return;

    std::vector<wxSizer*> child_sizers;
    const wxSizerItemList& children = sizer->GetChildren();
    for (wxSizerItemList::compatibility_iterator node = children.GetFirst();
         node; node = node->GetNext())
    {
        wxSizerItem* item = node->GetData();
        if (item && item->IsSizer())
            child_sizers.push_back(item->GetSizer());
    }

    for (wxSizer* child : child_sizers)
        forget_ui_sizer_tree_handles(child);

    for (auto it = g_ui_handles.begin(); it != g_ui_handles.end();)
    {
        if (it->second.kind == UiHandleKind::Sizer && it->second.pointer == sizer)
            it = g_ui_handles.erase(it);
        else
            ++it;
    }
}

int32_t ui_sizer_add(uint64_t sizer_handle, uint64_t child_handle,
                     int32_t proportion, int32_t flags, int32_t border)
{
    if (!on_gui_thread())
        return -1;
    UiHandleEntry* sizer_entry = find_ui_handle(sizer_handle, UiHandleKind::Sizer);
    if (!sizer_entry)
        return -1;
    UiHandleEntry* child_entry = nullptr;
    const auto child_it = g_ui_handles.find(child_handle);
    if (child_it != g_ui_handles.end())
        child_entry = &child_it->second;
    if (!child_entry)
        return -1;

    auto* sizer = static_cast<wxSizer*>(sizer_entry->pointer);
    if (child_entry->kind == UiHandleKind::Window)
    {
        sizer->Add(static_cast<wxWindow*>(child_entry->pointer), proportion, flags, border);
    }
    else if (child_entry->kind == UiHandleKind::Sizer)
    {
        auto* child_sizer = static_cast<wxSizer*>(child_entry->pointer);
        // Do not let a sizer already owned by a window be attached twice.
        if (child_entry->owner_window || ui_sizer_contains(sizer, child_sizer) ||
            ui_sizer_contains(child_sizer, sizer))
            return -1;

        sizer->Add(child_sizer, proportion, flags, border);
        if (sizer_entry->owner_window)
            set_sizer_owner_recursive(child_sizer, sizer_entry->owner_window,
                                      sizer_entry->frame);
    }
    else
    {
        return -1;
    }

    return 1;
}

int32_t ui_window_set_sizer(uint64_t window_handle, uint64_t sizer_handle)
{
    if (!on_gui_thread())
        return -1;

    wxWindow* window = find_ui_window(window_handle);
    UiHandleEntry* window_entry = find_ui_handle(window_handle, UiHandleKind::Window);
    UiHandleEntry* sizer_entry = find_ui_handle(sizer_handle, UiHandleKind::Sizer);
    if (!window || !window_entry || !sizer_entry)
        return -1;

    auto* new_sizer = static_cast<wxSizer*>(sizer_entry->pointer);
    if (!new_sizer || (sizer_entry->owner_window && sizer_entry->owner_window != window))
        return -1;

    wxSizer* old_sizer = window->GetSizer();
    if (old_sizer == new_sizer)
        return 1;

    // SetSizer() deletes the old root sizer by default. Invalidate handles for
    // the whole old tree before wxWidgets recursively destroys its children.
    if (old_sizer && ui_sizer_contains(old_sizer, new_sizer))
        return -1;
    if (old_sizer)
        forget_ui_sizer_tree_handles(old_sizer);

    window->SetSizer(new_sizer);
    set_sizer_owner_recursive(new_sizer, window, window_entry->frame);
    return 1;
}

int32_t ui_window_layout(uint64_t handle)
{
    if (!on_gui_thread())
        return -1;
    wxWindow* window = find_ui_window(handle);
    return window && window->Layout() ? 1 : (window ? 0 : -1);
}

int32_t ui_notebook_add_page(uint64_t notebook_handle, uint64_t page_handle,
                             const char* label, int32_t select)
{
    if (!on_gui_thread() || !label)
        return -1;
    auto* notebook = dynamic_cast<wxNotebook*>(find_ui_window(notebook_handle));
    wxWindow* page = find_ui_window(page_handle);
    if (!notebook || !page)
        return -1;
    return notebook->AddPage(page, wxString::FromUTF8(label), select != 0) ? 1 : 0;
}

int32_t ui_notebook_insert_page(uint64_t notebook_handle, uint32_t index, uint64_t page_handle,
                                const char* label, int32_t select)
{
    if (!on_gui_thread() || !label)
        return -1;
    auto* notebook = wxDynamicCast(find_ui_window(notebook_handle), wxNotebook);
    wxWindow* page = find_ui_window(page_handle);
    if (!notebook || !page)
        return -1;
    return notebook->InsertPage(index, page, wxString::FromUTF8(label), select != 0) ? 1 : 0;
}

int32_t ui_notebook_remove_page(uint64_t notebook_handle, uint32_t index)
{
    if (!on_gui_thread())
        return -1;
    auto* notebook = wxDynamicCast(find_ui_window(notebook_handle), wxNotebook);
    return notebook ? (notebook->RemovePage(index) ? 1 : 0) : -1;
}

int32_t ui_notebook_get_page_count(uint64_t notebook_handle)
{
    if (!on_gui_thread())
        return -1;
    auto* notebook = wxDynamicCast(find_ui_window(notebook_handle), wxNotebook);
    return notebook ? static_cast<int32_t>(notebook->GetPageCount()) : -1;
}

int32_t ui_notebook_set_selection(uint64_t notebook_handle, int32_t index)
{
    if (!on_gui_thread())
        return -1;
    auto* notebook = wxDynamicCast(find_ui_window(notebook_handle), wxNotebook);
    if (!notebook || index < 0 || static_cast<size_t>(index) >= notebook->GetPageCount())
        return -1;
    notebook->SetSelection(static_cast<size_t>(index));
    return 1;
}

int32_t ui_notebook_get_page_text(uint64_t notebook_handle, uint32_t index,
                                  char* buffer, uint32_t capacity)
{
    if (!on_gui_thread() || !buffer || capacity == 0)
        return -1;
    auto* notebook = wxDynamicCast(find_ui_window(notebook_handle), wxNotebook);
    if (!notebook || index >= notebook->GetPageCount())
        return -1;
    copy_utf8_to_buffer(notebook->GetPageText(index), buffer, capacity);
    return 1;
}

uint64_t ui_menu_bar_get(uint64_t frame_handle)
{
    if (!on_gui_thread())
        return 0;
    wxWindow* window = find_ui_window(frame_handle);
    auto* frame = dynamic_cast<wxFrame*>(window);
    return frame ? register_ui_handle(frame->GetMenuBar(), UiHandleKind::MenuBar,
                                      g_ui_handles[frame_handle].frame, frame) : 0;
}

uint64_t ui_menu_create(uint64_t frame_handle)
{
    if (!on_gui_thread() || !find_ui_window(frame_handle))
        return 0;
    return register_ui_handle(new wxMenu(), UiHandleKind::Menu,
                              g_ui_handles[frame_handle].frame, find_ui_window(frame_handle));
}

int32_t ui_menu_append(uint64_t menu_handle, int32_t id, const char* label,
                       const char* help, uint32_t kind)
{
    if (!on_gui_thread() || !label)
        return -1;
    UiHandleEntry* entry = find_ui_handle(menu_handle, UiHandleKind::Menu);
    if (!entry)
        return -1;
    wxItemKind item_kind = wxITEM_NORMAL;
    if (kind == KUA_UI_MENU_CHECK)
        item_kind = wxITEM_CHECK;
    else if (kind == KUA_UI_MENU_RADIO)
        item_kind = wxITEM_RADIO;
    else if (kind != KUA_UI_MENU_NORMAL)
        return -1;
    static_cast<wxMenu*>(entry->pointer)->Append(id, wxString::FromUTF8(label),
        wxString::FromUTF8(help ? help : ""), item_kind);
    return 1;
}

int32_t ui_menu_delete(uint64_t menu_handle, int32_t id)
{
    if (!on_gui_thread())
        return -1;
    UiHandleEntry* entry = find_ui_handle(menu_handle, UiHandleKind::Menu);
    return entry && static_cast<wxMenu*>(entry->pointer)->Delete(id) ? 1 : -1;
}

int32_t ui_menu_bar_append(uint64_t menu_bar_handle, uint64_t menu_handle, const char* title)
{
    if (!on_gui_thread() || !title)
        return -1;
    UiHandleEntry* bar = find_ui_handle(menu_bar_handle, UiHandleKind::MenuBar);
    UiHandleEntry* menu = find_ui_handle(menu_handle, UiHandleKind::Menu);
    if (!bar || !menu)
        return -1;
    const bool ok = static_cast<wxMenuBar*>(bar->pointer)->Append(
        static_cast<wxMenu*>(menu->pointer), wxString::FromUTF8(title));
    return ok ? 1 : 0;
}

int32_t ui_menu_bar_remove(uint64_t menu_bar_handle, uint32_t index)
{
    if (!on_gui_thread())
        return -1;
    UiHandleEntry* bar = find_ui_handle(menu_bar_handle, UiHandleKind::MenuBar);
    if (!bar)
        return -1;
    wxMenu* menu = static_cast<wxMenuBar*>(bar->pointer)->Remove(index);
    return menu ? 1 : 0;
}

uint64_t ui_menu_bind(uint64_t frame_handle, int32_t id, KUA_GuiCallback callback, void* user_data)
{
    if (!on_gui_thread() || !callback)
        return 0;
    wxWindow* frame = find_ui_window(frame_handle);
    if (!frame)
        return 0;

    for (const auto& [binding, entry] : g_menu_bindings)
    {
        (void)binding;
        if (entry.frame == frame && entry.id == id)
            return 0;
    }

    auto handler = std::make_unique<KuaMenuEventHandler>();
    handler->callback = callback;
    handler->user_data = user_data;
    frame->Bind(wxEVT_MENU, &KuaMenuEventHandler::on_menu, handler.get(), id, id);

    uint64_t binding = g_next_ui_handle++;
    if (binding == 0)
        binding = g_next_ui_handle++;
    g_menu_bindings.emplace(binding, MenuBindingEntry{frame, id, std::move(handler)});
    return binding;
}

int32_t ui_menu_unbind(uint64_t binding)
{
    if (!on_gui_thread())
        return -1;
    const auto it = g_menu_bindings.find(binding);
    if (it == g_menu_bindings.end())
        return -1;
    if (!it->second.frame->Unbind(wxEVT_MENU, &KuaMenuEventHandler::on_menu,
                                  it->second.handler.get(), it->second.id, it->second.id))
        return -1;
    g_menu_bindings.erase(it);
    return 1;
}

uint64_t ui_aui_get_manager(uint64_t window_handle)
{
    if (!on_gui_thread())
        return 0;
    wxWindow* window = find_ui_window(window_handle);
    UiHandleEntry* window_entry = find_ui_handle(window_handle, UiHandleKind::Window);
    if (!window || !window_entry || !window_entry->frame)
        return 0;

    wxAuiManager* manager = wxAuiManager::GetManager(window);
    wxWindow* managed_window = manager ? manager->GetManagedWindow() : nullptr;
    auto* editor_frame = static_cast<wxWindow*>(window_entry->frame);
    if (!manager || !managed_window ||
        !window_belongs_to_editor(managed_window, editor_frame))
        return 0;

    // The manager is tied to its managed window, which may be a child of the
    // KiCad frame. Ensure native destruction invalidates this handle even when
    // that managed window was never wrapped through another KUA call.
    install_ui_window_destroy_hook(managed_window);
    return register_ui_handle(manager, UiHandleKind::AuiManager,
                              window_entry->frame, managed_window);
}

int32_t ui_aui_add_pane(uint64_t manager_handle, uint64_t window_handle,
                        const KUA_AuiPaneOptions* options)
{
    if (!on_gui_thread() || !options ||
        options->struct_size < sizeof(KUA_AuiPaneOptions) || !options->name)
        return -1;
    UiHandleEntry* manager_entry = find_ui_handle(manager_handle, UiHandleKind::AuiManager);
    wxWindow* window = find_ui_window(window_handle);
    if (!manager_entry || !window)
        return -1;

    wxAuiPaneInfo pane;
    pane.Name(wxString::FromUTF8(options->name))
        .Caption(wxString::FromUTF8(options->caption ? options->caption : options->name))
        .Layer(options->layer).Row(options->row).Position(options->position);

    switch (options->direction)
    {
    case KUA_AUI_LEFT: pane.Left(); break;
    case KUA_AUI_RIGHT: pane.Right(); break;
    case KUA_AUI_TOP: pane.Top(); break;
    case KUA_AUI_BOTTOM: pane.Bottom(); break;
    case KUA_AUI_CENTER: pane.CenterPane(); break;
    case KUA_AUI_FLOATING: pane.Float(); break;
    default: return -1;
    }

    if (options->best_width > 0 && options->best_height > 0)
        pane.BestSize(options->best_width, options->best_height);
    if (options->min_width > 0 && options->min_height > 0)
        pane.MinSize(options->min_width, options->min_height);

    pane.CloseButton((options->flags & KUA_AUI_CLOSE_BUTTON) != 0)
        .PinButton((options->flags & KUA_AUI_PIN_BUTTON) != 0)
        .CaptionVisible((options->flags & KUA_AUI_CAPTION_VISIBLE) != 0)
        .PaneBorder((options->flags & KUA_AUI_PANE_BORDER) != 0)
        .Movable((options->flags & KUA_AUI_MOVABLE) != 0)
        .Resizable((options->flags & KUA_AUI_RESIZABLE) != 0)
        .Floatable((options->flags & KUA_AUI_FLOATABLE) != 0)
        .Dockable((options->flags & KUA_AUI_DOCKABLE) != 0);

    return static_cast<wxAuiManager*>(manager_entry->pointer)->AddPane(window, pane) ? 1 : 0;
}

int32_t ui_aui_detach_pane(uint64_t manager_handle, uint64_t window_handle)
{
    if (!on_gui_thread())
        return -1;
    UiHandleEntry* manager = find_ui_handle(manager_handle, UiHandleKind::AuiManager);
    wxWindow* window = find_ui_window(window_handle);
    if (!manager || !window)
        return -1;
    return static_cast<wxAuiManager*>(manager->pointer)->DetachPane(window) ? 1 : 0;
}

int32_t ui_aui_update(uint64_t manager_handle)
{
    if (!on_gui_thread())
        return -1;
    UiHandleEntry* manager = find_ui_handle(manager_handle, UiHandleKind::AuiManager);
    if (!manager)
        return -1;
    static_cast<wxAuiManager*>(manager->pointer)->Update();
    return 1;
}

uint64_t ui_properties_get_panel(uint64_t frame_handle)
{
    if (!on_gui_thread())
        return 0;

    wxWindow* window = find_ui_window(frame_handle);
    UiHandleEntry* frame_entry = find_ui_handle(frame_handle, UiHandleKind::Window);

    // The editor-frame registry is populated only for the primary Eeschema /
    // PCB Editor frame after its KIFACE class-ID check. Use that identity proof
    // instead of RTTI: EDA_DRAW_FRAME does not export its typeinfo from KiCad's
    // hidden-visibility common library, so dynamic_cast would leave an
    // unresolved ELF relocation in this separately loaded KIFACE module.
    if (!window || !frame_entry || g_editor_frames.find(window) == g_editor_frames.end())
        return 0;

    auto* frame = static_cast<EDA_DRAW_FRAME*>(window);
    PROPERTIES_PANEL* panel = frame->GetPropertiesPanel();
    return panel ? register_ui_window(panel, frame_entry->frame) : 0;
}

uint64_t ui_properties_get_grid(uint64_t panel_handle)
{
    if (!on_gui_thread())
        return 0;

    wxWindow* window = find_ui_window(panel_handle);
    UiHandleEntry* panel_entry = find_ui_handle(panel_handle, UiHandleKind::Window);

    if (!window || !panel_entry ||
        g_editor_frames.find(panel_entry->frame) == g_editor_frames.end())
        return 0;

    // A valid panel is exactly the object returned by the registered editor
    // frame. This proves its type without referring to hidden PROPERTIES_PANEL
    // RTTI; only then is the downcast and native property-grid call performed.
    auto* frame = static_cast<EDA_DRAW_FRAME*>(panel_entry->frame);
    if (frame->GetPropertiesPanel() != window)
        return 0;

    auto* panel = static_cast<PROPERTIES_PANEL*>(window);
    wxPropertyGrid* grid = panel->GetPropertyGrid();
    return grid ? register_ui_handle(grid, UiHandleKind::PropertyGrid,
                                     panel_entry->frame, panel) : 0;
}

int32_t ui_property_grid_append_string(uint64_t grid_handle, const char* label,
                                       const char* name, const char* value)
{
    if (!on_gui_thread() || !label || !name || !value)
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    auto* grid = static_cast<wxPropertyGrid*>(entry->pointer);
    grid->Append(new wxStringProperty(wxString::FromUTF8(label), wxString::FromUTF8(name),
                                      wxString::FromUTF8(value)));
    return 1;
}

int32_t ui_property_grid_append_bool(uint64_t grid_handle, const char* label,
                                     const char* name, int32_t value)
{
    if (!on_gui_thread() || !label || !name)
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    static_cast<wxPropertyGrid*>(entry->pointer)->Append(
        new wxBoolProperty(wxString::FromUTF8(label), wxString::FromUTF8(name), value != 0));
    return 1;
}

int32_t ui_property_grid_append_int(uint64_t grid_handle, const char* label,
                                    const char* name, int64_t value)
{
    if (!on_gui_thread() || !label || !name ||
        value < std::numeric_limits<long>::min() || value > std::numeric_limits<long>::max())
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    static_cast<wxPropertyGrid*>(entry->pointer)->Append(
        new wxIntProperty(wxString::FromUTF8(label), wxString::FromUTF8(name),
                          static_cast<long>(value)));
    return 1;
}

int32_t ui_property_grid_append_float(uint64_t grid_handle, const char* label,
                                      const char* name, double value)
{
    if (!on_gui_thread() || !label || !name || !std::isfinite(value))
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    static_cast<wxPropertyGrid*>(entry->pointer)->Append(
        new wxFloatProperty(wxString::FromUTF8(label), wxString::FromUTF8(name), value));
    return 1;
}

int32_t ui_property_grid_set_string(uint64_t grid_handle, const char* name, const char* value)
{
    if (!on_gui_thread() || !name || !value)
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    static_cast<wxPropertyGrid*>(entry->pointer)->SetPropertyValue(
        wxString::FromUTF8(name), wxString::FromUTF8(value));
    return 1;
}

int32_t ui_property_grid_get_string(uint64_t grid_handle, const char* name,
                                    char* buffer, uint32_t capacity)
{
    if (!on_gui_thread() || !name || !buffer || capacity == 0)
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    auto* grid = static_cast<wxPropertyGrid*>(entry->pointer);
    wxPGProperty* property = grid->GetProperty(wxString::FromUTF8(name));
    if (!property)
        return -1;
    copy_utf8_to_buffer(grid->GetPropertyValueAsString(property), buffer, capacity);
    return 1;
}

int32_t ui_property_grid_delete(uint64_t grid_handle, const char* name)
{
    if (!on_gui_thread() || !name)
        return -1;
    UiHandleEntry* entry = find_ui_handle(grid_handle, UiHandleKind::PropertyGrid);
    if (!entry)
        return -1;
    auto* grid = static_cast<wxPropertyGrid*>(entry->pointer);
    const wxString property_name = wxString::FromUTF8(name);
    if (!grid->GetProperty(property_name))
        return 0;
    grid->DeleteProperty(property_name);
    return 1;
}

bool on_gui_thread()
{
    return wxIsMainThread() && g_gui_callbacks_enabled.load(std::memory_order_acquire);
}

OverlayState* find_overlay(uint64_t handle)
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
    std::unordered_set<wxWindow*> menu_frames;
    for (const auto& [binding, entry] : g_menu_bindings)
    {
        (void)binding;
        menu_frames.insert(entry.frame);
    }
    for (wxWindow* frame : menu_frames)
        remove_menu_bindings_for_window(frame);
    if (g_window_event_filter_installed)
    {
        wxEvtHandler::RemoveFilter(&g_window_event_filter);
        g_window_event_filter_installed = false;
    }
    g_window_observers.clear();
    g_watched_ui_windows.clear();
    g_editor_frames.clear();
    g_ui_handles.clear();
    g_menu_bindings.clear();
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
uint64_t overlay_create(void*, void*, void*) { return 0; }
int32_t overlay_destroy(uint64_t) { return -1; }
int32_t overlay_clear(uint64_t) { return -1; }
int32_t overlay_set_style(uint64_t, double, double, double, double, double, double, double, double,
                          double, int32_t, int32_t) { return -1; }
int32_t overlay_line(uint64_t, double, double, double, double) { return -1; }
int32_t overlay_circle(uint64_t, double, double, double) { return -1; }
int32_t overlay_rectangle(uint64_t, double, double, double, double) { return -1; }
uint64_t ui_window_get_handle(void*) { return 0; }
int32_t ui_window_get_top_level_windows(uint64_t, uint64_t*, uint32_t) { return -1; }
uint64_t ui_window_observe(uint64_t, KUA_UiWindowEventCallback, void*) { return 0; }
int32_t ui_window_unobserve(uint64_t) { return -1; }
uint64_t ui_window_create(uint64_t, uint32_t, int32_t, const char*, int32_t) { return 0; }
int32_t ui_window_destroy(uint64_t) { return -1; }
int32_t ui_window_show(uint64_t, int32_t) { return -1; }
int32_t ui_window_get_children(uint64_t, uint64_t*, uint32_t) { return -1; }
int32_t ui_window_get_class_name(uint64_t, char*, uint32_t) { return -1; }
int32_t ui_window_set_text(uint64_t, const char*) { return -1; }
int32_t ui_text_ctrl_set_value(uint64_t, const char*) { return -1; }
int32_t ui_text_ctrl_get_value(uint64_t, char*, uint32_t) { return -1; }
int32_t ui_check_box_set_value(uint64_t, int32_t) { return -1; }
int32_t ui_check_box_get_value(uint64_t) { return -1; }
int32_t ui_choice_append(uint64_t, const char*) { return -1; }
int32_t ui_choice_clear(uint64_t) { return -1; }
int32_t ui_choice_get_count(uint64_t) { return -1; }
int32_t ui_choice_set_selection(uint64_t, int32_t) { return -1; }
int32_t ui_choice_get_selection(uint64_t) { return -1; }
int32_t ui_choice_get_string_selection(uint64_t, char*, uint32_t) { return -1; }
int32_t ui_window_enable(uint64_t, int32_t) { return -1; }
int32_t ui_window_raise(uint64_t) { return -1; }
int32_t ui_window_set_focus(uint64_t) { return -1; }
int32_t ui_window_set_size(uint64_t, int32_t, int32_t) { return -1; }
int32_t ui_window_get_size(uint64_t, int32_t*, int32_t*) { return -1; }
int32_t ui_window_set_position(uint64_t, int32_t, int32_t) { return -1; }
int32_t ui_window_get_position(uint64_t, int32_t*, int32_t*) { return -1; }
uint64_t ui_sizer_create(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t) { return 0; }
int32_t ui_sizer_add(uint64_t, uint64_t, int32_t, int32_t, int32_t) { return -1; }
int32_t ui_window_set_sizer(uint64_t, uint64_t) { return -1; }
int32_t ui_window_layout(uint64_t) { return -1; }
int32_t ui_notebook_add_page(uint64_t, uint64_t, const char*, int32_t) { return -1; }
int32_t ui_notebook_insert_page(uint64_t, uint32_t, uint64_t, const char*, int32_t) { return -1; }
int32_t ui_notebook_remove_page(uint64_t, uint32_t) { return -1; }
int32_t ui_notebook_get_page_count(uint64_t) { return -1; }
int32_t ui_notebook_set_selection(uint64_t, int32_t) { return -1; }
int32_t ui_notebook_get_page_text(uint64_t, uint32_t, char*, uint32_t) { return -1; }
uint64_t ui_menu_bar_get(uint64_t) { return 0; }
uint64_t ui_menu_create(uint64_t) { return 0; }
int32_t ui_menu_append(uint64_t, int32_t, const char*, const char*, uint32_t) { return -1; }
int32_t ui_menu_delete(uint64_t, int32_t) { return -1; }
int32_t ui_menu_bar_append(uint64_t, uint64_t, const char*) { return -1; }
int32_t ui_menu_bar_remove(uint64_t, uint32_t) { return -1; }
uint64_t ui_menu_bind(uint64_t, int32_t, KUA_GuiCallback, void*) { return 0; }
int32_t ui_menu_unbind(uint64_t) { return -1; }
uint64_t ui_aui_get_manager(uint64_t) { return 0; }
int32_t ui_aui_add_pane(uint64_t, uint64_t, const KUA_AuiPaneOptions*) { return -1; }
int32_t ui_aui_detach_pane(uint64_t, uint64_t) { return -1; }
int32_t ui_aui_update(uint64_t) { return -1; }
uint64_t ui_properties_get_panel(uint64_t) { return 0; }
uint64_t ui_properties_get_grid(uint64_t) { return 0; }
int32_t ui_property_grid_append_string(uint64_t, const char*, const char*, const char*) { return -1; }
int32_t ui_property_grid_append_bool(uint64_t, const char*, const char*, int32_t) { return -1; }
int32_t ui_property_grid_append_int(uint64_t, const char*, const char*, int64_t) { return -1; }
int32_t ui_property_grid_append_float(uint64_t, const char*, const char*, double) { return -1; }
int32_t ui_property_grid_set_string(uint64_t, const char*, const char*) { return -1; }
int32_t ui_property_grid_get_string(uint64_t, const char*, char*, uint32_t) { return -1; }
int32_t ui_property_grid_delete(uint64_t, const char*) { return -1; }
#else
uint64_t overlay_create(void* frame, void* canvas, void* view)
{
    if (!on_gui_thread() || !frame || !canvas || !view)
        return 0;
    const auto frame_it = g_editor_frames.find(frame);
    if (frame_it == g_editor_frames.end() || frame_it->second.canvas != canvas ||
        frame_it->second.view != view)
        return 0;
    auto state = std::make_unique<OverlayState>();
    state->frame = frame;
    state->canvas = static_cast<EDA_DRAW_PANEL_GAL*>(canvas);
    state->view = static_cast<KIGFX::VIEW*>(view);
    state->overlay = state->view->MakeOverlay();
    if (!state->overlay)
        return 0;
    uint64_t handle = g_next_overlay_handle++;
    if (handle == 0)
        handle = g_next_overlay_handle++;
    g_overlays.emplace(handle, std::move(state));
    return handle;
}

int32_t overlay_destroy(uint64_t handle)
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

int32_t overlay_clear(uint64_t handle)
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

int32_t overlay_set_style(uint64_t handle, double sr, double sg, double sb, double sa,
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

int32_t overlay_line(uint64_t handle, double x1, double y1, double x2, double y2)
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

int32_t overlay_circle(uint64_t handle, double cx, double cy, double radius)
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

int32_t overlay_rectangle(uint64_t handle, double x1, double y1, double x2, double y2)
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
    host.ui_window_get_handle = &ui_window_get_handle;
    host.ui_window_create = &ui_window_create;
    host.ui_window_destroy = &ui_window_destroy;
    host.ui_window_show = &ui_window_show;
    host.ui_window_get_children = &ui_window_get_children;
    host.ui_window_get_top_level_windows = &ui_window_get_top_level_windows;
    host.ui_window_observe = &ui_window_observe;
    host.ui_window_unobserve = &ui_window_unobserve;
    host.ui_text_ctrl_set_value = &ui_text_ctrl_set_value;
    host.ui_text_ctrl_get_value = &ui_text_ctrl_get_value;
    host.ui_check_box_set_value = &ui_check_box_set_value;
    host.ui_check_box_get_value = &ui_check_box_get_value;
    host.ui_choice_append = &ui_choice_append;
    host.ui_choice_clear = &ui_choice_clear;
    host.ui_choice_get_count = &ui_choice_get_count;
    host.ui_choice_set_selection = &ui_choice_set_selection;
    host.ui_choice_get_selection = &ui_choice_get_selection;
    host.ui_choice_get_string_selection = &ui_choice_get_string_selection;
    host.ui_window_enable = &ui_window_enable;
    host.ui_window_raise = &ui_window_raise;
    host.ui_window_set_focus = &ui_window_set_focus;
    host.ui_window_set_size = &ui_window_set_size;
    host.ui_window_get_size = &ui_window_get_size;
    host.ui_window_set_position = &ui_window_set_position;
    host.ui_window_get_position = &ui_window_get_position;
    host.ui_window_get_class_name = &ui_window_get_class_name;
    host.ui_window_set_text = &ui_window_set_text;
    host.ui_sizer_create = &ui_sizer_create;
    host.ui_sizer_add = &ui_sizer_add;
    host.ui_window_set_sizer = &ui_window_set_sizer;
    host.ui_window_layout = &ui_window_layout;
    host.ui_notebook_add_page = &ui_notebook_add_page;
    host.ui_notebook_insert_page = &ui_notebook_insert_page;
    host.ui_notebook_remove_page = &ui_notebook_remove_page;
    host.ui_notebook_get_page_count = &ui_notebook_get_page_count;
    host.ui_notebook_set_selection = &ui_notebook_set_selection;
    host.ui_notebook_get_page_text = &ui_notebook_get_page_text;
    host.ui_menu_bar_get = &ui_menu_bar_get;
    host.ui_menu_create = &ui_menu_create;
    host.ui_menu_append = &ui_menu_append;
    host.ui_menu_delete = &ui_menu_delete;
    host.ui_menu_bar_append = &ui_menu_bar_append;
    host.ui_menu_bar_remove = &ui_menu_bar_remove;
    host.ui_menu_bind = &ui_menu_bind;
    host.ui_menu_unbind = &ui_menu_unbind;
    host.ui_aui_get_manager = &ui_aui_get_manager;
    host.ui_aui_add_pane = &ui_aui_add_pane;
    host.ui_aui_detach_pane = &ui_aui_detach_pane;
    host.ui_aui_update = &ui_aui_update;
    host.ui_properties_get_panel = &ui_properties_get_panel;
    host.ui_properties_get_grid = &ui_properties_get_grid;
    host.ui_property_grid_append_string = &ui_property_grid_append_string;
    host.ui_property_grid_append_bool = &ui_property_grid_append_bool;
    host.ui_property_grid_append_int = &ui_property_grid_append_int;
    host.ui_property_grid_append_float = &ui_property_grid_append_float;
    host.ui_property_grid_set_string = &ui_property_grid_set_string;
    host.ui_property_grid_get_string = &ui_property_grid_get_string;
    host.ui_property_grid_delete = &ui_property_grid_delete;
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

    if (std::getenv("KUA_SMOKE_LOG"))
        log_message(1, ("Proxy KIFACE_1 entered: " + proxy_path.string()).c_str());

    const auto original_path = original_module_path(proxy_path);
    if (std::getenv("KUA_SMOKE_LOG"))
        log_message(1, ("Opening preserved KiCad KIFACE: " + original_path.string()).c_str());
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
        if (std::getenv("KUA_SMOKE_LOG"))
            log_message(1, "Proxy OnKifaceStart entered.");

        if (!m_original || !m_original->OnKifaceStart(program, control_bits, kiway))
        {
            if (std::getenv("KUA_SMOKE_LOG"))
                log_message(3, "Preserved original KIFACE OnKifaceStart failed.");
            return false;
        }

        if (std::getenv("KUA_SMOKE_LOG"))
            log_message(1, "Preserved original KIFACE OnKifaceStart succeeded.");

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
            register_ui_window(window, window);
            // EDA_BASE_FRAME creates its initial menu bar with CallAfter().
            // Queue extensions after that setup, while guarding the borrowed frame
            // against destruction before the callback runs.
            wxWeakRef<wxWindow> weak_window(window);
            window->CallAfter([weak_window, context]() {
                wxWindow* live_window = weak_window.get();
                if (!live_window || live_window->IsBeingDeleted() ||
                    !g_gui_callbacks_enabled.load(std::memory_order_acquire))
                    return;

                const auto frame_state = g_editor_frames.find(live_window);
                if (frame_state == g_editor_frames.end())
                    return;

                KUA_EditorContext live_context = context;
                live_context.frame = live_window;
                live_context.canvas = frame_state->second.canvas;
                live_context.view = frame_state->second.view;
                notify_window_created(live_context);
            });
#else
            notify_window_created(context);
#endif
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
        if (std::getenv("KUA_SMOKE_LOG"))
            log_message(1, ("Extension search directory: " + extension_dir.string()).c_str());
        if (!std::filesystem::is_directory(extension_dir, ec))
        {
            if (std::getenv("KUA_SMOKE_LOG"))
                log_message(2, ("Extension directory is unavailable: " + ec.message()).c_str());
            return;
        }

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

            // Extensions are invoked synchronously on the GUI thread. Production
            // callers defer this until initial frame UI setup has completed and
            // retain a weak wxWindow reference for the duration of the callback.
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
    if (std::getenv("KUA_SMOKE_LOG"))
        log_message(1, "Proxy KIFACE_1 entry point was called.");

    KIFACE_GETTER_FUNC* unused_getter = nullptr;
    if (!load_original(&unused_getter, version, kiway_version, program))
        return nullptr;

    if (!g_proxy)
        g_proxy = new KIFACE_PROXY(g_original_kiface);
    g_proxy->set_program(program);
    return g_proxy;
}
