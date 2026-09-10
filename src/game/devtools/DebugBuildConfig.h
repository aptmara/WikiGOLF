#pragma once

namespace game::debug {

#ifdef WIKIGOLF_DEBUG_TOOLS
inline constexpr bool kDebugToolsEnabled = true;
#else
inline constexpr bool kDebugToolsEnabled = false;
#endif

} // namespace game::debug
