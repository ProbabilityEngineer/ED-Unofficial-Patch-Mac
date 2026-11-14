// Stage 2b stand-in (ed-metalfx, 2026-11-14): the private graphics bridge (the VR runtime's command lists on the game's
// immediate context) is gone with the OpenXR runtime. With no consumer, no list is ever private: every
// ExecuteCommandList is the game's own, as it always was in a flat session.
#pragma once

#include <d3d11.h>

#include <cstdint>

namespace edvr {

enum class HookMode : uint32_t;

inline bool graphicsBridgeInstallTransport(ID3D11Device*, HookMode) { return false; }
inline void graphicsBridgeUninstallTransport() {}
inline bool graphicsBridgeConsumePermit(ID3D11DeviceContext*, ID3D11CommandList*, BOOL) { return false; }
inline void graphicsBridgeNoteUnknownExecution() {}

}  // namespace edvr
