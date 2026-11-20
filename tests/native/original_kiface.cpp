#include "synthetic_kiface.hpp"
#include <array>
#if defined(_WIN32)
#  define TEST_EXPORT __declspec(dllexport)
#else
#  define TEST_EXPORT __attribute__((visibility("default")))
#endif
namespace
{
enum Counter { Start, End, ResetCount, CreateWindow, SaveAs, Address, Actions, Job, JobConfig, OpenDocument,
 CloseDocument, Preload, CancelPreloadCount, ProjectChangedCount, RegisterLibraries, LoadLibraries, Count };
std::array<int, Count> calls{};
class OriginalKiface final : public KIFACE
{
public:
 bool OnKifaceStart(PGM_BASE*, int, KIWAY*) override { ++calls[Start]; return true; }
 void OnKifaceEnd() override { ++calls[End]; }
 void Reset() override { ++calls[ResetCount]; }
 wxWindow* CreateKiWindow(wxWindow*, int, KIWAY*, int) override { ++calls[CreateWindow]; return reinterpret_cast<wxWindow*>(static_cast<uintptr_t>(0x1234)); }
 void SaveFileAs(const wxString&, const wxString&, const wxString&, const wxString&, const wxString&, wxString&) override { ++calls[SaveAs]; }
 void* IfaceOrAddress(int) override { ++calls[Address]; return reinterpret_cast<void*>(static_cast<uintptr_t>(0x4567)); }
 void GetActions(std::vector<TOOL_ACTION*>& actions) const override { ++calls[Actions]; actions.push_back(reinterpret_cast<TOOL_ACTION*>(static_cast<uintptr_t>(0x89))); }
 int HandleJob(JOB*, REPORTER*, PROGRESS_REPORTER*) override { ++calls[Job]; return 73; }
 bool HandleJobConfig(JOB*, wxWindow*) override { ++calls[JobConfig]; return true; }
 bool HandleApiOpenDocument(const DOCUMENT_SPEC&, KICAD_API_SERVER*, wxString*) override { ++calls[OpenDocument]; return true; }
 bool HandleApiCloseDocument(const DOCUMENT_SPEC&, KICAD_API_SERVER*, wxString*) override { ++calls[CloseDocument]; return true; }
 void PreloadLibraries(KIWAY*) override { ++calls[Preload]; }
 void CancelPreload(bool) override { ++calls[CancelPreloadCount]; }
 void ProjectChanged() override { ++calls[ProjectChangedCount]; }
 void RegisterLibraryHandlers(KICAD_API_SERVER*) override { ++calls[RegisterLibraries]; }
 bool LoadAllLibraries() override { ++calls[LoadLibraries]; return true; }
};
OriginalKiface instance;
}
extern "C" TEST_EXPORT KIFACE* KIFACE_1(int* version, int, PGM_BASE*)
{ if (version) *version = 1; return &instance; }
extern "C" TEST_EXPORT int KUA_OriginalCounter(int index)
{ if (index < 0 || index >= Count) return -1; return calls[static_cast<size_t>(index)]; }
