// The few Dear ImGui names the tests and benchmarks use that are not the same
// in every version the library accepts (IMTYPESET_IMGUI_VERSION_NUM_MIN up).
// The library uses none of them: it writes its own quads and never touches a
// draw list's flags.
//
//   19274  ImGuiPlatformIO::DrawCallback_ResetRenderState, _SetSamplerLinear,
//          _SetSamplerNearest appear (tests/support/soft_raster.h).
//   19282  ImDrawListFlags_TextNoPixelSnap appears. Before it AddText always
//          truncates its position to whole pixels, which changes nothing for a
//          position that is whole already.
//   19298  ImDrawListFlags is merged into ImDrawFlags; the old names remain
//          only without IMGUI_DISABLE_OBSOLETE_FUNCTIONS.
//
// The numbers were read from the IMGUI_VERSION_NUM of the commit that
// introduced each name.

#pragma once

#include "imgui.h"

namespace imgui_compat
{

#if IMGUI_VERSION_NUM >= 19298
using DrawListFlags = ImDrawFlags;
constexpr DrawListFlags AllowVtxOffset = ImDrawFlags_UseVtxOffset;
constexpr DrawListFlags TextNoPixelSnap = ImDrawFlags_TextNoPixelSnap;
#else
using DrawListFlags = ImDrawListFlags;
constexpr DrawListFlags AllowVtxOffset = ImDrawListFlags_AllowVtxOffset;
#if IMGUI_VERSION_NUM >= 19282
constexpr DrawListFlags TextNoPixelSnap = ImDrawListFlags_TextNoPixelSnap;
#else
constexpr DrawListFlags TextNoPixelSnap = 0;
#endif
#endif

} // namespace imgui_compat
