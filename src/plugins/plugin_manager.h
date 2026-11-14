#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace edvr::plugins {

struct CandidatePlugin {
    uint32_t id = 0;        // stable for the manager's life; a menu row holds this, never a pointer
    std::wstring path;
    std::string stem;       // e.g. "edvr_mfd"
    std::string sha256;
    bool approved = false;
    // Another file in plugins\ has the same canonical identity (the sanitized stem, so plugins\foo.dll, plugins\foo\plugin.dll and
    // foo-bar.dll / foo_bar.dll all collide). Every file of such a group is refused: none loads and none can be approved.
    bool conflicted = false;
    // Why the last attempt to load this candidate failed, short enough for a menu hint; empty when none failed. Cleared by a load that succeeds.
    std::string loadNote;
};


struct ApprovalResult {
    // The approval took effect: the candidate existed, was not yet approved, and the addon loaded and initialised. Only then does the
    // caller record the approval in [addons]. False for a stale id, an already approved candidate, a changed file (it resurfaces under
    // its new hash) and any failed load (the candidate stays unapproved so the approval can be retried).
    bool applied = false;
    bool loaded = false;    // ...and the addon is running
    std::string stem;
    std::string sha256;     // the hash that was approved; what belongs in [addons]
    std::string reason;     // short, for the menu: why an approval did not take effect; empty when it did or the id named nothing
};

// ed-metalfx (stage 2c): add-ons were VR-only (dormant in the flat profile), so the manager is a stub that never discovers or loads
// anything. The candidate and approval types stay because the F8 menu's add-on rows are still compiled; with no candidates they never show.
class PluginManager {
public:
    static PluginManager& instance() { static PluginManager m; return m; }
    bool initialize(const std::wstring&) { return true; }
    void shutdown() {}
    void onProfileSwitched() {}
    bool hasActivePlugins() { return false; }
    std::vector<CandidatePlugin> candidatesSnapshot() { return {}; }
    ApprovalResult approveCandidate(uint32_t) { return {}; }
    bool candidateById(uint32_t, CandidatePlugin*) { return false; }
};

} // namespace edvr::plugins
