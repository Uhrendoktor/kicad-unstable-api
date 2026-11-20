#ifndef KUA_SYNTHETIC_KIFACE_HPP
#define KUA_SYNTHETIC_KIFACE_HPP
#include <cstdint>
#include <vector>
class wxWindow {};
struct wxString { int marker{}; };
class wxApp {};
class PGM_BASE {};
class KIWAY {};
class LIB_ID {};
class KICAD_API_SERVER {};
class JOB {};
class REPORTER {};
class PROGRESS_REPORTER {};
class TOOL_ACTION {};
struct KIFACE
{
    struct DOCUMENT_SPEC { enum class KIND { FILE_KIND, LIBID_KIND, CREATE_KIND }; KIND kind = KIND::FILE_KIND; };
    virtual ~KIFACE() noexcept = default;
    virtual bool OnKifaceStart(PGM_BASE*, int, KIWAY*) = 0;
    virtual void OnKifaceEnd() = 0;
    virtual void Reset() = 0;
    virtual wxWindow* CreateKiWindow(wxWindow*, int, KIWAY*, int = 0) = 0;
    virtual void SaveFileAs(const wxString&, const wxString&, const wxString&, const wxString&, const wxString&, wxString&) {}
    virtual void* IfaceOrAddress(int) = 0;
    virtual void GetActions(std::vector<TOOL_ACTION*>&) const = 0;
    virtual int HandleJob(JOB*, REPORTER*, PROGRESS_REPORTER*) { return 0; }
    virtual bool HandleJobConfig(JOB*, wxWindow*) { return false; }
    virtual bool HandleApiOpenDocument(const DOCUMENT_SPEC&, KICAD_API_SERVER*, wxString*) { return false; }
    virtual bool HandleApiCloseDocument(const DOCUMENT_SPEC&, KICAD_API_SERVER*, wxString*) { return false; }
    virtual void PreloadLibraries(KIWAY*) {}
    virtual void CancelPreload(bool = true) {}
    virtual void ProjectChanged() {}
    virtual void RegisterLibraryHandlers(KICAD_API_SERVER*) {}
    virtual bool LoadAllLibraries() { return false; }
};
using KIFACE_GETTER_FUNC = KIFACE*(int*, int, PGM_BASE*);
#endif
